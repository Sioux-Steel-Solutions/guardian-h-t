#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <time.h>

// Include your custom headers
#include "utils/utils.h"
#include "api/api.h"

// Web server on port 80
WebServer server(80);

// Timer variables
unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 30000;

unsigned long lastReconnectAttempt = 5000;
const unsigned long reconnectInterval = 10000;

unsigned long lastWifiRetryAttempt = 0;
const unsigned long wifiRetryInterval = 30000;

unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatInterval = 60000;

// I²C Keep-Alive Timer
unsigned long lastI2CKeepAliveTime = 0;
const unsigned long i2cKeepAliveInterval = 120000; // 2 minutes

// GPIO Constants
const uint8_t targetI2CAddress = 0x01;
const int sdaPin = 5;  // SDA: GPIO5
const int sclPin = 4;  // SCL: GPIO4

void startAccessPoint() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Guardian_GMS_Sensor", "");
    Serial.print("Access point IP: ");
    Serial.println(WiFi.softAPIP());
    digitalWrite(GEN2_LED_PIN, HIGH);  // LED on in AP mode
}

void synchronizeTime() {
    Serial.println("Synchronizing system time...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        Serial.println("Waiting for time synchronization...");
        delay(1000);
    }

    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "Current time: %A, %B %d %Y %H:%M:%S", &timeinfo);
    Serial.println(timeStr);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.println("\n[GEN2] Firmware Started - ESP32-C3 + HDC1080");

    // Initialize LED (Gen 2)
    pinMode(GEN2_LED_PIN, OUTPUT);
    digitalWrite(GEN2_LED_PIN, LOW);  // LED off initially

    // Initialize EEPROM
    EEPROM.begin(512);

    // Initialize HDC1080 sensor
    if (!initHDC1080()) {
        Serial.println("[ERROR] HDC1080 initialization failed!");
        digitalWrite(GEN2_LED_PIN, HIGH);  // Turn on LED to indicate error
        while(1) { delay(1000); }  // Halt if sensor fails
    }

    // Blink LED to indicate successful initialization
    for (int i = 0; i < 3; i++) {
        digitalWrite(GEN2_LED_PIN, HIGH);
        delay(200);
        digitalWrite(GEN2_LED_PIN, LOW);
        delay(200);
    }

    // Gen 1 EEPROM/AP logic (COMMENTED OUT for Gen 2 testing)
    /*
    if (checkForWifiAndUser()) {
        if (connectToWiFi(String(storedConfig.ssid), String(storedConfig.password))) {
            Serial.println("Connected to WiFi successfully.");
            digitalWrite(GEN2_LED_PIN, LOW);
        } else {
            Serial.println("WiFi connection failed, starting Access Point...");
            startAccessPoint();
        }
    } else {
        Serial.println("No Credentials Found, Starting Access Point...");
        startAccessPoint();
    }

    setupApiRoutes(server);
    server.begin();
    Serial.println("Web Server Started");
    */

    // Gen 2 - Hardcoded credentials for testing
    Serial.println("[GEN2 TEST MODE] Using hardcoded WiFi credentials");
    if (connectToWiFi(GEN2_TEST_SSID, GEN2_TEST_PASSWORD)) {
        Serial.println("WiFi connected successfully");
        digitalWrite(GEN2_LED_PIN, HIGH);  // LED ON when WiFi connected!

        // Synchronize time
        synchronizeTime();

        // Set up test user credentials (bypass EEPROM)
        strncpy(storedConfig.ssid, GEN2_TEST_SSID, MAX_SSID_LENGTH);
        strncpy(storedConfig.password, GEN2_TEST_PASSWORD, MAX_PASSWORD_LENGTH);
        strncpy(storedConfig.uuid, GEN2_TEST_USERID, UUID_LENGTH);
        strncpy(storedConfig.deviceId, GEN2_TEST_DEVICEID, DEVICEID_LENGTH);
        doesUserExist = true;

        // Connect to MQTT
        if (connectToMQTT()) {
            Serial.println("MQTT Connected Successfully");
            // LED stays ON - already HIGH from WiFi connection
        } else {
            Serial.println("Failed to Connect to MQTT Broker");
        }
    } else {
        Serial.println("WiFi connection failed!");
        // Blink LED rapidly to indicate error
        while(1) {
            digitalWrite(GEN2_LED_PIN, HIGH);
            delay(200);
            digitalWrite(GEN2_LED_PIN, LOW);
            delay(200);
        }
    }

    Serial.println("[GEN2] Setup complete");
}

void keepAliveI2C() {
    Serial.println("🔄 [I²C Keep-Alive] Sending periodic I²C ping...");
    Wire.beginTransmission(targetI2CAddress);
    Wire.write(0x00); // Attempt to write to a register
    if (Wire.endTransmission() == 0) {
        Serial.println("✅ [I²C Keep-Alive] Communication succeeded, device awake.");
    } else {
        Serial.println("❌ [I²C Keep-Alive] Communication failed.");
    }
}

void loop() {
    // server.handleClient();  // Gen 2: Web server disabled for testing
    unsigned long currentMillis = millis();

    // WiFi Reconnect
    if (doesUserExist && (WiFi.status() != WL_CONNECTED)) {
        if (currentMillis - lastWifiRetryAttempt > wifiRetryInterval) {
            lastWifiRetryAttempt = currentMillis;
            Serial.println("Attempting to reconnect to WiFi...");
            if (connectToWiFi(String(storedConfig.ssid), String(storedConfig.password))) {
                Serial.println("Reconnected to WiFi successfully.");
                // digitalWrite(SHELLY_BUILTIN_LED, HIGH);
                synchronizeTime();

                if (!mqttClient.connected()) {
                    if (connectToMQTT()) {
                        Serial.println("MQTT Connected Successfully after WiFi reconnect.");
                    } else {
                        Serial.println("Failed to Connect to MQTT Broker after WiFi reconnect.");
                    }
                }
            } else {
                Serial.println("WiFi reconnect attempt failed.");
            }
        }
    }

    // MQTT Reconnect
    if (doesUserExist && (WiFi.status() == WL_CONNECTED)) {
        if (!mqttClient.connected()) {
            if (currentMillis - lastReconnectAttempt > reconnectInterval) {
                lastReconnectAttempt = currentMillis;
                Serial.println("Reconnecting to MQTT Broker...");
                if (connectToMQTT()) {
                    Serial.println("Reconnected to MQTT Broker.");
                } else {
                    Serial.println("Failed to connect to broker");
                }
            }
        }
        mqttClient.loop();

        // Gen 2 - Read HDC1080 and publish to MQTT
        static unsigned long lastAttempt = 0;
        unsigned long now = millis();
        if (now - lastAttempt >= 5000) { // Every 5 seconds
            lastAttempt = now;

            Serial.println("[GEN2] Reading HDC1080 sensor...");
            SensorData data = readHDC1080Data();

            if (data.success) {
                // Publish to MQTT
                sendSensorMessage(data.temperature, data.humidity);

                // Brief blink (LED stays ON when connected, blinks OFF for 100ms)
                digitalWrite(GEN2_LED_PIN, LOW);
                delay(100);
                digitalWrite(GEN2_LED_PIN, HIGH);  // Back to ON
            } else {
                Serial.println("[GEN2] Sensor read failed!");
            }
        }
        delay(100);


    }
    
}
