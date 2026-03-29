#pragma once
#include <cstring>
#include <string>
#include <cmath>

// X-Plane Plugin SDK headers (must be available at build time via XPLANE_SDK_PATH)
#include "XPLMDataAccess.h"

#include "SimData.h"

/**
 * DataCollector
 *
 * Caches XPLMDataRef handles (looked up once on construction) and exposes a
 * single Collect() method that reads every required simulator variable and
 * returns a populated SimData snapshot.
 *
 * All DataRef names follow the X-Plane 11/12 standard data reference tree.
 */
class DataCollector {
public:
    DataCollector() {
        // ── Position / flight dynamics ─────────────────────────────────────
        drAltitude         = XPLMFindDataRef("sim/flightmodel/position/elevation");
        drLatitude         = XPLMFindDataRef("sim/flightmodel/position/latitude");
        drLongitude        = XPLMFindDataRef("sim/flightmodel/position/longitude");
        drAltAGL           = XPLMFindDataRef("sim/flightmodel/position/y_agl");
        drAirspeedInd      = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
        drAirspeedTrue     = XPLMFindDataRef("sim/flightmodel/position/true_airspeed2");
        drVerticalSpeed    = XPLMFindDataRef("sim/flightmodel/position/vh_ind_fpm");
        drHeadingTrue      = XPLMFindDataRef("sim/flightmodel/position/psi");
        drPitch            = XPLMFindDataRef("sim/flightmodel/position/theta");
        drBank             = XPLMFindDataRef("sim/flightmodel/position/phi");
        drGroundSpeed      = XPLMFindDataRef("sim/flightmodel/position/groundspeed");

        // ── Environment ───────────────────────────────────────────────────
        drWindDir          = XPLMFindDataRef("sim/weather/wind_direction_degt");
        drWindSpeed        = XPLMFindDataRef("sim/weather/wind_speed_kt");
        drTemperature      = XPLMFindDataRef("sim/weather/temperature_ambient_c");
        drPressure         = XPLMFindDataRef("sim/weather/barometer_sealevel_inhg");

        // ── Engines ───────────────────────────────────────────────────────
        drEngN1            = XPLMFindDataRef("sim/flightmodel/engine/ENGN_N1_");
        drEngN2            = XPLMFindDataRef("sim/flightmodel/engine/ENGN_N2_");

        // ── Misc float/double ─────────────────────────────────────────────
        drNumEngines       = XPLMFindDataRef("sim/aircraft/engine/acf_num_engines");
        drGForce           = XPLMFindDataRef("sim/flightmodel/misc/Gforce");
        drFlapsLeft        = XPLMFindDataRef("sim/flightmodel/controls/lft_flaprat");
        drFlapsRight       = XPLMFindDataRef("sim/flightmodel/controls/rgt_flaprat");
        drGearDeploy       = XPLMFindDataRef("sim/flightmodel2/gear/deploy_ratio");
        drSpoilers         = XPLMFindDataRef("sim/flightmodel/controls/speedbrake_ratio");

        // ── Int fields ────────────────────────────────────────────────────
        drOnGround         = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
        drLightNav         = XPLMFindDataRef("sim/cockpit/electrical/nav_lights_on");
        drLightBeacon      = XPLMFindDataRef("sim/cockpit/electrical/beacon_lights_on");
        drLightStrobe      = XPLMFindDataRef("sim/cockpit/electrical/strobe_lights_on");
        drLightTaxi        = XPLMFindDataRef("sim/cockpit/electrical/taxi_light_on");
        drLightLanding     = XPLMFindDataRef("sim/cockpit/electrical/landing_lights_on");
        drFlapsHandle      = XPLMFindDataRef("sim/cockpit2/controls/flap_handle_deploy_ratio");
        drGearHandle       = XPLMFindDataRef("sim/cockpit2/controls/gear_handle_down");
        drEngRunning       = XPLMFindDataRef("sim/flightmodel/engine/ENGN_running");

        // ── Strings ───────────────────────────────────────────────────────
        drTitle            = XPLMFindDataRef("sim/aircraft/view/acf_descrip");
        drAtcModel         = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
    }

    SimData Collect() const {
        SimData d{};

        // ── Position / flight dynamics ────────────────────────────────────
        d.PLANE_ALTITUDE              = SafeGetDouble(drAltitude);
        d.PLANE_LATITUDE              = SafeGetDouble(drLatitude);
        d.PLANE_LONGITUDE             = SafeGetDouble(drLongitude);
        d.PLANE_ALT_ABOVE_GROUND      = SafeGetFloat(drAltAGL);
        d.AIRSPEED_INDICATED          = SafeGetFloat(drAirspeedInd);

        // true_airspeed2 is in m/s — convert to knots
        d.AIRSPEED_TRUE               = SafeGetFloat(drAirspeedTrue) * kMsToKnots;

        d.VERTICAL_SPEED              = std::roundf(SafeGetFloat(drVerticalSpeed));
        d.PLANE_HEADING_DEGREES_TRUE  = SafeGetFloat(drHeadingTrue);
        d.PLANE_PITCH_DEGREES         = SafeGetFloat(drPitch);
        d.PLANE_BANK_DEGREES          = SafeGetFloat(drBank);

        // groundspeed is in m/s — convert to knots
        d.GROUND_VELOCITY             = std::roundf(SafeGetFloat(drGroundSpeed) * kMsToKnots);

        // ── Environment ───────────────────────────────────────────────────
        // wind DataRefs are float arrays; index 0 is the surface wind layer
        float windArr[3] = {0};
        SafeGetFloatArray(drWindDir,   windArr, 3);
        d.AMBIENT_WIND_DIRECTION = windArr[0];

        SafeGetFloatArray(drWindSpeed, windArr, 3);
        d.AMBIENT_WIND_VELOCITY  = windArr[0];

        d.AMBIENT_TEMPERATURE    = SafeGetFloat(drTemperature);
        d.AMBIENT_PRESSURE       = SafeGetFloat(drPressure);

        // ── Engine N1 / N2 ────────────────────────────────────────────────
        float n1[8] = {0};
        float n2[8] = {0};
        SafeGetFloatArray(drEngN1, n1, 8);
        SafeGetFloatArray(drEngN2, n2, 8);

        d.ENG_N1_RPM_1 = n1[0];  d.ENG_N1_RPM_2 = n1[1];
        d.ENG_N1_RPM_3 = n1[2];  d.ENG_N1_RPM_4 = n1[3];
        d.ENG_N2_RPM_1 = n2[0];  d.ENG_N2_RPM_2 = n2[1];
        d.ENG_N2_RPM_3 = n2[2];  d.ENG_N2_RPM_4 = n2[3];

        // ── Misc float64 ──────────────────────────────────────────────────
        d.NUMBER_OF_ENGINES              = static_cast<double>(SafeGetInt(drNumEngines));
        d.G_FORCE                        = static_cast<double>(SafeGetFloat(drGForce));
        d.TRAILING_EDGE_FLAPS_LEFT_ANGLE = static_cast<double>(SafeGetFloat(drFlapsLeft)  * kFlapToDegrees);
        d.TRAILING_EDGE_FLAPS_RIGHT_ANGLE= static_cast<double>(SafeGetFloat(drFlapsRight) * kFlapToDegrees);

        float gearArr[10] = {0};
        SafeGetFloatArray(drGearDeploy, gearArr, 10);
        d.GEAR_CENTER_POSITION  = static_cast<double>(gearArr[0]);
        d.GEAR_LEFT_POSITION    = static_cast<double>(gearArr[1]);
        d.GEAR_RIGHT_POSITION   = static_cast<double>(gearArr[2]);

        d.SPOILERS_HANDLE_POSITION = static_cast<double>(SafeGetFloat(drSpoilers));

        // ── Int fields ────────────────────────────────────────────────────
        d.SIM_ON_GROUND      = SafeGetInt(drOnGround);
        d.LIGHT_NAV_ON       = SafeGetInt(drLightNav);
        d.LIGHT_BEACON_ON    = SafeGetInt(drLightBeacon);
        d.LIGHT_STROBE_ON    = SafeGetInt(drLightStrobe);
        d.LIGHT_TAXI_ON      = SafeGetInt(drLightTaxi);
        d.LIGHT_LANDING_ON   = SafeGetInt(drLightLanding);

        // Flap handle: 0.0–1.0 ratio → 0–10 integer detent index
        float flapsRatio = SafeGetFloat(drFlapsHandle);
        d.FLAPS_HANDLE_INDEX = static_cast<int>(std::roundf(flapsRatio * 10.0f));

        d.GEAR_HANDLE_POSITION = SafeGetInt(drGearHandle);

        int running[8] = {0};
        SafeGetIntArray(drEngRunning, running, 8);
        d.GENERAL_ENG_COMBUSTION_1 = running[0];
        d.GENERAL_ENG_COMBUSTION_2 = running[1];
        d.GENERAL_ENG_COMBUSTION_3 = running[2];
        d.GENERAL_ENG_COMBUSTION_4 = running[3];

        // ── Strings ───────────────────────────────────────────────────────
        d.TITLE    = ReadByteString(drTitle,    256);
        d.ATC_MODEL= ReadByteString(drAtcModel, 40);

        return d;
    }

private:
    // ── DataRef handles ───────────────────────────────────────────────────
    XPLMDataRef drAltitude      = nullptr;
    XPLMDataRef drLatitude      = nullptr;
    XPLMDataRef drLongitude     = nullptr;
    XPLMDataRef drAltAGL        = nullptr;
    XPLMDataRef drAirspeedInd   = nullptr;
    XPLMDataRef drAirspeedTrue  = nullptr;
    XPLMDataRef drVerticalSpeed = nullptr;
    XPLMDataRef drHeadingTrue   = nullptr;
    XPLMDataRef drPitch         = nullptr;
    XPLMDataRef drBank          = nullptr;
    XPLMDataRef drGroundSpeed   = nullptr;

    XPLMDataRef drWindDir       = nullptr;
    XPLMDataRef drWindSpeed     = nullptr;
    XPLMDataRef drTemperature   = nullptr;
    XPLMDataRef drPressure      = nullptr;

    XPLMDataRef drEngN1         = nullptr;
    XPLMDataRef drEngN2         = nullptr;

    XPLMDataRef drNumEngines    = nullptr;
    XPLMDataRef drGForce        = nullptr;
    XPLMDataRef drFlapsLeft     = nullptr;
    XPLMDataRef drFlapsRight    = nullptr;
    XPLMDataRef drGearDeploy    = nullptr;
    XPLMDataRef drSpoilers      = nullptr;

    XPLMDataRef drOnGround      = nullptr;
    XPLMDataRef drLightNav      = nullptr;
    XPLMDataRef drLightBeacon   = nullptr;
    XPLMDataRef drLightStrobe   = nullptr;
    XPLMDataRef drLightTaxi     = nullptr;
    XPLMDataRef drLightLanding  = nullptr;
    XPLMDataRef drFlapsHandle   = nullptr;
    XPLMDataRef drGearHandle    = nullptr;
    XPLMDataRef drEngRunning    = nullptr;

    XPLMDataRef drTitle         = nullptr;
    XPLMDataRef drAtcModel      = nullptr;

    // Conversion constants: 1 m/s = 1.94384 knots.
    // kFlapToDegrees is a reasonable default for many airliners (full flap ≈ 40°)
    // but is aircraft-specific; adjusting it does not affect the 0–1 ratio
    // already exposed through TRAILING_EDGE_FLAPS_LEFT/RIGHT_ANGLE.
    static constexpr float kMsToKnots    = 1.94384f;  // 1 m/s = 1.94384 knots
    static constexpr float kFlapToDegrees= 40.0f;     // nominal full-flap angle (degrees)

    // ── Safe accessor helpers ─────────────────────────────────────────────
    static float SafeGetFloat(XPLMDataRef dr) {
        return (dr != nullptr) ? XPLMGetDataf(dr) : 0.0f;
    }
    static double SafeGetDouble(XPLMDataRef dr) {
        return (dr != nullptr) ? XPLMGetDatad(dr) : 0.0;
    }
    static int SafeGetInt(XPLMDataRef dr) {
        return (dr != nullptr) ? XPLMGetDatai(dr) : 0;
    }
    static void SafeGetFloatArray(XPLMDataRef dr, float* out, int count) {
        if (dr != nullptr) XPLMGetDatavf(dr, out, 0, count);
    }
    static void SafeGetIntArray(XPLMDataRef dr, int* out, int count) {
        if (dr != nullptr) XPLMGetDatavi(dr, out, 0, count);
    }
    static std::string ReadByteString(XPLMDataRef dr, int maxLen) {
        if (dr == nullptr) return "";
        std::string buf(static_cast<size_t>(maxLen), '\0');
        XPLMGetDatab(dr, &buf[0], 0, maxLen);
        // Trim to first null byte
        const size_t end = buf.find('\0');
        if (end != std::string::npos) buf.resize(end);
        return buf;
    }
};
