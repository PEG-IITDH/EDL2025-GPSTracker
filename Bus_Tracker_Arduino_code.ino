#include <TinyGPS++.h>

#define SIM_SERIAL  Serial1
#define GPS_SERIAL  Serial2

const int GSM_PWRKEY_PIN = 3;
const int GSM_RESET_PIN  = 2;
const int INDI_PIN       = 11;

const unsigned long SEND_INTERVAL = 30000UL;

const char* API_KEY = "AYDHNWSD93VRXC1S";

TinyGPSPlus gps;
unsigned long lastSendMillis = 0;

bool  initializeGSM();
void  hardwareResetGSM();
void  sendGpsToThingSpeak(double lat, double lng, float spd);
bool  sendATcommand(String cmd, String expected, unsigned int timeout);
void  glowLED(unsigned int pin, unsigned int times, unsigned int del);

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

void loop() {
    while (GPS_SERIAL.available()) {
        gps.encode(GPS_SERIAL.read());
    }

    unsigned long now = millis();

    if (now - lastSendMillis < SEND_INTERVAL) return;

    if (!gps.location.isValid()) {
        Serial.println(F("Waiting for GPS fix..."));
        glowLED(INDI_PIN, 2, 300);
        return;
    }

    double curLat = gps.location.lat();
    double curLng = gps.location.lng();
    float  curSpd = gps.speed.kmph();

    sendGpsToThingSpeak(curLat, curLng, curSpd);

    lastSendMillis = now;
}

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
