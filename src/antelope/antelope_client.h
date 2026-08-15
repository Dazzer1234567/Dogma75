#pragma once

#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <functional>

// Minimal blocking-TCP client for the AntelopeAudioServer (127.0.0.1:2021).
// Speaks the same "authoritative client" handshake the Control Panel uses:
// on connect we send `initialize_format` with authorative=true, then send
// commands as raw `[cmd, args, kwargs]` requests over the same persistent
// socket. That's the only path that actually writes the DSP — the
// `send_notification` wrapper we used before only moved the CP's UI.
// Multiple authoritative clients coexist; the CP does NOT need to be
// closed.
//
// A background reader thread holds a SECOND authoritative socket and
// listens for `set_mixer_cfg` notifications from the server, updating an
// in-memory per-channel state cache. This lets a mute-toggle re-send the
// user's current level/pan/etc instead of stomping them with defaults —
// the Antelope protocol has no partial-update setter, so we always have
// to send the full 7-tuple.
class AntelopeClient {
public:
    AntelopeClient();
    ~AntelopeClient();

    // Open the TCP socket. Non-fatal if it fails — every method becomes a
    // no-op. Log messages go through daw_log so we can diagnose after the
    // fact without pausing the audio path.
    bool init(const std::string& host = "127.0.0.1", uint16_t port = 2021);
    void shutdown();
    bool isConnected() const { return m_socket != kInvalidSocket; }

    // Mute / unmute a mixer channel (1-based, matches the CP's labelling
    // where "Ch 1" = mixerId=0, channelId=1). Uses the cached per-channel
    // state for level/pan/solo/send so toggling mute doesn't yank other
    // fields. If nothing is cached yet (we've never observed the channel
    // in a broadcast), we fall back to unity level / centre pan.
    void setChannelMute(int channelId1Based, bool muted);

    // Fired from the READER THREAD whenever an observed channel's mute
    // state changes — including changes made in the Antelope Control
    // Panel by hand, which is the point: it lets the DAW follow the mixer
    // instead of only driving it.
    //
    // Our own writes echo back here too (the server broadcasts every
    // change to all clients). That is harmless as long as the handler is
    // idempotent: applying a state that already matches must not send
    // anything back, or the two ends will ping-pong forever.
    using MuteChangeCallback = std::function<void(int channelId1Based, bool muted)>;
    void setMuteChangeCallback(MuteChangeCallback cb);

private:
    // Send one raw set_mixer_cfg request `[cmd, args, {}]` on our
    // authoritative send socket. Caller supplies the full 7-arg vector
    // for the wire. If the socket is dead we transparently reconnect,
    // re-handshake, and retry once.
    void sendSetMixerCfg(int mixerId, int channelId,
                         int level, int pan, int mute, int solo, int send);

    // Open the TCP socket and immediately send the initialize_format
    // handshake with authorative=true. Assumes m_mutex is held.
    bool connectLocked();
    // Do a single framed send attempt. Returns false on error and
    // leaves the socket closed; caller decides whether to retry.
    bool trySendLocked(const unsigned char* hdr, int hdrLen,
                       const char* body,       int bodyLen);

    // Reader thread body: opens its own authoritative socket, reads
    // length-prefixed JSON frames, and updates m_state for each
    // observed `set_mixer_cfg` notification. Reconnects on any error.
    void readerLoop();

    // Per-channel state, indexed by ((mixerId << 8) | channelId).
    // Populated from broadcasts; consumed by sendSetMixerCfg via
    // setChannelMute so mute-toggles echo back the user's real level.
    struct ChState {
        int level;   // 0 = unity/top ... 95 = -inf silence
        int pan;     // 32 = centre
        int mute;    // 0/1
        int solo;    // 0/1
        int send;    // 0..95
        bool valid;  // false until at least one broadcast populates it
    };
    ChState stateFor(int mixerId, int channelId) const;

    static constexpr uint64_t kInvalidSocket = (uint64_t)-1;
    uint64_t     m_socket   = kInvalidSocket;
    bool         m_wsaInited = false;
    std::string  m_host;
    uint16_t     m_port     = 0;
    mutable std::mutex m_mutex;

    std::thread              m_reader;
    std::atomic<bool>        m_readerStop{false};
    std::atomic<uint64_t>    m_readerSocket{kInvalidSocket};

    mutable std::mutex                     m_stateMutex;
    std::unordered_map<uint32_t, ChState>  m_state;

    mutable std::mutex m_callbackMutex;
    MuteChangeCallback m_onMuteChanged;
};
