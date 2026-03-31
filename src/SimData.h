#pragma once
#include <string>

/**
 * SimData – plain data structure holding a single snapshot of all simulator
 * fields that are streamed to the FlyV WebSocket server.
 *
 * Field naming mirrors the MSFS SimConnect variable names used elsewhere in
 * the FlyV ecosystem so that the server-side consumer is simulator-agnostic.
 */
struct SimData {
    // ── Position / flight dynamics ──────────────────────────────────────────
    double  PLANE_ALTITUDE;              // MSL altitude (metres)
    double  PLANE_LATITUDE;              // degrees
    double  PLANE_LONGITUDE;             // degrees
    float   PLANE_ALT_ABOVE_GROUND;      // AGL altitude (metres)
    float   AIRSPEED_INDICATED;          // knots
    float   AIRSPEED_TRUE;               // knots
    float   VERTICAL_SPEED;             // fpm – rounded
    float   PLANE_HEADING_DEGREES_TRUE;  // degrees
    float   PLANE_HEADING_MAGNETIC;
    float   PLANE_PITCH_DEGREES;         // degrees (nose-up positive)
    float   PLANE_BANK_DEGREES;          // degrees (right-bank positive)
    float   GROUND_VELOCITY;             // knots – rounded

    // ── Environment ─────────────────────────────────────────────────────────
    float   AMBIENT_WIND_DIRECTION;      // degrees magnetic
    float   AMBIENT_WIND_VELOCITY;       // knots
    float   AMBIENT_TEMPERATURE;         // °C
    float   AMBIENT_PRESSURE;            // inHg

    // ── Engine N1 (% rpm) ───────────────────────────────────────────────────
    float   ENG_N1_RPM_1;
    float   ENG_N1_RPM_2;
    float   ENG_N1_RPM_3;
    float   ENG_N1_RPM_4;

    // ── Engine N2 (% rpm) ───────────────────────────────────────────────────
    float   ENG_N2_RPM_1;
    float   ENG_N2_RPM_2;
    float   ENG_N2_RPM_3;
    float   ENG_N2_RPM_4;

    // ── float64 fields ──────────────────────────────────────────────────────
    double  NUMBER_OF_ENGINES;
    double  G_FORCE;
    double  TRAILING_EDGE_FLAPS_LEFT_ANGLE;   // degrees
    double  TRAILING_EDGE_FLAPS_RIGHT_ANGLE;  // degrees
    double  GEAR_CENTER_POSITION;             // 0.0–1.0 deploy ratio
    double  GEAR_LEFT_POSITION;
    double  GEAR_RIGHT_POSITION;
    double  SPOILERS_HANDLE_POSITION;         // 0.0–1.0

    // ── int32 fields ────────────────────────────────────────────────────────
    int     SIM_ON_GROUND;
    int     LIGHT_NAV_ON;
    int     LIGHT_BEACON_ON;
    int     LIGHT_STROBE_ON;
    int     LIGHT_TAXI_ON;
    int     LIGHT_LANDING_ON;
    int     FLAPS_HANDLE_INDEX;       // mapped from 0-1 ratio × 10
    int     GEAR_HANDLE_POSITION;     // 0 = up, 1 = down

    // ── Engine combustion (bool as int) ─────────────────────────────────────
    int     GENERAL_ENG_COMBUSTION_1;
    int     GENERAL_ENG_COMBUSTION_2;
    int     GENERAL_ENG_COMBUSTION_3;
    int     GENERAL_ENG_COMBUSTION_4;

    // ── String fields ────────────────────────────────────────────────────────
    std::string TITLE;      // aircraft description / full name
    std::string ATC_MODEL;  // ICAO aircraft type code
};
