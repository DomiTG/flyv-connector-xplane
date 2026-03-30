#pragma once
#include <sstream>
#include <iomanip>
#include <string>
#include <cmath>

#include "SimData.h"

/**
 * JsonSerializer
 *
 * Converts a SimData snapshot into a compact JSON string.
 * Deliberately avoids any external JSON library to keep the plugin
 * self-contained.  Only forward-slashes and ASCII printable characters
 * appear in the output, so no special escaping is needed for the fixed
 * numeric / ASCII-only string fields.
 */
class JsonSerializer {
public:
    static std::string Serialize(const SimData& d) {
        std::ostringstream o;
        o << std::fixed;

        o << '{';

        // ── Position / flight dynamics ────────────────────────────────────
        AppendDouble(o, "PLANE_ALTITUDE",             d.PLANE_ALTITUDE,             15); o << ',';
        AppendDouble(o, "PLANE_LATITUDE",             d.PLANE_LATITUDE,             10); o << ',';
        AppendDouble(o, "PLANE_LONGITUDE",            d.PLANE_LONGITUDE,            10); o << ',';
        AppendFloat (o, "PLANE_ALT_ABOVE_GROUND",     d.PLANE_ALT_ABOVE_GROUND,      3); o << ',';
        AppendFloat (o, "AIRSPEED_INDICATED",          d.AIRSPEED_INDICATED,          2); o << ',';
        AppendFloat (o, "AIRSPEED_TRUE",               d.AIRSPEED_TRUE,               2); o << ',';
        AppendFloat (o, "VERTICAL_SPEED",              d.VERTICAL_SPEED,              0); o << ',';
        AppendFloat (o, "PLANE_HEADING_DEGREES_TRUE",  d.PLANE_HEADING_DEGREES_TRUE,  2); o << ',';
        AppendFloat (o, "PLANE_PITCH_DEGREES",         d.PLANE_PITCH_DEGREES,         2); o << ',';
        AppendFloat (o, "PLANE_BANK_DEGREES",          d.PLANE_BANK_DEGREES,          2); o << ',';
        AppendFloat (o, "GROUND_VELOCITY",             d.GROUND_VELOCITY,             0); o << ',';

        // ── Environment ───────────────────────────────────────────────────
        AppendFloat (o, "AMBIENT_WIND_DIRECTION",      d.AMBIENT_WIND_DIRECTION,      2); o << ',';
        AppendFloat (o, "AMBIENT_WIND_VELOCITY",       d.AMBIENT_WIND_VELOCITY,       2); o << ',';
        AppendFloat (o, "AMBIENT_TEMPERATURE",         d.AMBIENT_TEMPERATURE,         2); o << ',';
        AppendFloat (o, "AMBIENT_PRESSURE",            d.AMBIENT_PRESSURE,            4); o << ',';

        // ── Engine N1 ─────────────────────────────────────────────────────
        AppendFloat (o, "ENG_N1_RPM_1",               d.ENG_N1_RPM_1,               2); o << ',';
        AppendFloat (o, "ENG_N1_RPM_2",               d.ENG_N1_RPM_2,               2); o << ',';
        AppendFloat (o, "ENG_N1_RPM_3",               d.ENG_N1_RPM_3,               2); o << ',';
        AppendFloat (o, "ENG_N1_RPM_4",               d.ENG_N1_RPM_4,               2); o << ',';

        // ── Engine N2 ─────────────────────────────────────────────────────
        AppendFloat (o, "ENG_N2_RPM_1",               d.ENG_N2_RPM_1,               2); o << ',';
        AppendFloat (o, "ENG_N2_RPM_2",               d.ENG_N2_RPM_2,               2); o << ',';
        AppendFloat (o, "ENG_N2_RPM_3",               d.ENG_N2_RPM_3,               2); o << ',';
        AppendFloat (o, "ENG_N2_RPM_4",               d.ENG_N2_RPM_4,               2); o << ',';

        // ── float64 fields ────────────────────────────────────────────────
        AppendDouble(o, "NUMBER_OF_ENGINES",            d.NUMBER_OF_ENGINES,            0); o << ',';
        AppendDouble(o, "G_FORCE",                      d.G_FORCE,                      4); o << ',';
        AppendDouble(o, "TRAILING_EDGE_FLAPS_LEFT_ANGLE",  d.TRAILING_EDGE_FLAPS_LEFT_ANGLE,  2); o << ',';
        AppendDouble(o, "TRAILING_EDGE_FLAPS_RIGHT_ANGLE", d.TRAILING_EDGE_FLAPS_RIGHT_ANGLE, 2); o << ',';
        AppendDouble(o, "GEAR_CENTER_POSITION",         d.GEAR_CENTER_POSITION,         4); o << ',';
        AppendDouble(o, "GEAR_LEFT_POSITION",           d.GEAR_LEFT_POSITION,           4); o << ',';
        AppendDouble(o, "GEAR_RIGHT_POSITION",          d.GEAR_RIGHT_POSITION,          4); o << ',';
        AppendDouble(o, "SPOILERS_HANDLE_POSITION",     d.SPOILERS_HANDLE_POSITION,     4); o << ',';

        // ── int32 fields ──────────────────────────────────────────────────
        AppendInt   (o, "SIM_ON_GROUND",               d.SIM_ON_GROUND);               o << ',';
        AppendInt   (o, "LIGHT_NAV_ON",                d.LIGHT_NAV_ON);                o << ',';
        AppendInt   (o, "LIGHT_BEACON_ON",             d.LIGHT_BEACON_ON);             o << ',';
        AppendInt   (o, "LIGHT_STROBE_ON",             d.LIGHT_STROBE_ON);             o << ',';
        AppendInt   (o, "LIGHT_TAXI_ON",               d.LIGHT_TAXI_ON);               o << ',';
        AppendInt   (o, "LIGHT_LANDING_ON",            d.LIGHT_LANDING_ON);            o << ',';
        AppendInt   (o, "FLAPS_HANDLE_INDEX",          d.FLAPS_HANDLE_INDEX);          o << ',';
        AppendInt   (o, "GEAR_HANDLE_POSITION",        d.GEAR_HANDLE_POSITION);        o << ',';

        // ── Engine combustion ─────────────────────────────────────────────
        AppendInt   (o, "GENERAL_ENG_COMBUSTION_1",   d.GENERAL_ENG_COMBUSTION_1);    o << ',';
        AppendInt   (o, "GENERAL_ENG_COMBUSTION_2",   d.GENERAL_ENG_COMBUSTION_2);    o << ',';
        AppendInt   (o, "GENERAL_ENG_COMBUSTION_3",   d.GENERAL_ENG_COMBUSTION_3);    o << ',';
        AppendInt   (o, "GENERAL_ENG_COMBUSTION_4",   d.GENERAL_ENG_COMBUSTION_4);    o << ',';

        // ── String fields (last, no trailing comma) ───────────────────────
        AppendString(o, "TITLE",     d.TITLE);      o << ',';
        AppendString(o, "ATC_MODEL", d.ATC_MODEL);

        o << '}';
        return o.str();
    }

    // ── Message envelope helpers ──────────────────────────────────────────

    /** Build {"type":"AircraftData","data":{"Aircraft":{...}}} */
    static std::string SerializeAircraftEnvelope(const SimData& d) {
        return "{\"type\":\"AircraftData\",\"data\":{\"Aircraft\":" + Serialize(d) + "}}";
    }

    /**
     * Build {"type":"Status","data":{"simulator_loaded":<bool>,
     *   "simulator_connected":<bool>,"simulator_name":"<name>","last_error":"<err>"}}
     */
    static std::string SerializeStatusEnvelope(bool loaded,
                                               bool connected,
                                               const std::string& simName,
                                               const std::string& lastError) {
        return std::string("{\"type\":\"Status\",\"data\":{"
                           "\"simulator_loaded\":") +
               (loaded    ? "true" : "false") +
               ",\"simulator_connected\":" +
               (connected ? "true" : "false") +
               ",\"simulator_name\":\"" + EscapeJson(simName) + "\"," +
               "\"last_error\":\""      + EscapeJson(lastError) + "\"}}";
    }

    /**
     * Build the unsolicited on-connect message:
     *   {"data":{"code":"600","message":"<simName>"}}  when connected, or
     *   {"data":{"code":"404","message":""}}            when not.
     */
    static std::string SerializeConnectMessage(bool connected,
                                               const std::string& simName) {
        const std::string code = connected ? "600" : "404";
        const std::string msg  = connected ? simName : "";
        return "{\"data\":{\"code\":\"" + code +
               "\",\"message\":\"" + EscapeJson(msg) + "\"}}";
    }

    /** Build {"type":"pong","data":{}} */
    static std::string SerializePongEnvelope() {
        return "{\"type\":\"pong\",\"data\":{}}";
    }

    /** Build {"type":"Error","data":{"message":"<msg>"}} */
    static std::string SerializeErrorEnvelope(const std::string& msg) {
        return "{\"type\":\"Error\",\"data\":{\"message\":\"" +
               EscapeJson(msg) + "\"}}";
    }

    /** Minimal JSON string escaping (also used by envelope helpers). */
    static std::string EscapeJson(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20) {
                        std::ostringstream esc;
                        esc << "\\u"
                            << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<unsigned>(c);
                        out += esc.str();
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    }

private:
    static void AppendFloat(std::ostringstream& o, const char* key, float val, int prec) {
        o << '"' << key << "\":" << std::setprecision(prec) << val;
    }
    static void AppendDouble(std::ostringstream& o, const char* key, double val, int prec) {
        o << '"' << key << "\":" << std::setprecision(prec) << val;
    }
    static void AppendInt(std::ostringstream& o, const char* key, int val) {
        o << '"' << key << "\":" << val;
    }
    static void AppendString(std::ostringstream& o, const char* key, const std::string& val) {
        o << '"' << key << "\":\"" << EscapeJson(val) << '"';
    }
};
