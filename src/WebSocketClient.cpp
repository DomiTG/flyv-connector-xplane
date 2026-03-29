#include "WebSocketClient.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ── Platform-specific sleep / error ──────────────────────────────────────────
#if defined(IBM) || defined(_WIN32)
#  include <windows.h>
void WebSocketClient::SleepMs(unsigned ms) { ::Sleep(ms); }
int  WebSocketClient::LastError()          { return static_cast<int>(::WSAGetLastError()); }
#else
#  include <thread>
#  include <chrono>
void WebSocketClient::SleepMs(unsigned ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
int WebSocketClient::LastError() { return errno; }
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void WebSocketClient::Start() {
#if defined(IBM) || defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&WebSocketClient::ThreadFunc, this);
}

void WebSocketClient::Stop() {
    running_.store(false, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
#if defined(IBM) || defined(_WIN32)
    WSACleanup();
#endif
}

void WebSocketClient::Send(std::string payload) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (sendQueue_.size() >= kMaxQueueDepth) {
        sendQueue_.pop();   // drop oldest to prevent unbounded growth
    }
    sendQueue_.push(std::move(payload));
}

// ─────────────────────────────────────────────────────────────────────────────
// Background thread
// ─────────────────────────────────────────────────────────────────────────────

void WebSocketClient::ThreadFunc() {
    while (running_.load(std::memory_order_relaxed)) {
        // ── Connect ───────────────────────────────────────────────────────
        SocketFd fd = ConnectSocket(host_, port_);
        if (fd == WS_INVALID_SOCKET) {
            SleepMs(reconnectDelaySec_ * 1000u);
            continue;
        }

        // ── WebSocket handshake ───────────────────────────────────────────
        if (!PerformHandshake(fd, host_, port_)) {
            CloseSocket(fd);
            SleepMs(reconnectDelaySec_ * 1000u);
            continue;
        }

        connected_.store(true, std::memory_order_relaxed);

        // ── Send loop ─────────────────────────────────────────────────────
        while (running_.load(std::memory_order_relaxed)) {
            // Drain the queue under the lock, then send without holding it
            std::queue<std::string> localQueue;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                std::swap(localQueue, sendQueue_);
            }

            while (!localQueue.empty()) {
                const std::string& msg = localQueue.front();
                if (!SendFrame(fd, msg)) {
                    goto reconnect;   // send error → reconnect
                }
                localQueue.pop();
            }

            SleepMs(1);   // yield; items arrive at ~100 ms intervals anyway
        }

    reconnect:
        connected_.store(false, std::memory_order_relaxed);
        CloseSocket(fd);
        if (running_.load(std::memory_order_relaxed)) {
            SleepMs(reconnectDelaySec_ * 1000u);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket helpers
// ─────────────────────────────────────────────────────────────────────────────

SocketFd WebSocketClient::ConnectSocket(const std::string& host, uint16_t port) {
    SocketFd fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == WS_INVALID_SOCKET) return WS_INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

#if defined(IBM) || defined(_WIN32)
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        closesocket(fd);
        return WS_INVALID_SOCKET;
    }
#else
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(fd);
        return WS_INVALID_SOCKET;
    }
#endif

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == WS_SOCKET_ERROR) {
        CloseSocket(fd);
        return WS_INVALID_SOCKET;
    }
    return fd;
}

void WebSocketClient::CloseSocket(SocketFd& fd) {
    if (fd != WS_INVALID_SOCKET) {
        WS_CLOSE_SOCKET(fd);
        fd = WS_INVALID_SOCKET;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket handshake (RFC 6455 §4)
// ─────────────────────────────────────────────────────────────────────────────

// Very small helper to send all bytes in a buffer
static bool SendAll(SocketFd fd, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#if defined(IBM) || defined(_WIN32)
        int n = ::send(fd, buf + sent, static_cast<int>(len - sent), 0);
#else
        ssize_t n = ::send(fd, buf + sent, len - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Receive until we find "\r\n\r\n" (end of HTTP response headers)
static bool RecvHttpHeaders(SocketFd fd, std::string& out) {
    out.clear();
    char ch;
    while (out.size() < 4096) {
#if defined(IBM) || defined(_WIN32)
        int n = ::recv(fd, &ch, 1, 0);
#else
        ssize_t n = ::recv(fd, &ch, 1, 0);
#endif
        if (n <= 0) return false;
        out += ch;
        if (out.size() >= 4 &&
            out[out.size()-4] == '\r' && out[out.size()-3] == '\n' &&
            out[out.size()-2] == '\r' && out[out.size()-1] == '\n') {
            return true;
        }
    }
    return false;
}

bool WebSocketClient::PerformHandshake(SocketFd fd,
                                       const std::string& host,
                                       uint16_t port) {
    // Build the upgrade request with a static (non-cryptographic) key.
    // The server only needs to accept any valid base64 key.
    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";   // RFC 6455 example key

    std::ostringstream req;
    req << "GET / HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";

    const std::string reqStr = req.str();
    if (!SendAll(fd, reqStr.c_str(), reqStr.size())) return false;

    std::string response;
    if (!RecvHttpHeaders(fd, response)) return false;

    // Check for HTTP 101
    return response.find("101") != std::string::npos;
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket text frame (RFC 6455 §5)
// ─────────────────────────────────────────────────────────────────────────────

bool WebSocketClient::SendFrame(SocketFd fd, const std::string& payload) {
    const size_t payloadLen = payload.size();

    // Masking key: 4 random bytes (required for client→server frames)
    static thread_local std::mt19937 rng{
        std::random_device{}() ^
        static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    uint8_t maskKey[4];
    for (int i = 0; i < 4; ++i) maskKey[i] = static_cast<uint8_t>(rng());

    // Build frame header
    std::vector<uint8_t> frame;
    frame.reserve(10 + payloadLen);

    // Byte 0: FIN=1, opcode=0x1 (text)
    frame.push_back(0x81u);

    // Byte 1 (and optional length extension): MASK=1 + payload length
    if (payloadLen <= 125) {
        frame.push_back(static_cast<uint8_t>(0x80u | payloadLen));
    } else if (payloadLen <= 65535) {
        frame.push_back(0xFEu);   // 0x80 | 126
        frame.push_back(static_cast<uint8_t>((payloadLen >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>( payloadLen       & 0xFF));
    } else {
        frame.push_back(0xFFu);   // 0x80 | 127
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((payloadLen >> shift) & 0xFF));
        }
    }

    // 4-byte masking key
    for (int i = 0; i < 4; ++i) frame.push_back(maskKey[i]);

    // Masked payload
    for (size_t i = 0; i < payloadLen; ++i) {
        frame.push_back(static_cast<uint8_t>(
            static_cast<unsigned char>(payload[i]) ^ maskKey[i % 4]));
    }

    return SendAll(fd, reinterpret_cast<const char*>(frame.data()), frame.size());
}
