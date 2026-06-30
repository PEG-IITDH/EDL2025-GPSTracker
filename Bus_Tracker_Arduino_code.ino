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
const unsigned long FORCE_UPLOAD_DEADLINE = 90000UL; // ms — if no good-HDOP upload has happened
                                                       // in this long, send the BEST available fix
                                                       // anyway (even if HDOP > MAX_HDOP), so the
                                                       // website doesn't go "Not live" (105s staleness
                                                       // threshold) just because HDOP hasn't settled
                                                       // yet after a restart. 90s gives margin under
                                                       // the website's 105s cutoff.

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
unsigned long    lastSendMillis          = 0;
unsigned long    lastStatusCheckMillis   = 0;
unsigned long    lastUploadAttemptMillis = 0;  // tracks time since ANY upload
                                                // attempt (good or forced HDOP),
                                                // used to detect "gone quiet too
                                                // long" and force a send through
bool             firstSend        = true;
int              consecFailures   = 0;

// ── Rolling buffer of recent fix candidates ───────────────────────────────────
// Used by the force-upload deadline: instead of remembering a single "best
// HDOP ever seen since last upload" (which could be up to 90s stale by the
// time it's used), we keep a small ring buffer of the last few fixes with
// their timestamps. When the deadline trips, we only consider candidates
// from the last RECENT_FIX_WINDOW (30s) and pick the best HDOP among those —
// capping max staleness of a forced send at 30s instead of 90s.
const unsigned long RECENT_FIX_WINDOW = 30000UL;  // ms — only consider fixes
                                                    // from this recent a window
                                                    // when forced to pick a
                                                    // "best available" fix
const uint8_t  FIX_BUFFER_SIZE = 8;  // ~1 sample per ~2s status interval over
                                       // 30s gives plenty of headroom; small
                                       // enough to be cheap on AVR SRAM
struct FixCandidate {
    unsigned long timestamp;
    float         hdop;
    float         lat;
    float         lng;
    float         spd;
    bool          valid;
};
FixCandidate fixBuffer[FIX_BUFFER_SIZE];
uint8_t      fixBufferIdx = 0;

// ── Forward declarations ──────────────────────────────────────────────────────
bool  initializeGSM();
void  hardwareResetGSM();
bool  sendGpsToThingSpeak(float lat, float lng, float spd);
bool  sendATcommand(const char* cmd, const char* expected, unsigned long timeout);
void  feedGPS(unsigned long ms);
void  glowLED(unsigned int pin, unsigned int times, unsigned long del);
void  recordFixCandidate(unsigned long ts, float hdop, float lat, float lng, float spd);
bool  findBestRecentFix(unsigned long now, unsigned long windowMs, FixCandidate &out);
void  clearFixBuffer();


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
// recordFixCandidate / findBestRecentFix / clearFixBuffer
// Small ring buffer of recent fix readings, used so the force-upload deadline
// can pick the best HDOP seen within only the last RECENT_FIX_WINDOW (30s),
// instead of either (a) whatever HDOP happens to be live at the exact deadline
// moment, or (b) a "best ever" reading that could be up to 90s stale.
// =============================================================================
void recordFixCandidate(unsigned long ts, float hdop, float lat, float lng, float spd) {
    fixBuffer[fixBufferIdx].timestamp = ts;
    fixBuffer[fixBufferIdx].hdop      = hdop;
    fixBuffer[fixBufferIdx].lat       = lat;
    fixBuffer[fixBufferIdx].lng       = lng;
    fixBuffer[fixBufferIdx].spd       = spd;
    fixBuffer[fixBufferIdx].valid     = true;
    fixBufferIdx = (fixBufferIdx + 1) % FIX_BUFFER_SIZE;
}

// Scans the buffer for the lowest-HDOP candidate whose timestamp is within
// windowMs of `now`. Returns true and fills `out` if one was found.
bool findBestRecentFix(unsigned long now, unsigned long windowMs, FixCandidate &out) {
    bool found = false;
    float bestHdop = 9999.0f;
    for (uint8_t i = 0; i < FIX_BUFFER_SIZE; i++) {
        if (!fixBuffer[i].valid) continue;
        // millis()-safe age check (handles rollover correctly via unsigned subtraction)
        unsigned long age = now - fixBuffer[i].timestamp;
        if (age > windowMs) continue;  // too old, outside the recent window
        if (fixBuffer[i].hdop < bestHdop) {
            bestHdop = fixBuffer[i].hdop;
            out      = fixBuffer[i];
            found    = true;
        }
    }
    return found;
}

void clearFixBuffer() {
    for (uint8_t i = 0; i < FIX_BUFFER_SIZE; i++) {
        fixBuffer[i].valid = false;
    }
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

    // Record into the rolling buffer at most once per RECORD_INTERVAL, not on
    // every loop() iteration (loop can run thousands of times/sec — without
    // this throttle, the small ring buffer would be overwritten within
    // milliseconds and never actually span the intended 30-second window).
    static unsigned long lastRecordMillis = 0;
    const unsigned long RECORD_INTERVAL = (RECENT_FIX_WINDOW / FIX_BUFFER_SIZE);
    if (gps.hdop.isValid() && (now - lastRecordMillis >= RECORD_INTERVAL)) {
        recordFixCandidate(
            now,
            gps.hdop.value(),
            (float)gps.location.lat(),
            (float)gps.location.lng(),
            gps.speed.isValid() ? gps.speed.kmph() : 0.0f
        );
        lastRecordMillis = now;
    }

    // Reject poor-quality fixes — at HDOP > 5.0 the position error can be
    // tens of meters, which is what caused the ~100m drift you saw on the map.
    // EXCEPTION: if no upload (good or forced) has gone through in
    // FORCE_UPLOAD_DEADLINE ms, send the best fix from the last 30 seconds
    // (RECENT_FIX_WINDOW) anyway, rather than whatever HDOP the receiver
    // currently reports. This prevents the website's staleness check (105s)
    // from ever showing "Not live" just because HDOP hasn't settled yet
    // after a restart — a less precise (but still recent) position beats no
    // update at all, and capping the lookback at 30s avoids ever sending a
    // position that's badly out of date.
    bool hdopBad = gps.hdop.isValid() && gps.hdop.value() > MAX_HDOP;
    bool forceDeadlineHit = (now - lastUploadAttemptMillis >= FORCE_UPLOAD_DEADLINE);

    if (hdopBad && !forceDeadlineHit) {
        if (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL) {
            DBGLN(F("  -> rejected (poor fix quality)"));
            lastStatusCheckMillis = now;
        }
        return;
    } else if (hdopBad && forceDeadlineHit) {
        FixCandidate best;
        bool haveBest = findBestRecentFix(now, RECENT_FIX_WINDOW, best);

        if (now - lastStatusCheckMillis >= STATUS_CHECK_INTERVAL) {
            if (haveBest) {
                DBG(F("  -> deadline hit, forcing best fix from last 30s (HDOP "));
                DBG(best.hdop);
                DBGLN(F(")"));
            } else {
                DBGLN(F("  -> deadline hit, no fix in last 30s, using live reading"));
            }
            lastStatusCheckMillis = now;
        }

        // If nothing qualifies within the 30s window (e.g. HDOP just became
        // valid this instant), fall back to the current live reading rather
        // than skipping the forced send entirely.
        float fLat = haveBest ? best.lat : (float)gps.location.lat();
        float fLng = haveBest ? best.lng : (float)gps.location.lng();
        float fSpd = haveBest ? best.spd : (gps.speed.isValid() ? gps.speed.kmph() : 0.0f);

        bool forcedOk = sendGpsToThingSpeak(fLat, fLng, fSpd);
        lastUploadAttemptMillis = millis();

        if (forcedOk) {
            consecFailures = 0;
            firstSend      = false;
            lastSendMillis = millis();
            clearFixBuffer();  // reset candidates now that we've used one
            glowLED(INDI_PIN, 1, 200UL);
        } else {
            consecFailures++;
            DBGLN(F("Forced upload failed."));
            firstSend      = false;
            lastSendMillis = millis() - SEND_INTERVAL + RETRY_BACKOFF;
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
        return;  // handled this cycle's upload already, skip the normal path below
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
    lastUploadAttemptMillis = millis();  // any attempt resets the force-deadline timer

    if (ok) {
        consecFailures = 0;
        // Reset the fix candidate buffer after any successful normal upload
        // too, so it doesn't hold stale candidates from before this send.
        clearFixBuffer();
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
