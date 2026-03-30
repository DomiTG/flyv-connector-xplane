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

void WebSocketClient::SetMessageCallback(MessageCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    messageCallback_ = std::move(cb);
}

void WebSocketClient::SetConnectCallback(ConnectCallback cb) {
    std::lock_guard<std::mutex> lock(connectCallbackMutex_);
    connectCallback_ = std::move(cb);
}

void WebSocketClient::QueueMessage(const std::string& payload) {
    std::lock_guard<std::mutex> lock(sendQueueMutex_);
    sendQueue_.push(payload);
}

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

// ─────────────────────────────────────────────────────────────────────────────
// Background thread – request/response loop
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

        // Invoke the connect callback (it may queue an unsolicited greeting)
        {
            ConnectCallback cb;
            {
                std::lock_guard<std::mutex> lock(connectCallbackMutex_);
                cb = connectCallback_;
            }
            if (cb) cb();
        }

        // Flush any messages queued by the connect callback before entering
        // the regular poll loop so they are sent immediately.
        {
            std::lock_guard<std::mutex> lk(sendQueueMutex_);
            while (!sendQueue_.empty()) {
                if (!SendFrame(fd, sendQueue_.front())) {
                    while (!sendQueue_.empty()) sendQueue_.pop();
                    goto reconnect;
                }
                sendQueue_.pop();
            }
        }

        // ── Request/response loop ─────────────────────────────────────────
        // Wait for an incoming frame from the server.  On receipt of any data
        // frame the registered callback is called and the result is sent back.
        // select() with a short timeout lets us check running_ regularly.
        while (running_.load(std::memory_order_relaxed)) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(fd, &readSet);

            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 50000;   // 50 ms polling interval

#if defined(IBM) || defined(_WIN32)
            int sel = ::select(0, &readSet, nullptr, nullptr, &tv);
#else
            int sel = ::select(static_cast<int>(fd) + 1,
                               &readSet, nullptr, nullptr, &tv);
#endif
            if (sel < 0) {
                goto reconnect;
            }

            if (sel > 0 && FD_ISSET(fd, &readSet)) {
                std::string incoming;
                int result = RecvFrame(fd, incoming);
                if (result < 0) {
                    goto reconnect;
                }

                if (result > 0) {
                    // Data frame received — invoke callback with incoming payload
                    MessageCallback cb;
                    {
                        std::lock_guard<std::mutex> lock(callbackMutex_);
                        cb = messageCallback_;
                    }
                    if (cb) {
                        std::string response = cb(incoming);
                        if (!response.empty()) {
                            if (!SendFrame(fd, response)) {
                                goto reconnect;
                            }
                        }
                    }
                }
            }

            // Drain any unsolicited outbound messages queued from other threads
            {
                std::lock_guard<std::mutex> lk(sendQueueMutex_);
                while (!sendQueue_.empty()) {
                    if (!SendFrame(fd, sendQueue_.front())) {
                        // Clear the queue and reconnect on send error
                        while (!sendQueue_.empty()) sendQueue_.pop();
                        goto reconnect;
                    }
                    sendQueue_.pop();
                }
            }
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
// WebSocket receive helpers (RFC 6455 §5)
// ─────────────────────────────────────────────────────────────────────────────

bool WebSocketClient::RecvAll(SocketFd fd, void* buf, size_t len) {
    size_t received = 0;
    auto* ptr = static_cast<char*>(buf);
    while (received < len) {
#if defined(IBM) || defined(_WIN32)
        int n = ::recv(fd, ptr + received, static_cast<int>(len - received), 0);
#else
        ssize_t n = ::recv(fd, ptr + received, len - received, 0);
#endif
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

int WebSocketClient::RecvFrame(SocketFd fd, std::string& payload) {
    // Read 2-byte frame header
    uint8_t header[2];
    if (!RecvAll(fd, header, 2)) return -1;

    const uint8_t opcode = header[0] & 0x0Fu;
    const bool    masked = (header[1] & 0x80u) != 0u;
    uint64_t      length = header[1] & 0x7Fu;

    if (length == 126) {
        uint8_t ext[2];
        if (!RecvAll(fd, ext, 2)) return -1;
        length = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (length == 127) {
        uint8_t ext[8];
        if (!RecvAll(fd, ext, 8)) return -1;
        length = 0;
        for (int i = 0; i < 8; ++i)
            length = (length << 8) | ext[i];
    }

    // Guard against unreasonably large frames
    if (length > 1024u * 1024u) return -1;

    uint8_t maskKey[4] = {};
    if (masked) {
        if (!RecvAll(fd, maskKey, 4)) return -1;
    }

    std::vector<uint8_t> data(static_cast<size_t>(length));
    if (length > 0 && !RecvAll(fd, data.data(), static_cast<size_t>(length)))
        return -1;

    if (masked) {
        for (size_t i = 0; i < static_cast<size_t>(length); ++i)
            data[i] ^= maskKey[i % 4];
    }

    switch (opcode) {
        case 0x0:   // continuation
        case 0x1:   // text
        case 0x2:   // binary
            payload = std::string(data.begin(), data.end());
            return 1;

        case 0x8:   // close
            return -1;

        case 0x9: { // ping → respond with pong
            const std::string pingPayload(data.begin(), data.end());
            SendControlFrame(fd, 0xAu, pingPayload);
            return 0;
        }

        case 0xA:   // pong → ignore
            return 0;

        default:
            return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket send helpers (RFC 6455 §5)
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<uint8_t> BuildMaskedFrame(uint8_t opcode,
                                             const std::string& payload) {
    const size_t payloadLen = payload.size();

    static thread_local std::mt19937 rng{
        std::random_device{}() ^
        static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())};

    uint8_t maskKey[4];
    for (int i = 0; i < 4; ++i) maskKey[i] = static_cast<uint8_t>(rng());

    std::vector<uint8_t> frame;
    frame.reserve(10 + payloadLen);

    // FIN=1, opcode
    frame.push_back(static_cast<uint8_t>(0x80u | (opcode & 0x0Fu)));

    // MASK=1 + length
    if (payloadLen <= 125) {
        frame.push_back(static_cast<uint8_t>(0x80u | payloadLen));
    } else if (payloadLen <= 65535) {
        frame.push_back(0xFEu);   // 0x80 | 126
        frame.push_back(static_cast<uint8_t>((payloadLen >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>( payloadLen       & 0xFF));
    } else {
        frame.push_back(0xFFu);   // 0x80 | 127
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<uint8_t>((payloadLen >> shift) & 0xFF));
    }

    for (int i = 0; i < 4; ++i) frame.push_back(maskKey[i]);

    for (size_t i = 0; i < payloadLen; ++i) {
        frame.push_back(static_cast<uint8_t>(
            static_cast<unsigned char>(payload[i]) ^ maskKey[i % 4]));
    }

    return frame;
}

bool WebSocketClient::SendFrame(SocketFd fd, const std::string& payload) {
    auto frame = BuildMaskedFrame(0x1u, payload);   // opcode 0x1 = text
    return SendAll(fd, reinterpret_cast<const char*>(frame.data()), frame.size());
}

bool WebSocketClient::SendControlFrame(SocketFd fd, uint8_t opcode,
                                       const std::string& payload) {
    auto frame = BuildMaskedFrame(opcode, payload);
    return SendAll(fd, reinterpret_cast<const char*>(frame.data()), frame.size());
}

