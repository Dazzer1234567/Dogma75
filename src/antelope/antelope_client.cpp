#include "antelope_client.h"
#include "../util/daw_log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdio>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace {
// Fallback defaults used only when we've never observed a channel via
// a broadcast (so the cache is empty). Once the reader thread sees any
// broadcast for the channel we echo those real values back instead.
// Antelope value scale (per the CP source, confirmed on-device):
//   level 0 = unity (top of fader, displays "+0.0"), 95 = -inf silence
//   pan   32 = centre (0)
//   send  95 = max (the CP's initial_value)
constexpr int kDefLevel = 0;
constexpr int kDefPan   = 32;
constexpr int kDefSolo  = 0;
constexpr int kDefSend  = 95;

inline uint32_t stateKey(int mixerId, int channelId) {
    return (static_cast<uint32_t>(mixerId) << 8) |
           (static_cast<uint32_t>(channelId) & 0xFF);
}

// The CP's handshake body. `authorative` (their misspelling) MUST be
// exact — the server matches the string. Minimum viable declaration:
// empty cyclic_reports and requests are fine; the server doesn't gate
// on which requests we've listed here, only on whether we sent
// initialize_format at all.
constexpr const char kHandshakeBody[] =
    "[\"initialize_format\", [{"
    "\"authorative\": true, "
    "\"version\": 1, "
    "\"cyclic_reports\": {\"0x73\": [], \"0x83\": []}, "
    "\"requests\": {}"
    "}], {}]";
}  // namespace

AntelopeClient::AntelopeClient() = default;
AntelopeClient::~AntelopeClient() { shutdown(); }

bool AntelopeClient::init(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket != kInvalidSocket) return true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        dawLog("AntelopeClient: WSAStartup failed");
        return false;
    }
    m_wsaInited = true;
    m_host = host;
    m_port = port;

    if (!connectLocked()) {
        WSACleanup();
        m_wsaInited = false;
        return false;
    }

    // Fire up the notification reader on its own authoritative socket
    // so we can track other clients' (mainly the CP's) mixer changes
    // and echo them back on our own mute-toggle sends.
    m_readerStop.store(false);
    m_reader = std::thread(&AntelopeClient::readerLoop, this);
    return true;
}

void AntelopeClient::shutdown() {
    m_readerStop.store(true);
    {
        SOCKET rs = static_cast<SOCKET>(m_readerSocket.load());
        if (rs != INVALID_SOCKET && rs != static_cast<SOCKET>(kInvalidSocket)) {
            // Force the blocked recv to return so the reader thread exits.
            ::shutdown(rs, SD_BOTH);
            closesocket(rs);
            m_readerSocket.store(kInvalidSocket);
        }
    }
    if (m_reader.joinable()) m_reader.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = kInvalidSocket;
    }
    if (m_wsaInited) {
        WSACleanup();
        m_wsaInited = false;
    }
}

bool AntelopeClient::connectLocked() {
    if (m_socket != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = kInvalidSocket;
    }
    if (!m_wsaInited || m_port == 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    // TCP_NODELAY matches the CP's `remote_device.try_connect` — the
    // command traffic is bursty and tiny, no benefit from Nagle.
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&flag), sizeof(flag));

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(m_port);
    if (inet_pton(AF_INET, m_host.c_str(), &dst.sin_addr) != 1) {
        closesocket(s); return false;
    }
    if (connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == SOCKET_ERROR) {
        dawLog("AntelopeClient: connect %s:%d failed (WSA %d)",
               m_host.c_str(), m_port, WSAGetLastError());
        closesocket(s); return false;
    }
    m_socket = static_cast<uint64_t>(s);

    // Handshake FIRST message on the socket, framed with the same
    // length prefix as everything else. Without this the server accepts
    // our commands but never runs the DSP handler for them.
    int hbLen = static_cast<int>(std::strlen(kHandshakeBody));
    uint32_t total = static_cast<uint32_t>(4 + hbLen);
    unsigned char hdr[4] = {
        static_cast<unsigned char>((total >> 24) & 0xFF),
        static_cast<unsigned char>((total >> 16) & 0xFF),
        static_cast<unsigned char>((total >> 8)  & 0xFF),
        static_cast<unsigned char>( total        & 0xFF),
    };
    if (!trySendLocked(hdr, 4, kHandshakeBody, hbLen)) {
        dawLog("AntelopeClient: handshake send failed");
        return false;
    }
    dawLog("AntelopeClient: connected + handshake OK to %s:%d",
           m_host.c_str(), m_port);
    return true;
}

bool AntelopeClient::trySendLocked(const unsigned char* hdr, int hdrLen,
                                   const char* body, int bodyLen) {
    if (m_socket == kInvalidSocket) return false;
    SOCKET s = static_cast<SOCKET>(m_socket);
    if (::send(s, reinterpret_cast<const char*>(hdr), hdrLen, 0) != hdrLen ||
        ::send(s, body, bodyLen, 0) != bodyLen) {
        int err = WSAGetLastError();
        dawLog("AntelopeClient: send failed (WSA %d) — dropping socket", err);
        closesocket(s);
        m_socket = kInvalidSocket;
        return false;
    }
    return true;
}

void AntelopeClient::sendSetMixerCfg(int mixerId, int channelId,
                                     int level, int pan, int mute,
                                     int solo, int send) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Raw request on our authoritative socket: `[cmd, args, kwargs]`.
    // The server runs the command handler (writes DSP + broadcasts) only
    // when it arrives this way, not when wrapped in `send_notification`.
    char body[192];
    int n = std::snprintf(
        body, sizeof(body),
        "[\"set_mixer_cfg\", [%d, %d, %d, %d, %d, %d, %d], {}]",
        mixerId, channelId, level, pan, mute, solo, send);
    if (n <= 0 || n >= (int)sizeof(body)) return;

    uint32_t total = static_cast<uint32_t>(4 + n);
    unsigned char hdr[4] = {
        static_cast<unsigned char>((total >> 24) & 0xFF),
        static_cast<unsigned char>((total >> 16) & 0xFF),
        static_cast<unsigned char>((total >> 8)  & 0xFF),
        static_cast<unsigned char>( total        & 0xFF),
    };

    // Try send; on failure the socket is dropped and we reconnect
    // (which re-does the initialize_format handshake) and retry once.
    if (!trySendLocked(hdr, 4, body, n)) {
        if (connectLocked()) {
            if (trySendLocked(hdr, 4, body, n)) {
                dawLog("AntelopeClient: sent (after reconnect) %s", body);
            }
        } else {
            dawLog("AntelopeClient: reconnect failed, message dropped");
        }
    } else {
        dawLog("AntelopeClient: sent %s", body);
    }
}

AntelopeClient::ChState AntelopeClient::stateFor(int mixerId, int channelId) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    auto it = m_state.find(stateKey(mixerId, channelId));
    if (it != m_state.end() && it->second.valid) return it->second;
    return ChState{kDefLevel, kDefPan, 0, kDefSolo, kDefSend, false};
}

void AntelopeClient::setChannelMute(int channelId1Based, bool muted) {
    if (channelId1Based <= 0) return;
    ChState s = stateFor(/*mixerId*/ 0, channelId1Based);
    dawLog("AntelopeClient: setChannelMute ch=%d mute=%d (cached L=%d P=%d S=%d Snd=%d valid=%d)",
           channelId1Based, muted ? 1 : 0,
           s.level, s.pan, s.solo, s.send, s.valid ? 1 : 0);
    sendSetMixerCfg(/*mixerId*/ 0,
                    channelId1Based,
                    s.level, s.pan,
                    muted ? 1 : 0,
                    s.solo, s.send);
}

// ---------------- Reader thread ----------------

namespace {

// Cheap in-place JSON scan: find `"key":` then read the next integer.
// The notification body is small and machine-generated by the server,
// so we don't need a full parser — just enough to pull the 7 mixer_cfg
// values out. Returns false if the key wasn't found or wasn't followed
// by a number.
bool findIntAfter(const char* buf, int len, const char* key, int& out) {
    int klen = static_cast<int>(std::strlen(key));
    for (int i = 0; i + klen < len; ++i) {
        if (std::memcmp(buf + i, key, klen) != 0) continue;
        int j = i + klen;
        while (j < len && (buf[j] == ' ' || buf[j] == '\t' ||
                           buf[j] == ':' || buf[j] == '\n' ||
                           buf[j] == '\r')) ++j;
        if (j >= len) return false;
        int sign = 1;
        if (buf[j] == '-') { sign = -1; ++j; }
        if (j >= len || buf[j] < '0' || buf[j] > '9') continue;
        int v = 0;
        while (j < len && buf[j] >= '0' && buf[j] <= '9') {
            v = v * 10 + (buf[j] - '0');
            ++j;
        }
        out = sign * v;
        return true;
    }
    return false;
}

// Pull the args array out of a set_mixer_cfg notification body and
// return the 7 ints. The wire looks roughly like:
//   {"type":"notification","contents":["set_mixer_cfg",[0,1,0,32,0,0,95],{}]}
// We just find the "[" after `set_mixer_cfg` and read seven ints.
bool parseMixerCfg(const char* body, int len, int out[7]) {
    const char* marker = "set_mixer_cfg";
    int mlen = static_cast<int>(std::strlen(marker));
    const char* p = nullptr;
    for (int i = 0; i + mlen < len; ++i) {
        if (std::memcmp(body + i, marker, mlen) == 0) { p = body + i + mlen; break; }
    }
    if (!p) return false;
    const char* end = body + len;
    while (p < end && *p != '[') ++p;
    if (p >= end) return false;
    ++p; // step past '['
    for (int i = 0; i < 7; ++i) {
        while (p < end && (*p == ' ' || *p == ',')) ++p;
        int sign = 1;
        if (p < end && *p == '-') { sign = -1; ++p; }
        if (p >= end || *p < '0' || *p > '9') return false;
        int v = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            ++p;
        }
        out[i] = sign * v;
    }
    return true;
}

}  // namespace

void AntelopeClient::readerLoop() {
    // Own socket, own handshake — simpler than sharing the writer's
    // socket, and having two authoritative clients (us and us) is fine.
    while (!m_readerStop.load()) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) { break; }

        int flag = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));

        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(m_port);
        if (inet_pton(AF_INET, m_host.c_str(), &dst.sin_addr) != 1) {
            closesocket(s); break;
        }
        if (connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == SOCKET_ERROR) {
            closesocket(s);
            // Back off then retry — the server might be starting up.
            for (int i = 0; i < 20 && !m_readerStop.load(); ++i) Sleep(100);
            continue;
        }

        // Same initialize_format handshake as the writer socket.
        int hbLen = static_cast<int>(std::strlen(kHandshakeBody));
        uint32_t total = static_cast<uint32_t>(4 + hbLen);
        unsigned char hdr[4] = {
            static_cast<unsigned char>((total >> 24) & 0xFF),
            static_cast<unsigned char>((total >> 16) & 0xFF),
            static_cast<unsigned char>((total >> 8)  & 0xFF),
            static_cast<unsigned char>( total        & 0xFF),
        };
        if (::send(s, reinterpret_cast<const char*>(hdr), 4, 0) != 4 ||
            ::send(s, kHandshakeBody, hbLen, 0) != hbLen) {
            closesocket(s);
            continue;
        }
        m_readerSocket.store(static_cast<uint64_t>(s));
        dawLog("AntelopeClient: reader connected + handshake OK");

        // Frame reader loop. Server pushes broadcasts as length-prefixed
        // JSON frames on this socket; we accumulate a buffer and pop
        // complete frames off the front.
        std::vector<char> buf;
        buf.reserve(4096);
        char chunk[4096];
        while (!m_readerStop.load()) {
            int n = ::recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.insert(buf.end(), chunk, chunk + n);

            while (buf.size() >= 4) {
                uint32_t frameLen =
                    (static_cast<uint32_t>(static_cast<unsigned char>(buf[0])) << 24) |
                    (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 16) |
                    (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 8)  |
                    (static_cast<uint32_t>(static_cast<unsigned char>(buf[3])));
                if (frameLen < 4 || frameLen > 1u * 1024u * 1024u) {
                    // Stream desync — bail and reconnect.
                    buf.clear();
                    ::shutdown(s, SD_BOTH);
                    break;
                }
                if (buf.size() < frameLen) break; // wait for more

                const char* body = buf.data() + 4;
                int bodyLen = static_cast<int>(frameLen - 4);

                // Only interested in set_mixer_cfg notifications.
                int vals[7];
                if (parseMixerCfg(body, bodyLen, vals)) {
                    ChState st{
                        vals[2], vals[3], vals[4], vals[5], vals[6], true,
                    };
                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        m_state[stateKey(vals[0], vals[1])] = st;
                    }
                    dawLog("AntelopeClient: rx set_mixer_cfg mix=%d ch=%d L=%d P=%d M=%d S=%d Snd=%d",
                           vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6]);
                }

                buf.erase(buf.begin(), buf.begin() + frameLen);
            }
        }

        m_readerSocket.store(kInvalidSocket);
        closesocket(s);
        if (!m_readerStop.load()) {
            dawLog("AntelopeClient: reader socket closed, reconnecting");
            for (int i = 0; i < 10 && !m_readerStop.load(); ++i) Sleep(100);
        }
    }
    dawLog("AntelopeClient: reader thread exiting");
}
