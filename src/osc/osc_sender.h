#pragma once

#include <string>
#include <cstdint>
#include <mutex>

// Minimal UDP OSC 1.0 sender. Windows-only (Winsock2). One socket per
// instance, one destination host:port. Send calls are serialised so the
// object is safe to hit from arbitrary threads.
class OscSender {
public:
    OscSender();
    ~OscSender();

    // Open the UDP socket and remember the destination. Returns false if
    // WSAStartup / socket() fails. init() may only be called once per
    // instance; call shutdown() before re-initing.
    bool init(const std::string& host, uint16_t port);
    void shutdown();
    bool isOpen() const { return m_socket != kInvalidSocket; }

    // Send an OSC message with a single float32 argument. Address must
    // start with '/'. Returns false on send error.
    bool sendFloat(const std::string& address, float value);

private:
    // Winsock's SOCKET is uintptr_t on Windows. We stash it as uint64_t to
    // avoid including winsock2.h from this header.
    static constexpr uint64_t kInvalidSocket = (uint64_t)-1;
    uint64_t     m_socket = kInvalidSocket;
    std::string  m_host;
    uint16_t     m_port      = 0;
    bool         m_wsaInited = false;
    std::mutex   m_mutex;
};
