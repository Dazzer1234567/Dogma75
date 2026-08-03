#include "osc_sender.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

namespace {

// OSC strings are null-terminated then zero-padded to a 4-byte boundary.
void appendPaddedString(std::vector<char>& buf, const char* s) {
    while (*s) buf.push_back(*s++);
    buf.push_back('\0');
    while (buf.size() % 4) buf.push_back('\0');
}

// OSC float32 is big-endian IEEE-754.
void appendFloatBE(std::vector<char>& buf, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    uint32_t be = htonl(bits);
    const char* p = reinterpret_cast<const char*>(&be);
    buf.insert(buf.end(), p, p + 4);
}

}  // namespace

OscSender::OscSender() = default;

OscSender::~OscSender() { shutdown(); }

bool OscSender::init(const std::string& host, uint16_t port) {
    if (m_socket != kInvalidSocket) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    m_wsaInited = true;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        m_wsaInited = false;
        return false;
    }
    m_socket = (uint64_t)s;
    m_host = host;
    m_port = port;
    return true;
}

void OscSender::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket != kInvalidSocket) {
        closesocket((SOCKET)m_socket);
        m_socket = kInvalidSocket;
    }
    if (m_wsaInited) {
        WSACleanup();
        m_wsaInited = false;
    }
}

bool OscSender::sendFloat(const std::string& address, float value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket == kInvalidSocket) return false;

    std::vector<char> buf;
    buf.reserve(address.size() + 12);
    appendPaddedString(buf, address.c_str());
    appendPaddedString(buf, ",f");
    appendFloatBE(buf, value);

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(m_port);
    if (inet_pton(AF_INET, m_host.c_str(), &dst.sin_addr) != 1) return false;

    int sent = sendto((SOCKET)m_socket, buf.data(), (int)buf.size(), 0,
                      reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    return sent == (int)buf.size();
}
