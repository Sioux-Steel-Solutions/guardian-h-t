// utils.h
#ifndef UTILS_H
#define UTILS_H

#include <WebServer.h>
#include <PubSubClient.h>
#include <ClosedCube_HDC1080.h>
#include <Wire.h>
#include <string.h>   // for memcpy
#include <stdlib.h>

#define MQTT_MAX_PACKET_SIZE 512

// Constants
#define MAX_SSID_LENGTH     32
#define MAX_PASSWORD_LENGTH 64
#define UUID_LENGTH         37
#define DEVICEID_LENGTH     37

// Gen 2 Hardware - ESP32-C3 + HDC1080
#define GEN2_LED_PIN    1   // GPIO1 with 330Ω resistor

// HDC1080 I2C Configuration
#define HDC1080_SDA_PIN 5   // GPIO5 (with 4.7k pull-up R5)
#define HDC1080_SCL_PIN 4   // GPIO4 (with 4.7k pull-up R6)
#define HDC1080_I2C_ADDR 0x40

// Legacy pin definitions (for unused diagnostic functions)
#define ALT_SDA_PIN 12
#define ALT_SCL_PIN 14
#define UART_MIN 0x00
#define UART_MAX 0xFF

// Temporary hardcoded credentials for Gen 2 testing
#define GEN2_TEST_SSID "NBSJK"
#define GEN2_TEST_PASSWORD "12345679"
#define GEN2_TEST_USERID "test-user-id-123"
#define GEN2_TEST_DEVICEID "test-device-id-456"
struct Config {
  char ssid[MAX_SSID_LENGTH];
  char password[MAX_PASSWORD_LENGTH];
  char uuid[UUID_LENGTH];
  char deviceId[DEVICEID_LENGTH];
  uint32_t checksum; // For data integrity
};

// External Variables
extern Config storedConfig;
extern PubSubClient mqttClient;

// MQTT Topics
extern const char* mqtt_publish_topic;
extern const char* mqtt_subscribe_topic;

extern bool doesUserExist;

// Some debug placeholders
#define UNDEFINED -999.0

// Minimal logging: Only report temperature and humidity
#define REPORT_TEMPHUM(t, h) Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%\n", t, h)

// Structure to hold sensor data
struct SensorData {
    float temperature;
    float humidity;
    bool success;
};

// Function Prototypes
void sendResponse(WebServer &server, int statusCode, const String &content);
void flashLED();
bool connectToWiFi(const String& ssid, const String& password);
bool saveUserAndWifiCreds(const String& ssid, const String& password, const String& uuid, const String& deviceId);
bool checkForWifiAndUser();
uint32_t calculateChecksum(const uint8_t* data, size_t length);
void clearEEPROM();

// MQTT Function Prototypes
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool connectToMQTT();
void publishMessage(const char* topic, const char* message);

// Sensor and I²C Function Prototypes
SensorData readSensorData();
void sendHeartbeat();
uint8_t findI2CAddress();
void wakeUpSTM32();
void setHTS221ThresholdsManual();
void scanI2CDevices();
void scanI2C(uint8_t sda, uint8_t scl);
void readHTS221Raw();
void testI2CBusHealth();
void forceHTS221Init();
void runFullDiagnostics();
void testAllGPIOPins();
void readAllGPIOPins();
void pulseLowGPIOPins();
void advancedButtonSequence();
void gpio5WithWakeUp();
void bruteForceI2CRegisters(uint8_t deviceAddress);

// HDC1080 Sensor Function Prototypes (Gen 2)
bool initHDC1080();
SensorData readHDC1080Data();
void testHDC1080();
String getDeviceId();
void sendSensorMessage(float temperature, float humidity);
String getPubTopic();
String getSubTopic();
String getUserId();


void sensorLoop();
#endif // UTILS_H
