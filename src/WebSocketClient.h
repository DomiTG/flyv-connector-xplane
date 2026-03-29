#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

// ── Platform socket type ──────────────────────────────────────────────────────
#if defined(IBM) || defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using SocketFd = SOCKET;
#  define WS_INVALID_SOCKET  INVALID_SOCKET
#  define WS_SOCKET_ERROR    SOCKET_ERROR
#  define WS_CLOSE_SOCKET(s) closesocket(s)
#  define WS_WOULDBLOCK      WSAEWOULDBLOCK
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <cerrno>
   using SocketFd = int;
#  define WS_INVALID_SOCKET  (-1)
#  define WS_SOCKET_ERROR    (-1)
#  define WS_CLOSE_SOCKET(s) ::close(s)
#  define WS_WOULDBLOCK      EWOULDBLOCK
#endif

/**
 * WebSocketClient
 *
 * A minimal, cross-platform (Windows / macOS) WebSocket TEXT-frame sender.
 *
 * Architecture
 * ────────────
 *  • A dedicated background thread owns the TCP socket and the RFC-6455
 *    handshake/framing logic so the X-Plane flight loop is never blocked.
 *  • Send() enqueues a string; the background thread drains the queue.
 *  • On any send/receive error the connection is closed and the thread
 *    automatically retries after kReconnectDelay seconds.
 *
 * Usage
 * ─────
 *  WebSocketClient client("localhost", 8487);
 *  client.Start();                // spawns background thread
 *  client.Send("{\"hello\":1}");  // non-blocking
 *  client.Stop();                 // graceful shutdown
 */
class WebSocketClient {
public:
    explicit WebSocketClient(const std::string& host, uint16_t port,
                             unsigned reconnectDelaySec = 3)
        : host_(host)
        , port_(port)
        , reconnectDelaySec_(reconnectDelaySec)
    {}

    ~WebSocketClient() { Stop(); }

    /** Spawn background IO thread. May be called only once. */
    void Start();

    /** Signal shutdown and join the background thread (blocks briefly). */
    void Stop();

    /**
     * Enqueue a UTF-8 text payload.  Returns immediately; the background
     * thread will send it as soon as the connection is established.
     * The queue is bounded to kMaxQueueDepth items; oldest items are dropped
     * when the connection is down for a long time to avoid unbounded growth.
     */
    void Send(std::string payload);

    bool IsConnected() const { return connected_.load(std::memory_order_relaxed); }

private:
    // ── Configuration ─────────────────────────────────────────────────────
    static constexpr size_t kMaxQueueDepth = 512;

    // ── State ─────────────────────────────────────────────────────────────
    std::string            host_;
    uint16_t               port_;
    unsigned               reconnectDelaySec_;
    std::atomic<bool>      running_{false};
    std::atomic<bool>      connected_{false};
    std::thread            thread_;

    std::mutex             queueMutex_;
    std::queue<std::string> sendQueue_;

    // ── Background thread entry ────────────────────────────────────────────
    void ThreadFunc();

    // ── Socket helpers ────────────────────────────────────────────────────
    static SocketFd ConnectSocket(const std::string& host, uint16_t port);
    static void     CloseSocket(SocketFd& fd);

    // ── WebSocket handshake ────────────────────────────────────────────────
    static bool PerformHandshake(SocketFd fd,
                                 const std::string& host, uint16_t port);

    // ── WebSocket framing (RFC 6455 – client-to-server, masked text) ───────
    static bool SendFrame(SocketFd fd, const std::string& payload);

    // ── Platform helpers ──────────────────────────────────────────────────
    static void SleepMs(unsigned ms);
    static int  LastError();
};
