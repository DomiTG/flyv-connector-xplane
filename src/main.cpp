/**
 * FlyV X-Plane Connector
 *
 * X-Plane plugin that streams simulator data to a local WebSocket server at
 * ws://localhost:8487 every 100 milliseconds.
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

#include <cstring>
#include <memory>

// X-Plane SDK headers (resolved via XPLANE_SDK_PATH at build time)
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include "DataCollector.h"
#include "JsonSerializer.h"
#include "WebSocketClient.h"

// ── Plugin metadata ───────────────────────────────────────────────────────────
static constexpr char kPluginName[] = "FlyV X-Plane Connector";
static constexpr char kPluginSig[]  = "com.flyv.connector.xplane";
static constexpr char kPluginDesc[] =
    "Streams X-Plane simulator data to FlyV via WebSocket (ws://localhost:8487)";

static constexpr char   kWsHost[]         = "127.0.0.1";
static constexpr uint16_t kWsPort         = 8487;
static constexpr float  kFlightLoopInterval = 0.1f;   // 100 ms

// ── Module-level singletons ───────────────────────────────────────────────────
static std::unique_ptr<DataCollector>  g_collector;
static std::unique_ptr<WebSocketClient> g_wsClient;

// ─────────────────────────────────────────────────────────────────────────────
// Flight-loop callback – called by X-Plane at ~100 ms intervals
// ─────────────────────────────────────────────────────────────────────────────
static float FlightLoopCallback(float /*elapsedSinceLastCall*/,
                                float /*elapsedSinceLastFlightLoop*/,
                                int   /*counter*/,
                                void* /*refCon*/)
{
    if (g_collector && g_wsClient) {
        SimData data = g_collector->Collect();
        std::string json = JsonSerializer::Serialize(data);
        g_wsClient->Send(std::move(json));
    }
    return kFlightLoopInterval;
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
    g_wsClient->Start();

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, kFlightLoopInterval, nullptr);

    XPLMDebugString("[FlyV] Plugin started – streaming to ws://127.0.0.1:8487\n");
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// XPluginStop
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API void XPluginStop() {
    XPLMDebugString("[FlyV] XPluginStop\n");

    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);

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
// XPluginReceiveMessage – not used, but required by the SDK
// ─────────────────────────────────────────────────────────────────────────────
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID /*from*/,
                                      int          /*msg*/,
                                      void*        /*param*/) {}
