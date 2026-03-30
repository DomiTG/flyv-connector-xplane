/**
 * FlyV X-Plane Connector
 *
 * X-Plane plugin that connects to a local WebSocket server at
 * ws://localhost:8487 and handles typed JSON message requests:
 *
 *   Client → Plugin                  Plugin → Client
 *   ─────────────────────────────────────────────────────
 *   { "type": "AircraftData" }  →  { "type": "AircraftData", "data": { "Aircraft": {...} } }
 *   { "type": "Status" }        →  { "type": "Status", "data": { "simulator_loaded": <bool>,
 *                                     "simulator_connected": <bool>, "simulator_name": "<str>",
 *                                     "last_error": "<str>" } }
 *   { "type": "ping" }          →  { "type": "pong", "data": {} }
 *
 * On WebSocket connection open the plugin immediately pushes an unsolicited
 * greeting:  { "data": { "code": "600"/"404", "message": "<simName>"/""} }
 *
 * Build requirements
 * ──────────────────
 *  • X-Plane Plugin SDK  (set XPLANE_SDK_PATH in CMake)
 *  • CMake ≥ 3.16, a C++17 compiler
 *  • Windows: MSVC or MinGW-w64, ws2_32.lib
 *  • macOS:   Apple Clang, Xcode Command Line Tools
 *
 * Entry points (required by X-Plane)
 * ───────────────────────────────────
 *  XPluginStart / XPluginStop / XPluginEnable / XPluginDisable
 *  XPluginReceiveMessage
 */

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

// X-Plane SDK headers (resolved via XPLANE_SDK_PATH at build time)
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "DataCollector.h"
#include "JsonSerializer.h"
#include "WebSocketClient.h"

// ── Plugin metadata ───────────────────────────────────────────────────────────
static constexpr char kPluginName[] = "FlyV X-Plane Connector";
static constexpr char kPluginSig[]  = "com.flyv.connector.xplane";
static constexpr char kPluginDesc[] =
    "Streams X-Plane simulator data to FlyV via WebSocket (ws://localhost:8487)";

static constexpr char     kWsHost[] = "127.0.0.1";
static constexpr uint16_t kWsPort   = 8487;

// ── Module-level singletons ───────────────────────────────────────────────────
static std::unique_ptr<DataCollector>   g_collector;
static std::unique_ptr<WebSocketClient> g_wsClient;

// ── Simulator state (written from the X-Plane main thread) ───────────────────
static std::atomic<bool> g_simLoaded{false};  // true once a flight is loaded
static std::string       g_lastError;         // last simulator error (empty if none)

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Extract the string value of a JSON key from a flat single-level object. */
static std::string JsonGetString(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":\"";
    const auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    const auto start = pos + search.size();
    const auto end   = json.find('"', start);
    if (end == std::string::npos) return {};
    return json.substr(start, end - start);
}

/** Return the X-Plane simulator name derived from the running version. */
static std::string GetSimulatorName() {
    int xpVer = 0, xplmVer = 0;
    XPLMHostApplicationID hostID;
    XPLMGetVersions(&xpVer, &xplmVer, &hostID);
    const int major = xpVer / 10000;
    return "X-Plane " + std::to_string(major);
}

// ─────────────────────────────────────────────────────────────────────────────
// XPluginStart
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    strncpy(outName, kPluginName, 255); outName[255] = '\0';
    strncpy(outSig,  kPluginSig,  255); outSig [255] = '\0';
    strncpy(outDesc, kPluginDesc, 255); outDesc[255] = '\0';

    XPLMDebugString("[FlyV] XPluginStart\n");

    g_collector = std::make_unique<DataCollector>();
    g_wsClient  = std::make_unique<WebSocketClient>(kWsHost, kWsPort);

    // Handle typed incoming requests and return typed responses.
    g_wsClient->SetMessageCallback([](const std::string& incoming) -> std::string {
        const std::string type = JsonGetString(incoming, "type");

        if (type == "AircraftData") {
            // Collect current simulator state and wrap in the typed envelope
            if (!g_collector) return JsonSerializer::SerializeErrorEnvelope("Collector unavailable");
            const SimData data = g_collector->Collect();
            return JsonSerializer::SerializeAircraftEnvelope(data);
        }

        if (type == "Status") {
            const bool loaded    = g_simLoaded.load(std::memory_order_relaxed);
            const bool connected = loaded;
            return JsonSerializer::SerializeStatusEnvelope(
                loaded, connected, GetSimulatorName(), g_lastError);
        }

        if (type == "ping") {
            return JsonSerializer::SerializePongEnvelope();
        }

        if (type == "Error" || type == "error") {
            // Log server-side errors; no reply needed
            XPLMDebugString(("[FlyV] Error from server: " + incoming + "\n").c_str());
            return {};
        }

        // Unknown type – return an error envelope
        return JsonSerializer::SerializeErrorEnvelope("Unknown message type: " + type);
    });

    // Push an unsolicited greeting immediately when the WebSocket opens.
    g_wsClient->SetConnectCallback([]() {
        const bool connected = g_simLoaded.load(std::memory_order_relaxed);
        const std::string simName = connected ? GetSimulatorName() : "";
        g_wsClient->QueueMessage(
            JsonSerializer::SerializeConnectMessage(connected, simName));
    });

    g_wsClient->Start();

    XPLMDebugString("[FlyV] Plugin started – listening for requests on ws://127.0.0.1:8487\n");
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// XPluginStop
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API void XPluginStop() {
    XPLMDebugString("[FlyV] XPluginStop\n");

    if (g_wsClient) {
        g_wsClient->Stop();
        g_wsClient.reset();
    }
    g_collector.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// XPluginEnable / XPluginDisable
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API int  XPluginEnable()  { return 1; }
PLUGIN_API void XPluginDisable() {}

// ─────────────────────────────────────────────────────────────────────────────
// XPluginReceiveMessage
// Update simulator state when a flight is loaded or unloaded.
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*from*/,
                                      int          msg,
                                      void*        /*param*/) {
    switch (msg) {
        case XPLM_MSG_PLANE_LOADED:
            // User's aircraft (and its flight) finished loading
            g_simLoaded.store(true, std::memory_order_relaxed);
            break;

        case XPLM_MSG_PLANE_UNLOADED:
            // Flight/aircraft is being torn down
            g_simLoaded.store(false, std::memory_order_relaxed);
            break;

        default:
            break;
    }
}

