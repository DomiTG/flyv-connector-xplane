# flyv-connector-xplane

Cross-platform X-Plane plugin that streams simulator data to the **FlyV** application via a local WebSocket connection.

Every **100 milliseconds** the plugin reads simulator DataRefs and sends a JSON snapshot to `ws://localhost:8487`.

---

## Data fields

| Category | Fields |
|---|---|
| Position / flight | `PLANE_ALTITUDE`, `PLANE_LATITUDE`, `PLANE_LONGITUDE`, `PLANE_ALT_ABOVE_GROUND`, `AIRSPEED_INDICATED`, `AIRSPEED_TRUE`, `VERTICAL_SPEED`, `PLANE_HEADING_DEGREES_TRUE`, `PLANE_PITCH_DEGREES`, `PLANE_BANK_DEGREES`, `GROUND_VELOCITY` |
| Environment | `AMBIENT_WIND_DIRECTION`, `AMBIENT_WIND_VELOCITY`, `AMBIENT_TEMPERATURE`, `AMBIENT_PRESSURE` |
| Engines N1/N2 | `ENG_N1_RPM_1..4`, `ENG_N2_RPM_1..4` |
| Misc float64 | `NUMBER_OF_ENGINES`, `G_FORCE`, `TRAILING_EDGE_FLAPS_LEFT_ANGLE`, `TRAILING_EDGE_FLAPS_RIGHT_ANGLE`, `GEAR_CENTER_POSITION`, `GEAR_LEFT_POSITION`, `GEAR_RIGHT_POSITION`, `SPOILERS_HANDLE_POSITION` |
| Misc int32 | `SIM_ON_GROUND`, `LIGHT_NAV_ON`, `LIGHT_BEACON_ON`, `LIGHT_STROBE_ON`, `LIGHT_TAXI_ON`, `LIGHT_LANDING_ON`, `FLAPS_HANDLE_INDEX`, `GEAR_HANDLE_POSITION` |
| Combustion | `GENERAL_ENG_COMBUSTION_1..4` |
| Strings | `TITLE`, `ATC_MODEL` |

---

## Project layout

```
flyv-connector-xplane/
├── CMakeLists.txt
├── README.md
└── src/
    ├── main.cpp            – X-Plane plugin entry points + flight-loop callback
    ├── DataCollector.h     – Wraps XPLM DataRef accessors for all fields
    ├── SimData.h           – Plain struct holding one data snapshot
    ├── JsonSerializer.h    – Converts SimData → compact JSON (no external deps)
    ├── WebSocketClient.h   – Cross-platform WebSocket client interface
    └── WebSocketClient.cpp – RFC 6455 handshake + framing; background thread
```

---

## Requirements

| Tool | Version |
|---|---|
| CMake | ≥ 3.16 |
| C++ compiler | C++17 (MSVC 2019+, Apple Clang 12+) |
| X-Plane Plugin SDK | 3.0+ (XPLM 300/301) |

Download the **X-Plane Plugin SDK** from  
<https://developer.x-plane.com/sdk/plugin-sdk/>

---

## Building

### macOS

```bash
# 1. Clone / enter the repo
git clone https://github.com/DomiTG/flyv-connector-xplane.git
cd flyv-connector-xplane

# 2. Configure (replace /path/to/SDK with your SDK root)
cmake -B build \
      -DXPLANE_SDK_PATH=/path/to/SDK \
      -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --config Release

# 4. The plugin is at build/FlyV-Connector-XPlane.xpl
```

### Windows (Visual Studio 2019+)

```powershell
# From a "Developer Command Prompt for VS 2019" (64-bit)
cmake -B build `
      -DXPLANE_SDK_PATH="C:\path\to\SDK" `
      -A x64
cmake --build build --config Release
# Output: build\Release\FlyV-Connector-XPlane.xpl
```

---

## Installation

1. Build the plugin for your platform (see above).
2. Create a folder inside the X-Plane `Resources/plugins/` directory:
   ```
   X-Plane 12/Resources/plugins/FlyV-Connector/
   ```
3. Copy the `.xpl` file into that folder.
4. Start X-Plane — the plugin will auto-load.
5. Make sure the FlyV application is running and listening on port **8487** before or after starting X-Plane; the plugin retries the connection automatically.

---

## Architecture

```
X-Plane flight loop (main thread, every 100 ms)
        │
        ▼  DataCollector::Collect() – reads all DataRefs
        │
        ▼  JsonSerializer::Serialize() – builds JSON string
        │
        ▼  WebSocketClient::Send() – enqueues (non-blocking)
                │
                └─── Background IO thread ──► ws://localhost:8487
                         • TCP connect + HTTP Upgrade handshake
                         • RFC 6455 text frames (masked, as required)
                         • Auto-reconnect on connection loss
```

The flight loop callback is **never blocked** by network I/O.  A dedicated background thread owns the socket, performs the WebSocket handshake, and drains the send queue.  If the connection drops, the thread reconnects automatically (default: 3-second retry delay).
