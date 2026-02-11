// src/utils.cpp
#include "utils.h"
#include "secrets.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ClosedCube_HDC1080.h>
#include <string.h>
#include <stdlib.h>
#include <cmath>
#include <ArduinoJson.h>

// Global Variables
Config storedConfig;
ClosedCube_HDC1080 hdc;

String getPubTopic(){
    String userId = getUserId();
    String deviceId = getDeviceId();

    Serial.println("Getting pub topic String");
    Serial.println(userId);


    String topic = "/toDaemon/" + userId + "/" + deviceId;

    Serial.println(topic);

    return topic;

}

String getSubTopic(){
    String userId = getUserId();
    String deviceId = getDeviceId();

    String topic = "/toDevice/" + userId + "/" + deviceId;

    Serial.println("Getting sub topic String");
    Serial.println(userId);

    Serial.println(topic);


    return topic;

}



const char* mqtt_publish_topic    = getPubTopic().c_str();
const char* mqtt_subscribe_topic  = getSubTopic().c_str();

// Initialize MQTT Client with a plain WiFiClient
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Define the global state variable
bool doesUserExist = false;


// Calculate Checksum for Data Integrity
uint32_t calculateChecksum(const uint8_t* data, size_t length) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        checksum += data[i];
    }
    return checksum;
}

// Save User and WiFi Credentials to EEPROM
bool saveUserAndWifiCreds(const String& ssid, const String& password, const String& uuid, const String& deviceId) {
    // Clear the Config struct
    memset(&storedConfig, 0, sizeof(Config));

    // Copy SSID, password, UUID, and DeviceID into the struct
    ssid.toCharArray(storedConfig.ssid, MAX_SSID_LENGTH);
    password.toCharArray(storedConfig.password, MAX_PASSWORD_LENGTH);
    uuid.toCharArray(storedConfig.uuid, UUID_LENGTH);
    deviceId.toCharArray(storedConfig.deviceId, DEVICEID_LENGTH);

    // Ensure null-termination (just to be safe)
    storedConfig.ssid[MAX_SSID_LENGTH - 1] = '\0';
    storedConfig.password[MAX_PASSWORD_LENGTH - 1] = '\0';
    storedConfig.uuid[UUID_LENGTH - 1] = '\0';
    storedConfig.deviceId[DEVICEID_LENGTH - 1] = '\0';

    // ✅ Verify each field after copying
    if (String(storedConfig.ssid) != ssid) {
        Serial.println("[ERROR] SSID mismatch after copying to storedConfig.");
        return false;
    }

    if (String(storedConfig.password) != password) {
        Serial.println("[ERROR] Password mismatch after copying to storedConfig.");
        return false;
    }

    if (String(storedConfig.uuid) != uuid) {
        Serial.println("[ERROR] UUID mismatch after copying to storedConfig.");
        return false;
    }

    if (String(storedConfig.deviceId) != deviceId) {
        Serial.println("[ERROR] Device ID mismatch after copying to storedConfig.");
        return false;
    }

    Serial.println("[SUCCESS] All fields verified successfully.");

    // Calculate checksum excluding the checksum field itself
    storedConfig.checksum = 0;
    storedConfig.checksum = calculateChecksum(reinterpret_cast<uint8_t*>(&storedConfig), sizeof(Config) - sizeof(uint32_t));

    // Write the Config struct to EEPROM
    for (size_t i = 0; i < sizeof(Config); ++i) {
        EEPROM.write(i, *((uint8_t*)&storedConfig + i));
    }

    // Commit changes to EEPROM
    if (EEPROM.commit()) {
        Serial.println("[EEPROM] Configuration saved successfully.");
        return true;
    } else {
        Serial.println("[EEPROM] Failed to commit changes.");
        return false;
    }
}

// Check for WiFi and User Configuration in EEPROM
bool checkForWifiAndUser() {
    // Read the Config struct from EEPROM
    for (size_t i = 0; i < sizeof(Config); ++i) {
        *((uint8_t*)&storedConfig + i) = EEPROM.read(i);
    }

    // Ensure null-termination for UUID and DeviceID
    storedConfig.uuid[UUID_LENGTH - 1] = '\0';
    storedConfig.deviceId[DEVICEID_LENGTH - 1] = '\0';
    storedConfig.ssid[MAX_SSID_LENGTH - 1] = '\0';
    storedConfig.password[MAX_PASSWORD_LENGTH - 1] = '\0';

    // Calculate checksum of the read data
    uint32_t calculatedChecksum = calculateChecksum(reinterpret_cast<uint8_t*>(&storedConfig), sizeof(Config) - sizeof(uint32_t));

    // Verify checksum
    if (storedConfig.checksum == calculatedChecksum) {
        // Check if all required fields are non-empty
        if (strlen(storedConfig.ssid) == 0 || strlen(storedConfig.password) == 0 ||
            strlen(storedConfig.uuid) == 0 || strlen(storedConfig.deviceId) == 0) {
            Serial.println("[EEPROM] Configuration found, but one or more fields are empty.");
            doesUserExist = false;
            return false;
        }

        Serial.println("[EEPROM] Valid configuration found.");
        Serial.printf("SSID: %s\n", storedConfig.ssid);
        Serial.printf("UUID: %s, Length: %d\n", storedConfig.uuid, strlen(storedConfig.uuid));
        Serial.printf("DeviceID: %s, Length: %d\n", storedConfig.deviceId, strlen(storedConfig.deviceId));

        doesUserExist = true;
        return true;
    } else {
        Serial.println("[EEPROM] Invalid or no configuration found.");
        doesUserExist = false;
        return false;
    }
}


// Send HTTP Response with CORS Headers
void sendResponse(WebServer &server, int statusCode, const String &content) {
    // Debug output
    Serial.printf("[DEBUG] Sending response with status code %d, content: %s\n", statusCode, content.c_str());

    // Set headers for CORS
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

    // Send the response
    server.send(statusCode, "application/json", content);
}

// Flash LED (Gen 2)
void flashLED() {
    digitalWrite(GEN2_LED_PIN, HIGH);  // Turn LED on
    delay(500);
    digitalWrite(GEN2_LED_PIN, LOW);   // Turn LED off
    delay(500);
}

// Connect to WiFi with Given Credentials
bool connectToWiFi(const String& ssid, const String& password) {
    Serial.println("Attempting to connect to WiFi...");

    // Turn on the LED (solid, indicating connection attempt) - Gen 2
    digitalWrite(GEN2_LED_PIN, HIGH);

    // Disconnect any previous connection
    WiFi.disconnect();
    delay(100);

    // Start WiFi connection
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait until connected or timeout (15 seconds)
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
        delay(500);
        Serial.print(".");
        Serial.printf(" Current WiFi.status(): %d\n", WiFi.status());
    }

    // Check connection status
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
        digitalWrite(GEN2_LED_PIN, LOW);  // Gen 2: Turn LED off when connected

        doesUserExist = true;
        return true;
    } else {
        Serial.println("\nFailed to connect to WiFi.");
        Serial.printf("WiFi.status(): %d\n", WiFi.status());

        doesUserExist = false;
        return false;
    }
}

// Clear EEPROM (Use with Caution)
void clearEEPROM() {
    EEPROM.begin(512);
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0xFF);
    }
    EEPROM.commit();
    Serial.println("EEPROM cleared.");
    ESP.restart();
}

void restart(){
    ESP.restart();
}

void sendStatus(){
    Serial.print("online");
}

// MQTT Callback Function to Handle Incoming Messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);

  // Convert payload to a string
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message: ");
  Serial.println(message);

  // Parse the JSON message
  StaticJsonDocument<256> doc; // Adjust buffer size as needed

  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Dispatch based on the "command" field in JSON
  if (doc.containsKey("command")) {
    String command = doc["command"];
    Serial.print("Command received: ");
    Serial.println(command);

    // Map commands to specific function calls
    if (command == "restart") {
      restart();
    } else if (command == "clearEEPROM") {
      clearEEPROM();
    } else if (command == "status") {
      sendStatus();
    } else {
      Serial.println("Unknown command received.");
    }
  } else {
    Serial.println("JSON does not contain 'command' key.");
  }
}

// Connect to MQTT Broker
bool connectToMQTT() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    int randomSessionId = random(1, 201); // Random number from 1 to 200

    String clientId = "ESP8266Client-" + String(WiFi.macAddress()) + "-" + String(storedConfig.deviceId) + "-" + String(randomSessionId);
    String pubTopic = getPubTopic().c_str();
    String subTopic = getSubTopic().c_str();

   

    bool connected = mqttClient.connect(clientId.c_str());

    Serial.println("about to connect to mqtt broker with this topic");
    Serial.println(mqtt_subscribe_topic);

    if (connected) {
        Serial.println("Connected to MQTT Broker.");
        mqttClient.subscribe(getSubTopic().c_str());
        Serial.print("Subscribed to topic: ");
        Serial.println(getSubTopic().c_str());
    

        // mqttClient.publish(topic.c_str(), "ESP8266 Connected");
        return true;
    } else {
        Serial.print("Failed to connect to MQTT Broker, state: ");
        Serial.println(mqttClient.state());
        return false;
    }
}

// Publish Message to MQTT Broker
void publishMessage(const char* topic, const char* message) {

    if (mqttClient.publish(topic, message)) {
        Serial.print("Message published to topic ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(message);
    } else {
        Serial.print("Failed to publish message to topic ");
        Serial.println(topic);
    }
}

// ------------------------------------------------------------
// HDC1080 Sensor Functions (Gen 2)
// ------------------------------------------------------------

bool initHDC1080() {
    Serial.println("[HDC1080] Initializing sensor...");

    // Initialize I2C with custom pins
    Wire.begin(HDC1080_SDA_PIN, HDC1080_SCL_PIN);

    // Initialize sensor (ClosedCube library)
    hdc.begin(HDC1080_I2C_ADDR);

    // Read manufacturer ID to verify sensor connection
    uint16_t manufacturerID = hdc.readManufacturerId();
    if (manufacturerID != 0x5449) {  // TI manufacturer ID
        Serial.println("[HDC1080] ERROR: Failed to communicate with HDC1080!");
        Serial.print("[HDC1080] Expected manufacturer ID: 0x5449, Got: 0x");
        Serial.println(manufacturerID, HEX);
        Serial.println("[HDC1080] Check wiring and I2C address (should be 0x40)");
        return false;
    }

    Serial.println("[HDC1080] Sensor initialized successfully");
    Serial.print("[HDC1080] I2C Address: 0x");
    Serial.println(HDC1080_I2C_ADDR, HEX);
    Serial.print("[HDC1080] Manufacturer ID: 0x");
    Serial.println(manufacturerID, HEX);

    return true;
}

SensorData readHDC1080Data() {
    SensorData data;
    data.success = false;

    // Read temperature and humidity (ClosedCube library)
    float temperature = hdc.readTemperature();
    float humidity = hdc.readHumidity();

    // Check for valid readings
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("[HDC1080] ERROR: Failed to read sensor data");
        return data;
    }

    // Populate sensor data
    data.temperature = temperature;
    data.humidity = humidity;
    data.success = true;

    // Log readings
    REPORT_TEMPHUM(data.temperature, data.humidity);

    return data;
}

void testHDC1080() {
    Serial.println("\n[HDC1080] Running sensor test...");

    // Test I2C communication
    Wire.beginTransmission(HDC1080_I2C_ADDR);
    byte error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("[HDC1080] I2C communication OK");
    } else {
        Serial.print("[HDC1080] I2C error code: ");
        Serial.println(error);
        return;
    }

    // Read and display data
    SensorData data = readHDC1080Data();
    if (data.success) {
        Serial.println("[HDC1080] Test PASSED");
        Serial.printf("  Temperature: %.2f°C\n", data.temperature);
        Serial.printf("  Humidity: %.2f%%\n", data.humidity);
    } else {
        Serial.println("[HDC1080] Test FAILED");
    }
}

// Read Sensor Data (Gen 2 - HDC1080)
SensorData readSensorData() {
    return readHDC1080Data();  // Delegate to HDC1080 function
}



// Send Heartbeat via I²C
void sendHeartbeat() {
    Serial.println("[DEBUG] Sending heartbeat signal to STM via I²C...");

    // Trigger START condition and check response
    Wire.beginTransmission(0x18); // STM32 I²C Address (try 0x08, 0x18, etc.)
    Wire.write("HEARTBEAT");
    byte error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("[INFO] Heartbeat signal sent successfully via I²C!");
    } else {
        Serial.print("[ERROR] Failed to send heartbeat, error code: ");
        Serial.println(error);
    }
}

// Wake Up STM32 via GPIO
void wakeUpSTM32() {
    Serial.println("[DEBUG] Triggering wake-up pin...");

    pinMode(16, OUTPUT); // Example GPIO pin, adjust if needed
    digitalWrite(16, HIGH);
    delay(100);  // Hold high for 100ms
    digitalWrite(16, LOW);

    Serial.println("[INFO] Wake-up signal sent via GPIO!");
}

// Find I²C Address
uint8_t findI2CAddress() {
    Serial.println("[DEBUG] Scanning I²C bus for devices...");

    uint8_t foundAddress = 0x00; // Default to 0x00 (no device found)
    byte error;
    int nDevices = 0;

    // Loop through all possible I²C addresses (1 to 127)
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("[INFO] I²C device found at address 0x");
            Serial.println(address, HEX);
            foundAddress = address;
            nDevices++;
        } else if (error == 4) {
            Serial.print("[ERROR] Unknown error at address 0x");
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0) {
        Serial.println("[WARNING] No I²C devices found on the bus.");
    } else {
        Serial.print("[INFO] Total I²C devices found: ");
        Serial.println(nDevices);
    }

    return foundAddress;
}

// Set HTS221 Thresholds Manually
void setHTS221ThresholdsManual() {
    Serial.println("[DEBUG] Configuring HTS221 thresholds manually...");

    Wire.beginTransmission(0x5F); // HTS221 default address

    // Write humidity threshold
    Wire.write(0x33); // Humidity high threshold register
    Wire.write(50);   // Example: 50% threshold

    // Write temperature threshold
    Wire.write(0x34); // Temperature high threshold register
    Wire.write(30);   // Example: 30°C threshold

    byte error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("[INFO] HTS221 thresholds configured successfully!");
    } else {
        Serial.print("[ERROR] Failed to set thresholds, error code: ");
        Serial.println(error);
    }
}

// Scan I²C Devices
void scanI2CDevices() {
    Serial.println("[DEBUG] Scanning I²C bus for devices...");

    int nDevices = 0;

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.print("[INFO] Found I²C device at address 0x");
            Serial.println(address, HEX);

            // dumpI2CRegisters(address); // Dump registers for this device
            nDevices++;
        }
    }

    if (nDevices == 0) {
        Serial.println("[WARNING] No I²C devices found on the bus.");
    } else {
        Serial.print("[INFO] Total I²C devices found: ");
        Serial.println(nDevices);
    }
}

// Test I²C Bus Health
void testI2CBusHealth() {
    Wire.beginTransmission(0x5F);
    byte error = Wire.endTransmission();

    if (error == 0) {
        Serial.println("[INFO] HTS221 detected successfully on I²C bus!");
    } else {
        Serial.print("[ERROR] I²C communication error: ");
        Serial.println(error);
    }
}

// Force HTS221 Initialization
void forceHTS221Init() {
    Wire.beginTransmission(0x5F);
    Wire.write(0x20); // CTRL_REG1
    Wire.write(0x85); // Enable sensor, 1 Hz mode
    Wire.endTransmission();
    delay(100);

    Wire.beginTransmission(0x5F);
    Wire.write(0x21); // CTRL_REG2
    Wire.write(0x01); // Trigger one-shot mode
    Wire.endTransmission();

    Serial.println("[INFO] HTS221 Initialization Commands Sent");
}

void readHTS221Raw() {
  Wire.beginTransmission(0x5F); // HTS221 Address
  Wire.write(0x28 | 0x80); // Humidity register (0x28) with auto-increment (0x80)
  Wire.endTransmission();
  Wire.requestFrom(0x5F, 4);

  if (Wire.available() == 4) {
      uint8_t humL = Wire.read(); // Humidity low byte
      uint8_t humH = Wire.read(); // Humidity high byte
      uint8_t tempL = Wire.read(); // Temperature low byte
      uint8_t tempH = Wire.read(); // Temperature high byte

      float humidity = ((humH << 8) | humL) / 65536.0 * 100.0;
      float temperature = ((tempH << 8) | tempL) / 65536.0 * 120.0 - 40.0;

      Serial.print("Raw Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      Serial.print("Raw Temperature: ");
      Serial.print(temperature);
      Serial.println(" °C");
  } else {
      Serial.println("[ERROR] Failed to read raw HTS221 data!");
  }
}

void dumpI2CRegisters(uint8_t deviceAddress) {
  Serial.print("[INFO] Dumping registers for device at 0x");
  Serial.println(deviceAddress, HEX);

  for (uint8_t reg = 0x00; reg <= 0xFF; reg++) {
    Wire.beginTransmission(deviceAddress);
    Wire.write(reg); // Select register
    if (Wire.endTransmission() == 0) {
      Wire.requestFrom(deviceAddress, (uint8_t)1);

      if (Wire.available()) {
        uint8_t value = Wire.read();
        Serial.print("Register 0x");
        Serial.print(reg, HEX);
        Serial.print(": 0x");
        Serial.println(value, HEX);
      } else {
        Serial.print("Register 0x");
        Serial.print(reg, HEX);
        Serial.println(": [No Data]");
      }
    } else {
      Serial.print("Register 0x");
      Serial.print(reg, HEX);
      Serial.println(": [Write Error]");
    }
  }
  Serial.println("[INFO] Register dump complete.\n");
}

void scanI2C(uint8_t sda, uint8_t scl) {
  Serial.print("[DEBUG] Scanning I²C bus on SDA: ");
  Serial.print(sda);
  Serial.print(", SCL: ");
  Serial.println(scl);

  Wire.begin(sda, scl);
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("[INFO] Found device at 0x");
      Serial.println(address, HEX);
    }
  }
}

// Run Full Diagnostics
void runFullDiagnostics() {
    Serial.println("\n🔍 [DEBUG] Starting Full System Diagnostics...\n");
    
    // ---------- GPIO Pin Probing ----------
    Serial.println("🟢 [GPIO] Testing All GPIO Pins...");
    int gpioPins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
    int pinCount = sizeof(gpioPins) / sizeof(gpioPins[0]);

    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        
        Serial.printf("\n🟢 [GPIO TEST] Testing GPIO%d...\n", pin);
        pinMode(pin, OUTPUT);

        // Step 1: Set GPIO HIGH
        Serial.printf("[GPIO%d] Setting HIGH...\n", pin);
        digitalWrite(pin, HIGH);
        delay(2000); // Wait 2 seconds to observe behavior

        // Step 2: Set GPIO LOW
        Serial.printf("[GPIO%d] Setting LOW...\n", pin);
        digitalWrite(pin, LOW);
        delay(2000); // Wait 2 seconds to observe behavior

        // Step 3: Pulse GPIO (HIGH -> LOW -> HIGH)
        Serial.printf("[GPIO%d] Pulsing HIGH-LOW-HIGH...\n", pin);
        digitalWrite(pin, HIGH);
        delay(500);
        digitalWrite(pin, LOW);
        delay(500);
        digitalWrite(pin, HIGH);
        delay(2000); // Wait 2 seconds to observe behavior

        Serial.printf("✅ [GPIO%d] Test Complete. Moving to next pin...\n", pin);

        // Clear pin state
        digitalWrite(pin, LOW);
        pinMode(pin, INPUT);
    }
    Serial.println("✅ [GPIO] GPIO Test Complete.\n");

    // ---------- I²C Bus Health ----------
    Serial.println("🟢 [I²C] Testing I²C Bus Health...");
    Wire.begin(ALT_SDA_PIN, ALT_SCL_PIN);
    Wire.beginTransmission(0x5F); // HTS221 Address
    byte error = Wire.endTransmission();
    if (error == 0) {
        Serial.println("✅ [I²C] HTS221 detected successfully on alternate pins!");
    } else {
        Serial.printf("❌ [I²C] HTS221 I²C communication error: %d\n", error);
    }

    // ---------- I²C Register Dump ----------
    Serial.println("🟢 [I²C] Dumping All Registers from 0x00 to 0xFF...");
    for (uint8_t reg = 0x00; reg <= 0xFF; reg++) {
        Wire.beginTransmission(0x5F);
        Wire.write(reg);
        Wire.endTransmission();
        Wire.requestFrom(0x5F, 1);

        if (Wire.available()) {
            uint8_t value = Wire.read();
            Serial.printf("[I²C] Register 0x%02X: 0x%02X\n", reg, value);
        } else {
            Serial.printf("❌ [I²C] No Response from Register 0x%02X\n", reg);
        }
    }
    Serial.println("✅ [I²C] I²C Register Dump Complete.\n");

    // ---------- I²C Write Test ----------
    Serial.println("🟢 [I²C] Attempting to Write to Registers...");
    for (uint8_t reg = 0x00; reg <= 0xFF; reg++) {
        Wire.beginTransmission(0x5F);
        Wire.write(reg);
        Wire.write(0xFF); // Arbitrary write value
        if (Wire.endTransmission() == 0) {
            Serial.printf("✅ [I²C] Wrote to Register 0x%02X\n", reg);
        } else {
            Serial.printf("❌ [I²C] Failed to Write to Register 0x%02X\n", reg);
        }
        delay(50); // Prevent I²C flood
    }
    Serial.println("✅ [I²C] I²C Write Test Complete.\n");

    // ---------- UART Sniffing ----------
    Serial.println("🟢 [UART] Sniffing UART Communication...");
    unsigned long uartStartTime = millis();
    while (millis() - uartStartTime < 5000) { // 5-second UART Sniffing
        if (Serial.available()) {
            char c = Serial.read();
            Serial.print(c);
        }
    }
    Serial.println("\n✅ [UART] UART Sniffing Complete.\n");

    // ---------- UART Fuzzing ----------
    Serial.println("🟢 [UART] Fuzzing UART Communication...");
    for (uint8_t command = UART_MIN; command <= UART_MAX; command++) {
        Serial.write(command);
        delay(50);
        Serial.printf("[UART] Sent Command: 0x%02X\n", command);
    }
    Serial.println("✅ [UART] UART Fuzzing Complete.\n");

    // ---------- GPIO Wake Behavior ----------
    Serial.println("🟢 [GPIO] Observing GPIO Wake-Up Behavior...");
    pinMode(16, INPUT);
    if (digitalRead(16) == HIGH) {
        Serial.println("✅ [GPIO] GPIO16 detected HIGH (Wake-Up Triggered)");
    } else {
        Serial.println("❌ [GPIO] GPIO16 not triggered.");
    }

    Serial.println("\n✅ [DEBUG] Full Diagnostics Complete!");
}

// Test All GPIO Pins
void testAllGPIOPins() {
    Serial.println("\n🔍 [TEST] Starting Systematic GPIO Test...");

    // List of GPIOs to test (excluding TX/RX and reserved pins)
    int gpioPins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
    int pinCount = sizeof(gpioPins) / sizeof(gpioPins[0]);

    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        
        Serial.printf("\n🟢 [GPIO TEST] Testing GPIO%d...\n", pin);
        pinMode(pin, OUTPUT);

        // Step 1: Set GPIO HIGH
        Serial.printf("[GPIO%d] Setting HIGH...\n", pin);
        digitalWrite(pin, HIGH);
        delay(2000); // Wait 2 seconds to observe behavior

        // Step 2: Set GPIO LOW
        Serial.printf("[GPIO%d] Setting LOW...\n", pin);
        digitalWrite(pin, LOW);
        delay(2000); // Wait 2 seconds to observe behavior

        // Step 3: Pulse GPIO (HIGH -> LOW -> HIGH)
        Serial.printf("[GPIO%d] Pulsing HIGH-LOW-HIGH...\n", pin);
        digitalWrite(pin, HIGH);
        delay(500);
        digitalWrite(pin, LOW);
        delay(500);
        digitalWrite(pin, HIGH);
        delay(2000); // Wait 2 seconds to observe behavior

        Serial.printf("✅ [GPIO%d] Test Complete. Moving to next pin...\n", pin);

        // Clear pin state
        digitalWrite(pin, LOW);
        pinMode(pin, INPUT);
    }

    Serial.println("\n✅ [TEST COMPLETE] All GPIO pins tested systematically.\n");
}

// Read All GPIO Pins
void readAllGPIOPins() {
    Serial.println("\n🔍 [TEST] Starting GPIO Pin Listening Test...");

    // List of GPIOs to test (excluding TX/RX and reserved pins)
    int gpioPins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
    int pinCount = sizeof(gpioPins) / sizeof(gpioPins[0]);

    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        
        Serial.printf("\n🟢 [GPIO READ] Listening on GPIO%d...\n", pin);
        pinMode(pin, INPUT);
        
        for (int j = 0; j < 10; j++) { // Read each pin 10 times
            int state = digitalRead(pin);
            Serial.printf("[GPIO%d] State: %s\n", pin, state == HIGH ? "HIGH" : "LOW");
            delay(500); // Half-second delay to observe pin state
        }
        
        Serial.printf("✅ [GPIO%d] Listening Complete. Moving to next pin...\n", pin);
    }

    Serial.println("\n✅ [TEST COMPLETE] All GPIO pins listened systematically.\n");
}

// Pulse Low GPIO Pins
void pulseLowGPIOPins() {
    Serial.println("\n🔍 [ULTIMATE TEST] Beginning Full GPIO, UART, and I²C Exploration...");

    // List of GPIOs to test (excluding TX/RX and reserved pins)
    int gpioPins[] = {0, 2, 4, 5, 12, 13, 14, 15, 16};
    int pinCount = sizeof(gpioPins) / sizeof(gpioPins[0]);

    // 🟢 STEP 1: Flick each GPIO pin ON and OFF
    Serial.println("\n🟢 [STEP 1] Flicking GPIO Pins ON and OFF...");
    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        Serial.printf("[GPIO%d] Flicking ON and OFF...\n", pin);
        pinMode(pin, OUTPUT);
        for (int j = 0; j < 3; j++) {
            digitalWrite(pin, HIGH);
            delay(500);
            digitalWrite(pin, LOW);
            delay(500);
        }
        pinMode(pin, INPUT);
        delay(500);
    }

    // 🟢 STEP 2: Pulse Each GPIO Pin
    Serial.println("\n🟢 [STEP 2] Pulsing GPIO Pins...");
    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        Serial.printf("[GPIO%d] Pulsing HIGH-LOW-HIGH...\n", pin);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        delay(500);
        digitalWrite(pin, LOW);
        delay(500);
        digitalWrite(pin, HIGH);
        delay(500);
        pinMode(pin, INPUT);
        delay(500);
    }

    // 🟢 STEP 3: Monitor UART During Pin Tests
    Serial.println("\n🟢 [STEP 3] Monitoring UART During GPIO Tests...");
    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        Serial.printf("[GPIO%d] Pulsing with UART Monitoring...\n", pin);
        pinMode(pin, OUTPUT);

        digitalWrite(pin, HIGH);
        delay(500);
        digitalWrite(pin, LOW);
        delay(500);
        digitalWrite(pin, HIGH);
        delay(500);

        Serial.println("[UART] Monitoring UART for 3 seconds...");
        unsigned long startMillis = millis();
        while (millis() - startMillis < 3000) {
            if (Serial.available()) {
                char c = Serial.read();
                Serial.print(c);
            }
        }

        pinMode(pin, INPUT);
        delay(500);
    }

    // 🟢 STEP 4: Monitor I²C During GPIO Tests
    Serial.println("\n🟢 [STEP 4] Monitoring I²C During GPIO Tests...");
    Wire.begin(); // Ensure I2C is initialized
    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        Serial.printf("[GPIO%d] Pulsing with I²C Monitoring...\n", pin);
        pinMode(pin, OUTPUT);

        digitalWrite(pin, HIGH);
        delay(500);
        digitalWrite(pin, LOW);
        delay(500);
        digitalWrite(pin, HIGH);
        delay(500);

        Serial.println("[I²C] Scanning for devices...");
        for (uint8_t address = 1; address < 127; address++) {
            Wire.beginTransmission(address);
            byte error = Wire.endTransmission();
            if (error == 0) {
                Serial.printf("[I²C] Device found at address 0x%X\n", address);
            }
        }

        pinMode(pin, INPUT);
        delay(500);
    }

    // 🟢 STEP 5: Test GPIO Pin Combinations
    Serial.println("\n🟢 [STEP 5] Testing GPIO Pin Combinations...");
    for (int i = 0; i < pinCount; i++) {
        for (int j = i + 1; j < pinCount; j++) {
            int pinA = gpioPins[i];
            int pinB = gpioPins[j];

            Serial.printf("[COMBO] Pulsing GPIO%d & GPIO%d...\n", pinA, pinB);
            pinMode(pinA, OUTPUT);
            pinMode(pinB, OUTPUT);

            digitalWrite(pinA, HIGH);
            digitalWrite(pinB, HIGH);
            delay(500);
            digitalWrite(pinA, LOW);
            digitalWrite(pinB, LOW);
            delay(500);
            digitalWrite(pinA, HIGH);
            digitalWrite(pinB, HIGH);
            delay(500);

            Serial.println("[COMBO] Monitoring UART for 3 seconds...");
            unsigned long startMillis = millis();
            while (millis() - startMillis < 3000) {
                if (Serial.available()) {
                    char c = Serial.read();
                    Serial.print(c);
                }
            }

            pinMode(pinA, INPUT);
            pinMode(pinB, INPUT);
            delay(500);
        }
    }

    // 🟢 STEP 6: Test GPIO with Different Durations
    Serial.println("\n🟢 [STEP 6] Testing GPIO Durations...");
    for (int i = 0; i < pinCount; i++) {
        int pin = gpioPins[i];
        Serial.printf("[DURATION] Holding GPIO%d HIGH for 5 seconds...\n", pin);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        delay(5000);
        digitalWrite(pin, LOW);
        delay(1000);
        pinMode(pin, INPUT);
        delay(500);
    }

    Serial.println("\n✅ [ULTIMATE TEST COMPLETE] All GPIO, UART, and I²C tests completed.\n");
}

// Simulate Button Press on GPIO5
void simulateButtonPress() {
    Serial.println("[TEST] Simulating Button Press on GPIO5...");
    pinMode(5, OUTPUT);
    digitalWrite(5, LOW); delay(100);
    digitalWrite(5, HIGH); delay(500);
    digitalWrite(5, LOW); delay(100);
    Serial.println("[TEST] Button Press Simulation Complete");
}

// Advanced Button Sequence
void advancedButtonSequence() {
    Serial.println("[TEST] Testing Advanced GPIO5 Sequences...");
    pinMode(5, OUTPUT);

    // Quick toggle
    for (int i = 0; i < 5; i++) {
        digitalWrite(5, HIGH); delay(100);
        digitalWrite(5, LOW); delay(100);
    }

    // Long hold
    digitalWrite(5, HIGH); delay(3000); // 3-second hold
    digitalWrite(5, LOW); delay(1000);

    // Combo toggle with GPIO4
    pinMode(4, OUTPUT);
    digitalWrite(5, HIGH); digitalWrite(4, HIGH); delay(500);
    digitalWrite(5, LOW); digitalWrite(4, LOW); delay(500);

    Serial.println("[TEST] Advanced Sequence Complete");
}

// GPIO5 with Wake-Up
void gpio5WithWakeUp() {
    Serial.println("[TEST] Testing GPIO5 + GPIO16 Wake-Up Combo...");
    pinMode(5, OUTPUT);
    pinMode(16, OUTPUT);

    digitalWrite(5, HIGH); delay(200);
    digitalWrite(16, HIGH); delay(200);
    digitalWrite(5, LOW); delay(200);
    digitalWrite(16, LOW); delay(200);

    Serial.println("[TEST] Combo Complete");
}

// Brute Force I²C Registers
void bruteForceI2CRegisters(uint8_t deviceAddress) {
    Serial.printf("[I²C] Brute-Forcing Registers on 0x%02X...\n", deviceAddress);
    for (uint8_t reg = 0x00; reg <= 0xFF; reg++) {
        Wire.beginTransmission(deviceAddress);
        Wire.write(reg);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom(deviceAddress, (uint8_t)1);
            if (Wire.available()) {
                uint8_t value = Wire.read();
                Serial.printf("[I²C] Register 0x%02X: 0x%02X\n", reg, value);
            }
        }
    }
}


String getDeviceId() {
    // Gen 2: Use in-memory storedConfig (set by hardcoded credentials)
    // Check if storedConfig has a valid deviceId first
    if (storedConfig.deviceId[0] != '\0') {
        return String(storedConfig.deviceId);
    }

    // Fallback: Try to read from EEPROM    // Temporary storage for the Config struct
    Config tempConfig;

    // Read data from EEPROM into tempConfig
    for (size_t i = 0; i < sizeof(Config); ++i) {
        *((uint8_t*)&tempConfig + i) = EEPROM.read(i);
    }

    // Ensure null-termination for DeviceID
    tempConfig.deviceId[DEVICEID_LENGTH - 1] = '\0';

    // Calculate checksum
    uint32_t calculatedChecksum = calculateChecksum(reinterpret_cast<uint8_t*>(&tempConfig), sizeof(Config) - sizeof(uint32_t));

    // Verify checksum
    if (tempConfig.checksum == calculatedChecksum) {
        if (strlen(tempConfig.deviceId) == 0) {
            Serial.println("[EEPROM] DeviceID field is empty.");
            return String(""); // Return empty string if DeviceID is empty
        }

        return String(tempConfig.deviceId);
    }

    Serial.println("[EEPROM] Invalid configuration or checksum mismatch while retrieving DeviceID.");
    return String(""); // Return empty string if checksum fails
}



String getUserId() {
    // Gen 2: Use in-memory storedConfig (set by hardcoded credentials)
    // Check if storedConfig has a valid uuid first
    if (storedConfig.uuid[0] != '\0') {
        return String(storedConfig.uuid);
    }

    // Fallback: Try to read from EEPROM
    // Temporary storage for the Config struct
    Config tempConfig;

    // Read data from EEPROM into tempConfig
    for (size_t i = 0; i < sizeof(Config); ++i) {
        *((uint8_t*)&tempConfig + i) = EEPROM.read(i);
    }

    // Ensure null-termination for UUID
    tempConfig.uuid[UUID_LENGTH - 1] = '\0';

    // Calculate checksum
    uint32_t calculatedChecksum = calculateChecksum(reinterpret_cast<uint8_t*>(&tempConfig), sizeof(Config) - sizeof(uint32_t));

    // Verify checksum
    if (tempConfig.checksum == calculatedChecksum) {
        if (strlen(tempConfig.uuid) == 0) {
            Serial.println("[EEPROM] UUID field is empty.");
            return String(""); // Return empty string if UUID is empty
        }

        return String(tempConfig.uuid);
    }

    Serial.println("[EEPROM] Invalid configuration or checksum mismatch while retrieving UUID.");
    return String(""); // Return empty string if checksum fails
}


void sendSensorMessage(float temperature, float humidity) {

    // Retrieve necessary IDs
    String userId = getUserId();
    String deviceId = getDeviceId();
    String status = "online";
    String deviceType = "sensor";

    // Create a JSON document
    StaticJsonDocument<256> doc;

    // Populate required fields
    doc["device_id"] = deviceId;
    doc["user_id"] = userId;
    doc["status"] = status;
    doc["device_type"] = deviceType;

    // Populate optional fields if valid
    if (!isnan(temperature)) {
        doc["temp_sensor_reading"] = temperature;
    }

    if (!isnan(humidity)) {
        doc["humid_sensor_reading"] = humidity;
    }

    // Serialize JSON to a String
    String jsonResponse;
    serializeJson(doc, jsonResponse);


    if(mqttClient.connected()){

        if(!mqttClient.subscribe(getSubTopic().c_str())){
            Serial.println("Error Subscribing for some reason");
        }

        if(!mqttClient.publish(getPubTopic().c_str(), jsonResponse.c_str())){
            Serial.println("Error publishing for some reason");
            Serial.print("[ERROR] MQTT Publish Failed, State: ");
            Serial.println(mqttClient.state());

        }
    } else{
        Serial.print("Unable to reconnect to mqtt");
    }

    Serial.println("[MQTT] Payload published successfully");
}

