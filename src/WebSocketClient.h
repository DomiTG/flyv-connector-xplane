#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>

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
 * A minimal, cross-platform (Windows / macOS) WebSocket client that operates
 * in request-response mode: it waits for an incoming message from the server
 * and responds immediately with simulator data via a registered callback.
 *
 * Architecture
 * ────────────
 *  • A dedicated background thread owns the TCP socket and the RFC-6455
 *    handshake/framing logic so the X-Plane flight loop is never blocked.
 *  • The thread uses select() to wait for incoming frames with a short
 *    timeout so it can check the running_ flag regularly.
 *  • When any data frame arrives the registered MessageCallback is invoked
 *    with the raw incoming payload; if the callback returns a non-empty
 *    string it is sent back as a masked text frame.
 *  • Unsolicited outbound messages can be queued via QueueMessage() from
 *    any thread; they are drained by the background thread on each poll
 *    cycle so the socket is always written from a single thread.
 *  • Ping frames are answered with a Pong automatically.
 *  • On any error the connection is closed and the thread retries after
 *    reconnectDelaySec seconds.
 *
 * Usage
 * ─────
 *  WebSocketClient client("localhost", 8487);
 *  client.SetMessageCallback([](const std::string& msg) { return reply(msg); });
 *  client.Start();   // spawns background thread
 *  client.Stop();    // graceful shutdown
 */
class WebSocketClient {
public:
    /**
     * Callback invoked for each incoming data frame.
     * Receives the raw UTF-8 payload; returns a UTF-8 response string to
     * send back, or an empty string to send no response.
     */
    using MessageCallback = std::function<std::string(const std::string&)>;

    explicit WebSocketClient(const std::string& host, uint16_t port,
                             unsigned reconnectDelaySec = 3)
        : host_(host)
        , port_(port)
        , reconnectDelaySec_(reconnectDelaySec)
    {}

    ~WebSocketClient() { Stop(); }

    /** Register the callback that produces response data for each incoming message. */
    void SetMessageCallback(MessageCallback cb);

    /**
     * Queue an unsolicited outbound message.  Thread-safe; may be called from
     * any thread (including the X-Plane flight loop).  The message is sent by
     * the background IO thread on its next poll cycle.
     */
    void QueueMessage(const std::string& payload);

    /** Spawn background IO thread. May be called only once. */
    void Start();

    /** Signal shutdown and join the background thread (blocks briefly). */
    void Stop();

    bool IsConnected() const { return connected_.load(std::memory_order_relaxed); }

private:
    // ── State ─────────────────────────────────────────────────────────────
    std::string            host_;
    uint16_t               port_;
    unsigned               reconnectDelaySec_;
    std::atomic<bool>      running_{false};
    std::atomic<bool>      connected_{false};
    std::thread            thread_;

    MessageCallback        messageCallback_;
    std::mutex             callbackMutex_;

    std::queue<std::string> sendQueue_;
    std::mutex              sendQueueMutex_;

    // ── Background thread entry ────────────────────────────────────────────
    void ThreadFunc();

    // ── Socket helpers ────────────────────────────────────────────────────
    static SocketFd ConnectSocket(const std::string& host, uint16_t port);
    static void     CloseSocket(SocketFd& fd);

    // ── WebSocket handshake ────────────────────────────────────────────────
    static bool PerformHandshake(SocketFd fd,
                                 const std::string& host, uint16_t port);

    // ── WebSocket framing (RFC 6455) ───────────────────────────────────────
    /** Send a masked client-to-server text frame. */
    static bool SendFrame(SocketFd fd, const std::string& payload);

    /** Send a masked client-to-server control frame (e.g. pong, opcode 0xA). */
    static bool SendControlFrame(SocketFd fd, uint8_t opcode,
                                 const std::string& payload);

    /**
     * Receive one WebSocket frame from the server (unmasked, server-to-client).
     * Returns: 1 = data frame (payload filled), 0 = control frame handled
     *          (ping answered, pong/close ignored), -1 = error / connection closed.
     */
    static int  RecvFrame(SocketFd fd, std::string& payload);

    /** Blocking helper: receive exactly len bytes. */
    static bool RecvAll(SocketFd fd, void* buf, size_t len);

    // ── Platform helpers ──────────────────────────────────────────────────
    static void SleepMs(unsigned ms);
    static int  LastError();
};
