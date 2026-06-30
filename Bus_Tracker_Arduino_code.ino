// =============================================================================
// Bus GPS Tracker — Arduino Mega + NEO-6M + SIMCom A7672S + ThingSpeak
// =============================================================================
// Fixes applied:
//   STARTUP  — feedGPS() during all blocking waits (GSM reset + init)
//   STARTUP  — firstSend flag: upload fires on first valid fix, no 30s wait
//   STARTUP  — AT+CREG? polled in a loop while simultaneously feeding GPS
//   STARTUP  — Diagnostic ATs gated behind DEBUG, timeouts tightened
//   BUG      — sendATcommand uses fixed char[] buffer, no heap growth
//   BUG      — AT+HTTPACTION checks "+HTTPACTION: 0,200", not bare "200"
//   BUG      — GSM init failure halts with error blink, loop() never runs
//   BUG      — lastSendMillis only reset on successful upload
//   BUG      — GPS age check: stale fix (>5s) is not uploaded
//   BUG      — Pre-emptive AT+HTTPTERM before every HTTPINIT (clean slate)
//   BUG      — AT+CPIN? result actually checked (SIM presence verified)
//   BUG      — AT+CGATT? result checked; forced attach if not attached
//   BUG      — AT+CGACT=1,1 called to explicitly activate PDP context
//   BUG      — AT+CREG? also checks ",1" / ",5" for URC mode 2 responses
//   BUG      — AT+CEREG? checked as fallback for LTE registration
//   CORRECT  — float used for lat/lng (AVR double == float anyway)
//   CORRECT  — gps.speed.isValid() checked before reading speed
//   CORRECT  — timeout params changed to unsigned long (was 16-bit unsigned int)
//   CORRECT  — glowLED delay param changed to unsigned long
//   CORRECT  — lastSendMillis = millis() after upload (no interval drift)
//   CORRECT  — sendGpsToThingSpeak returns bool (success/failure)
//   CORRECT  — URL built with snprintf into char[], no String heap churn
//   ROBUST   — AVR hardware watchdog (8s) prevents permanent hangs in field
//   ROBUST   — Consecutive failure counter triggers GSM re-init after 3 fails
//   ROBUST   — Single retry on transient HTTPINIT / HTTPPARA failures
//   ROBUST   — AT+HTTPREAD removed (response body not needed, saves 10s)
//   ROBUST   — glowLED delays shortened (was blocking 2–6s per cycle)
//   ROBUST   — Unused GSM_RESET_PIN removed
// =============================================================================

#include <TinyGPS++.h>
#include <avr/wdt.h>

// ── Serial port aliases ───────────────────────────────────────────────────────
#define SIM_SERIAL  Serial1   // A7672S AT command port
#define GPS_SERIAL  Serial2   // NEO-6M NMEA port

// ── Pin definitions ───────────────────────────────────────────────────────────
const int GSM_PWRKEY_PIN = 3;
const int INDI_PIN       = 11;

// ── Tunable constants ─────────────────────────────────────────────────────────
const unsigned long SEND_INTERVAL       = 20000UL;  // ms between uploads (lowered from 30s to
                                                       // compensate for ~5-10s upload time, since
                                                       // lastSendMillis is captured AFTER upload
                                                       // completes — real-world cadence is ~25-30s)
const unsigned long STATUS_CHECK_INTERVAL = 2000UL;  // ms between "waiting/stale" status checks
const unsigned long GPS_STALE_THRESHOLD = 5000UL;   // ms before fix considered stale
const unsigned long NET_REG_TIMEOUT     = 60000UL;  // ms to wait for network registration
const int           MAX_CONSEC_FAILURES = 3;        // failed uploads before GSM re-init
const unsigned long RETRY_BACKOFF        = 5000UL;   // ms to wait before retrying after a failed upload
const uint16_t       MAX_HDOP           = 500;       // HDOP * 100; 500 = 5.0 — reject poor fixes
                                                       // (you observed visible drift at HDOP 6.33,
                                                       // and clear garbage at HDOP 27.5 — 5.0 gives
                                                       // margin below the drift point you saw)

// ── ThingSpeak config ─────────────────────────────────────────────────────────
const char API_KEY[] = "AYDHNWSD93VRXC1S";
const char APN[]     = "airtelgprs.com";

// ── Debug flag — comment out to silence diagnostic Serial prints ──────────────
#define DEBUG

#ifdef DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ── Global state ──────────────────────────────────────────────────────────────
TinyGPSPlus      gps;
unsigned long    lastSendMillis        = 0;
unsigned long    lastStatusCheckMillis = 0;
bool             firstSend        = true;
int              consecFailures   = 0;

// ── Forward declarations ──────────────────────────────────────────────────────
bool  initializeGSM();
void  hardwareResetGSM();
bool  sendGpsToThingSpeak(float lat, float lng, float spd);
bool  sendATcommand(const char* cmd, const char* expected, unsigned long timeout);
void  feedGPS(unsigned long ms);
void  glowLED(unsigned int pin, unsigned int times, unsigned long del);


// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    SIM_SERIAL.begin(115200);
    GPS_SERIAL.begin(9600);

    pinMode(INDI_PIN,       OUTPUT);
    pinMode(GSM_PWRKEY_PIN, OUTPUT);

    DBGLN(F("=== Bus GPS Tracker starting ==="));

    hardwareResetGSM();

    // If GSM init fails, blink error pattern and halt — do NOT enter loop()
    // with an uninitialised modem.
    if (!initializeGSM()) {
        DBGLN(F("FATAL: GSM init failed. Halting."));
        while (true) {
            glowLED(INDI_PIN, 5, 150UL);  // rapid 5-blink error pattern
            delay(1000);
        }
    }

    // Enable watchdog — if loop() ever stalls >8s the Mega auto-resets.
    wdt_enable(WDTO_8S);
    DBGLN(F("Watchdog enabled (8s). Entering loop."));
}


// =============================================================================
// loop
// =============================================================================
void loop() {
    wdt_reset();  // kick watchdog at top of every iteration

    // Drain GPS UART into TinyGPS++ parser
    while (GPS_SERIAL.available()) {
        gps.encode(GPS_SERIAL.read());
    }

    unsigned long now = millis();

    // ── GPS validity checks ───────────────────────────────────────────────────
    // These run on STATUS_CHECK_INTERVAL (2s) so we notice an acquired fix
    // quickly, without spamming the log/LED on every loop() iteration.

    if (!gps.location.isValid()) {
        if (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL) {
            DBGLN(F("Waiting for GPS fix..."));
            glowLED(INDI_PIN, 2, 150UL);
            lastStatusCheckMillis = now;
        }
        return;
    }

    if (gps.location.age() > GPS_STALE_THRESHOLD) {
        if (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL) {
            DBGLN(F("GPS fix stale — skipping upload."));
            glowLED(INDI_PIN, 2, 150UL);
            lastStatusCheckMillis = now;
        }
        return;
    }

    // Always log HDOP for tuning purposes — shows up whether the fix passes
    // or fails the filter below, so you can watch real-world values over a
    // full route and adjust MAX_HDOP based on data if needed.
    if (gps.hdop.isValid() && (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL)) {
        DBG(F("HDOP: "));
        DBG(gps.hdop.hdop());
    }

    // Reject poor-quality fixes — at HDOP > 5.0 the position error can be
    // tens of meters, which is what caused the ~100m drift you saw on the map.
    if (gps.hdop.isValid() && gps.hdop.value() > MAX_HDOP) {
        if (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL) {
            DBGLN(F("  -> rejected (poor fix quality)"));
            lastStatusCheckMillis = now;
        }
        return;
    } else if (gps.hdop.isValid() && (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL)) {
        DBGLN(F("  -> OK"));
        lastStatusCheckMillis = now;
    }

    // Fix is valid, fresh, and good enough — now respect the upload interval.
    if (!firstSend && (now - lastSendMillis < SEND_INTERVAL)) return;

    // ── Read GPS fields ───────────────────────────────────────────────────────
    // AVR double == float (32-bit), so use float explicitly.
    float curLat = (float)gps.location.lat();
    float curLng = (float)gps.location.lng();
    float curSpd = gps.speed.isValid() ? gps.speed.kmph() : 0.0f;

    // ── Upload ────────────────────────────────────────────────────────────────
    bool ok = sendGpsToThingSpeak(curLat, curLng, curSpd);

    if (ok) {
        consecFailures = 0;
        firstSend      = false;
        lastSendMillis = millis();  // capture time AFTER upload to avoid drift
        glowLED(INDI_PIN, 1, 200UL);
    } else {
        consecFailures++;
        DBGLN(F("Upload failed."));

        // CRITICAL: always advance lastSendMillis on failure too, using a
        // shorter backoff than the normal interval. Without this, a failing
        // upload caused loop() to retry instantly, thousands of times per
        // second, flooding the serial monitor and hammering the modem.
        firstSend      = false;
        lastSendMillis = millis() - SEND_INTERVAL + RETRY_BACKOFF;

        // After N consecutive failures, re-initialise the GSM module
        if (consecFailures >= MAX_CONSEC_FAILURES) {
            DBGLN(F("Too many failures — reinitialising GSM..."));
            glowLED(INDI_PIN, 5, 150UL);
            wdt_reset();
            hardwareResetGSM();
            if (!initializeGSM()) {
                DBGLN(F("GSM re-init failed. Will retry next cycle."));
            }
            consecFailures = 0;
        }
    }
}


// =============================================================================
// sendGpsToThingSpeak
// Returns true on successful HTTP 200, false otherwise.
// =============================================================================
bool sendGpsToThingSpeak(float lat, float lng, float spd) {
    DBG(F("Lat: "));  DBG(lat);  DBG(F("  Lng: "));  DBG(lng);
    DBG(F("  Spd: ")); DBG(spd); DBGLN(F(" km/h"));

    // CRITICAL: AVR's snprintf does NOT support %f / floating point formatting
    // (avr-libc strips it out by default to save flash). Using "%.6f" silently
    // produces garbage/empty output, which is exactly what was causing NaN
    // fields on ThingSpeak. dtostrf() is the AVR-safe way to convert a float
    // to a decimal string without pulling in the full float-printf library
    // and without using String (which caused the original heap fragmentation).
    char latStr[12];
    char lngStr[12];
    char spdStr[10];
    dtostrf(lat, 1, 6, latStr);   // width 1 (no padding), 6 decimal places
    dtostrf(lng, 1, 6, lngStr);
    dtostrf(spd, 1, 2, spdStr);   // 2 decimal places for speed

    // Build URL into a fixed stack buffer — no heap allocation
    char url[200];
    snprintf(url, sizeof(url),
        "http://api.thingspeak.com/update?api_key=%s&field1=%s&field2=%s&field3=%s",
        API_KEY, latStr, lngStr, spdStr);

    // Build the AT+HTTPPARA command in one fixed buffer
    char paraCmd[220];
    snprintf(paraCmd, sizeof(paraCmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);

    // Pre-emptive HTTPTERM to guarantee a clean HTTP stack state
    sendATcommand("AT+HTTPTERM", "OK", 3000UL);

    // Open HTTP session
    bool ok1 = sendATcommand("AT+HTTPINIT", "OK", 5000UL);
    if (!ok1) {
        // Single retry on transient failure
        DBGLN(F("HTTPINIT retry..."));
        ok1 = sendATcommand("AT+HTTPINIT", "OK", 5000UL);
    }
    if (!ok1) {
        DBGLN(F("HTTPINIT failed."));
        sendATcommand("AT+HTTPTERM", "OK", 3000UL);
        return false;
    }

    // Set URL
    bool ok2 = sendATcommand(paraCmd, "OK", 5000UL);
    if (!ok2) {
        DBGLN(F("HTTPPARA retry..."));
        ok2 = sendATcommand(paraCmd, "OK", 5000UL);
    }
    if (!ok2) {
        DBGLN(F("HTTPPARA failed."));
        sendATcommand("AT+HTTPTERM", "OK", 3000UL);
        return false;
    }

    // Execute GET — A7672S returns "+HTTPACTION: 0,200,<len>" as a URC
    bool ok3 = sendATcommand("AT+HTTPACTION=0", "+HTTPACTION: 0,200", 30000UL);

    sendATcommand("AT+HTTPTERM", "OK", 3000UL);  // always close session

    if (ok3) {
        DBGLN(F("Data sent to ThingSpeak."));
    } else {
        DBGLN(F("ThingSpeak upload failed (no 200 response)."));
    }

    return ok3;
}


// =============================================================================
// initializeGSM
// Returns true when module is registered on network and data bearer is up.
// Feeds GPS throughout so NEO-6M hot-start sentences are never lost.
// =============================================================================
bool initializeGSM() {
    // ── Wait for module to respond to AT ─────────────────────────────────────
    DBGLN(F("Waiting for A7672S..."));
    bool atOk = false;
    for (int i = 0; i < 5; i++) {
        feedGPS(500UL);
        if (sendATcommand("AT", "OK", 2000UL)) { atOk = true; break; }
    }
    if (!atOk) {
        DBGLN(F("AT failed after 5 attempts."));
        return false;
    }
    DBGLN(F("A7672S ready."));

    // ── Suppress echoing AT commands back (cleaner response parsing) ──────────
    sendATcommand("ATE0", "OK", 2000UL);

    // ── SIM card check ────────────────────────────────────────────────────────
    feedGPS(300UL);
    if (!sendATcommand("AT+CPIN?", "READY", 5000UL)) {
        DBGLN(F("ERROR: SIM not ready. Check card/PIN."));
        return false;
    }
    DBGLN(F("SIM ready."));

    // ── Network registration — poll with GPS feed ─────────────────────────────
    DBGLN(F("Waiting for network registration..."));
    bool registered = false;
    unsigned long regStart = millis();

    while (millis() - regStart < NET_REG_TIMEOUT) {
        feedGPS(1000UL);  // keep GPS buffer drained during the wait

        // Check GSM registration (home or roaming, URC mode 0, 1, or 2)
        if (sendATcommand("AT+CREG?", ",1", 2000UL) ||
            sendATcommand("AT+CREG?", ",5", 2000UL)) {
            registered = true;
            break;
        }

        // Also check LTE registration (more relevant for A7672S on 4G)
        if (sendATcommand("AT+CEREG?", ",1", 2000UL) ||
            sendATcommand("AT+CEREG?", ",5", 2000UL)) {
            registered = true;
            break;
        }

        DBG(F("."));
    }

    if (!registered) {
        DBGLN(F("ERROR: Network registration timed out."));
        return false;
    }
    DBGLN(F("Network registered."));

    // ── GPRS attach check ─────────────────────────────────────────────────────
    feedGPS(300UL);
    if (!sendATcommand("AT+CGATT?", "+CGATT: 1", 5000UL)) {
        DBGLN(F("Not attached — forcing GPRS attach..."));
        sendATcommand("AT+CGATT=1", "OK", 10000UL);
    }

    // ── APN and PDP context ───────────────────────────────────────────────────
    feedGPS(300UL);
    char cgdcont[60];
    snprintf(cgdcont, sizeof(cgdcont), "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
    if (!sendATcommand(cgdcont, "OK", 5000UL)) {
        DBGLN(F("WARNING: CGDCONT failed. Continuing anyway."));
    }

    feedGPS(300UL);
    if (!sendATcommand("AT+CGACT=1,1", "OK", 10000UL)) {
        DBGLN(F("WARNING: CGACT failed. HTTP may still work if bearer auto-activates."));
    }

#ifdef DEBUG
    // Signal quality — diagnostic only, gated behind DEBUG
    feedGPS(200UL);
    sendATcommand("AT+CSQ", "OK", 2000UL);
#endif

    DBGLN(F("GSM initialisation complete."));
    return true;
}


// =============================================================================
// hardwareResetGSM
// Pulses PWRKEY to power-cycle the A7672S.
// All delays replaced with feedGPS() so NEO-6M sentences are never dropped.
// =============================================================================
void hardwareResetGSM() {
    DBGLN(F("Power-cycling A7672S..."));

    feedGPS(2000UL);                     // settle time before touching PWRKEY

    digitalWrite(GSM_PWRKEY_PIN, LOW);
    feedGPS(200UL);                      // minimum pulse width
    digitalWrite(GSM_PWRKEY_PIN, HIGH);

    DBGLN(F("PWRKEY pulsed. Waiting for boot..."));
    feedGPS(10000UL);                    // A7672S boot time (~8-10s)

    DBGLN(F("Reset complete."));
}


// =============================================================================
// feedGPS
// Runs for `ms` milliseconds, continuously draining GPS_SERIAL into TinyGPS++.
// Use this instead of delay() anywhere during setup or blocking waits.
// Also kicks the watchdog so long waits don't trigger a reset.
// =============================================================================
void feedGPS(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        while (GPS_SERIAL.available()) {
            gps.encode(GPS_SERIAL.read());
        }
        wdt_reset();
    }
}


// =============================================================================
// sendATcommand
// Sends an AT command and waits up to `timeout` ms for `expected` in response.
// Uses a fixed char[] buffer — no heap allocation, no String fragmentation.
// timeout is unsigned long (not unsigned int) to support values beyond 65535ms.
// =============================================================================
bool sendATcommand(const char* cmd, const char* expected, unsigned long timeout) {
    // Fixed response buffer — prevents unbounded SRAM growth
    char    response[256];
    uint8_t responseLen = 0;
    memset(response, 0, sizeof(response));

    SIM_SERIAL.println(cmd);

    unsigned long start = millis();
    while (millis() - start < timeout) {
        while (SIM_SERIAL.available()) {
            char c = (char)SIM_SERIAL.read();
            if (responseLen < sizeof(response) - 1) {
                response[responseLen++] = c;
                response[responseLen]   = '\0';
            }
            if (strstr(response, expected) != NULL) {
                DBG(F("AT OK | "));
                DBGLN(response);
                return true;
            }
        }
        wdt_reset();  // kick watchdog during long waits (e.g. HTTPACTION 30s)
    }

    DBG(F("AT TIMEOUT | cmd: "));
    DBG(cmd);
    DBG(F(" | resp: "));
    DBGLN(response);
    return false;
}


// =============================================================================
// glowLED
// Non-blocking-friendly LED blinker.
// del is unsigned long (not unsigned int) — no 65535ms cap.
// Delays shortened: status blinks no longer eat 2–6s per cycle.
// =============================================================================
void glowLED(unsigned int pin, unsigned int times, unsigned long del) {
    for (unsigned int i = 0; i < times; i++) {
        digitalWrite(pin, HIGH); delay(del);
        digitalWrite(pin, LOW);  delay(del);
    }
}
