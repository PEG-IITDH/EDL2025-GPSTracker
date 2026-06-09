// ============================================================
//  IIT Dharwad Bus Tracker — Optimised GPS Upload
//  ThingSpeak Free Tier: hard minimum 15 s between updates
//
//  Strategy
//  --------
//  • Poll GPS every loop() iteration (no blocking delay).
//  • Enforce a 15 s hard floor between HTTP posts (ThingSpeak limit).
//  • Skip the post if the bus hasn't moved > MIN_MOVE_METRES —
//    saves quota when the bus is parked / stationary at a stop.
//  • Speed zones handled automatically:
//      inside campus  ≤ 50 km/h → moves ≤ ~208 m in 15 s  (fine grain)
//      outside campus ≤ 80 km/h → moves ≤ ~333 m in 15 s  (fine grain)
//  Both are well within the inter-stop distances, so no position
//  updates are ever "missed" between stops.
// ============================================================

#include <TinyGPS++.h>

#define SIM_SERIAL  Serial1   // A7672S  TX→19, RX→18
#define GPS_SERIAL  Serial2   // GPS     TX→17, RX→16

const int GSM_PWRKEY_PIN = 3;
const int GSM_RESET_PIN  = 2;
const int INDI_PIN       = 11;

// ── Timing ──────────────────────────────────────────────────
// ThingSpeak free tier: 1 update per 15 seconds (hard limit).
// Setting SEND_INTERVAL to exactly 15 000 ms keeps us on the
// safe side; you can raise it if you want to conserve quota
// even more (e.g. 20 000 ms at night).
const unsigned long SEND_INTERVAL = 15000UL;   // ms — DO NOT go below 15000

// ── Movement threshold ───────────────────────────────────────
// Don't POST if the bus has moved less than this many metres
// since the last successful upload.  Avoids burning quota
// while the bus is parked at a stop.
const float MIN_MOVE_METRES = 10.0f;           // ~2 bus-lengths

// ── ThingSpeak ───────────────────────────────────────────────
const char* API_KEY = "AYDHNWSD93VRXC1S";

// ── State ────────────────────────────────────────────────────
TinyGPSPlus gps;
unsigned long lastSendMillis = 0;
double lastSentLat  = 0.0;
double lastSentLng  = 0.0;
bool   firstFix     = true;   // always send the very first valid fix

// ── Forward declarations ─────────────────────────────────────
bool  initializeGSM();
void  hardwareResetGSM();
void  sendGpsToThingSpeak(double lat, double lng, float spd);
bool  sendATcommand(String cmd, String expected, unsigned int timeout);
void  glowLED(unsigned int pin, unsigned int times, unsigned int del);
float haversineMetres(double lat1, double lng1, double lat2, double lng2);

// ════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    SIM_SERIAL.begin(115200);
    GPS_SERIAL.begin(9600);

    pinMode(INDI_PIN,       OUTPUT);
    pinMode(GSM_RESET_PIN,  OUTPUT);
    pinMode(GSM_PWRKEY_PIN, OUTPUT);

    Serial.println(F("Initialising GSM module..."));
    hardwareResetGSM();

    if (!initializeGSM()) {
        Serial.println(F("ERROR: SIM module not responding."));
        glowLED(INDI_PIN, 3, 500);
    }
}

// ════════════════════════════════════════════════════════════
void loop() {
    // Feed every available GPS byte to TinyGPS+
    while (GPS_SERIAL.available()) {
        gps.encode(GPS_SERIAL.read());
    }

    unsigned long now = millis();

    // ── Rate-gate: respect ThingSpeak's 15 s minimum ─────────
    if (now - lastSendMillis < SEND_INTERVAL) return;

    // ── Need a valid GPS fix ──────────────────────────────────
    if (!gps.location.isValid()) {
        Serial.println(F("Waiting for GPS fix..."));
        glowLED(INDI_PIN, 2, 300);
        return;
    }

    double curLat = gps.location.lat();
    double curLng = gps.location.lng();
    float  curSpd = gps.speed.kmph();

    // ── Movement gate: skip if bus hasn't moved ───────────────
    if (!firstFix) {
        float moved = haversineMetres(lastSentLat, lastSentLng, curLat, curLng);
        if (moved < MIN_MOVE_METRES) {
            Serial.print(F("Bus stationary ("));
            Serial.print(moved, 1);
            Serial.println(F(" m) — skipping upload to save quota."));
            lastSendMillis = now;   // still reset timer so we recheck in 15 s
            return;
        }
    }

    // ── All checks passed — upload ────────────────────────────
    sendGpsToThingSpeak(curLat, curLng, curSpd);

    lastSentLat    = curLat;
    lastSentLng    = curLng;
    lastSendMillis = now;
    firstFix       = false;
}

// ════════════════════════════════════════════════════════════
//  Haversine distance between two GPS coordinates (metres)
// ════════════════════════════════════════════════════════════
float haversineMetres(double lat1, double lng1, double lat2, double lng2) {
    const double R = 6371000.0;  // Earth radius in metres
    double dLat = radians(lat2 - lat1);
    double dLng = radians(lng2 - lng1);
    double a    = sin(dLat / 2) * sin(dLat / 2)
                + cos(radians(lat1)) * cos(radians(lat2))
                * sin(dLng / 2) * sin(dLng / 2);
    return (float)(R * 2.0 * atan2(sqrt(a), sqrt(1.0 - a)));
}

// ════════════════════════════════════════════════════════════
//  ThingSpeak HTTP POST
// ════════════════════════════════════════════════════════════
void sendGpsToThingSpeak(double lat, double lng, float spd) {
    Serial.print(F("Lat: "));  Serial.println(lat, 6);
    Serial.print(F("Lng: "));  Serial.println(lng, 6);
    Serial.print(F("Spd: "));  Serial.print(spd, 2);
    Serial.println(F(" km/h"));

    String url  = "http://api.thingspeak.com/update?api_key=";
    url += API_KEY;
    url += "&field1=" + String(lat, 6);
    url += "&field2=" + String(lng, 6);
    url += "&field3=" + String(spd, 2);

    bool ok1 = sendATcommand("AT+HTTPINIT",                               "OK", 5000);
    bool ok2 = sendATcommand("AT+HTTPPARA=\"URL\",\"" + url + "\"",       "OK", 5000);
    bool ok3 = sendATcommand("AT+HTTPACTION=0",                           "200", 30000);

    if (ok1 && ok2 && ok3) {
        Serial.println(F("✓ Data sent to ThingSpeak."));
        glowLED(INDI_PIN, 1, 1000);
        sendATcommand("AT+HTTPREAD", "OK", 10000);
    } else {
        Serial.println(F("✗ ThingSpeak upload failed."));
        glowLED(INDI_PIN, 3, 1000);
    }

    sendATcommand("AT+HTTPTERM", "OK", 5000);
}

// ════════════════════════════════════════════════════════════
//  GSM init
// ════════════════════════════════════════════════════════════
bool initializeGSM() {
    if (!sendATcommand("AT", "OK", 5000)) {
        Serial.println(F("AT failed"));
        return false;
    }
    Serial.println(F("SIM module ready."));

    sendATcommand("AT+CPIN?",  "OK", 5000);
    sendATcommand("AT+CSQ",    "OK", 5000);
    sendATcommand("AT+CREG?",  "OK", 5000);
    sendATcommand("AT+CGATT?", "OK", 5000);

    // Airtel APN
    sendATcommand("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"", "OK", 5000);

    if (sendATcommand("AT+CREG?", "+CREG: 0,1", 5000) ||
        sendATcommand("AT+CREG?", "+CREG: 0,5", 5000)) {
        Serial.println(F("Network registered."));
        return true;
    }

    Serial.println(F("ERROR: Not registered on network!"));
    return false;
}

void hardwareResetGSM() {
    delay(2000);
    digitalWrite(GSM_PWRKEY_PIN, HIGH);
    Serial.println(F("Resetting A7672S..."));
    digitalWrite(GSM_PWRKEY_PIN, LOW);
    delay(200);
    digitalWrite(GSM_PWRKEY_PIN, HIGH);
    delay(10000);
    Serial.println(F("Reset complete."));
}

// ════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════
void glowLED(unsigned int pin, unsigned int times, unsigned int del) {
    for (unsigned int i = 0; i < times; i++) {
        digitalWrite(pin, HIGH); delay(del);
        digitalWrite(pin, LOW);  delay(del);
    }
}

bool sendATcommand(String command, String expected_response, unsigned int timeout) {
    String response = "";
    SIM_SERIAL.println(command);
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (SIM_SERIAL.available()) {
            char c = SIM_SERIAL.read();
            response += c;
            if (response.indexOf(expected_response) != -1) {
                Serial.println(F("AT OK"));
                Serial.println(response);
                return true;
            }
        }
    }
    Serial.print(F("AT TIMEOUT | Response: "));
    Serial.println(response);
    return false;
}
