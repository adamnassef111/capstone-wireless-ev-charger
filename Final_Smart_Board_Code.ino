#include <Wire.h>                  // I2C communication for MCP9808
#include "Adafruit_MCP9808.h"      // MCP9808 temperature sensor library
#include <Adafruit_GFX.h>          // Graphics library for TFT display
#include <Adafruit_ST7789.h>       // ST7789 display library
#include <SPI.h>                   // SPI communication for TFT display
#include <WiFi.h>                  // ESP32 Wi-Fi library
#include <WebServer.h>             // Simple web server library

// Create MCP9808 temperature sensor object
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();

// ---------- Wi-Fi Credentials ----------
const char* homeSsid = "aquapine";          // Wi-Fi network name
const char* homePassword = "Removed";  // Wi-Fi password removed for my security

// ---------- Wi-Fi Option 2: iPhone Hotspot ----------
const char* hotspotSsid = "Adam's iphone";
const char* hotspotPassword = "Removed"; //password removed for my security

// ---------- Wi-Fi Option 3: Carleton CU-Wireless ----------
const char* cuSsid = "CU-Wireless";
const char* cuUsername = "adamnassef";
const char* cuPassword = "Removed"; //password removed for my security

// Stores the name of the network that connected successfully
String connectedNetwork = "None";

// ---------- Function: Connect to home Wi-Fi ----------
// Tries to connect to the saved home network.
// Returns true if the connection is successful.
bool connectToHomeWiFi() {
  Serial.print("Connecting to Home Wi-Fi");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(homeSsid, homePassword);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedNetwork = "Home Wi-Fi";
    return true;
  }

  return false;
}

// ---------- Function: Connect to iPhone hotspot ----------
// Tries to connect to the saved phone hotspot.
// Returns true if the connection is successful.
bool connectToHotspot() {
  Serial.print("Connecting to iPhone Hotspot");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(hotspotSsid, hotspotPassword);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedNetwork = "iPhone Hotspot";
    return true;
  }

  return false;
}

// ---------- Function: Connect to Carleton CU-Wireless ----------
// Tries to connect to the school enterprise Wi-Fi network using
// WPA2-Enterprise PEAP authentication.
// Returns true if the connection is successful.
bool connectToCUWireless() {
#if defined(CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT)
  Serial.print("Connecting to CU-Wireless");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(500);

  WiFi.begin(cuSsid, WPA2_AUTH_PEAP, cuUsername, cuUsername, cuPassword);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedNetwork = "CU-Wireless";
    return true;
  }

  return false;
#else
  Serial.println("CU-Wireless skipped: WPA2-Enterprise not enabled");
  return false;
#endif
}

// ---------- Function: Connect to any available Wi-Fi ----------
// Tries the home network first, then the iPhone hotspot,
// then CU-Wireless.
// Returns true as soon as one connection succeeds.
bool connectToAnyWiFi() {
  if (connectToHomeWiFi()) {
    return true;
  }

  if (connectToHotspot()) {
    return true;
  }

  if (connectToCUWireless()) {
    return true;
  }

  connectedNetwork = "None";
  return false;
}

// Create web server object on port 80
WebServer server(80);

// ---------- ST7789 Display Pins ----------
#define TFT_CS   5                 // TFT chip select pin
#define TFT_DC   2                 // TFT data/command pin
#define TFT_RST  4                 // TFT reset pin
#define TFT_BL   14                // TFT backlight control pin

// Create ST7789 display object
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ---------- ESP32 Pin Assignments ----------
const int currentPin = 34;         // ACS712 analog output pin
const int voltagePin = 35;         // Voltage divider analog output pin
const int relayPin   = 15;         // Relay fault-control pin
const int ledPin     = 13;         // Indicator LED pin

// ---------- Temperature Sensor Status ----------
bool tempSensorAvailable = false;  // True if MCP9808 is detected and available

// ---------- ACS712 Current Sensor ----------
const float currentSensitivity = 0.185;   // ACS712-05B sensitivity in V/A
float zeroCurrentVoltage = 0.0;           // Zero-current offset measured during calibration

// ---------- Voltage Divider Values ----------
const float R1 = 120000.0;         // Upper resistor: 100k + 10k + 10k in series
const float R2 = 10000.0;          // Lower resistor: 10k

// ---------- Protection Thresholds ----------
const float underVoltageLimit = 16.7;     // Undervoltage threshold in volts
const float overVoltageLimit  = 22.0;     // Overvoltage threshold in volts
const float overCurrentLimit  = 0.100;     // Overcurrent threshold in amps
const float overTempLimit     = 28.0;     // Overtemperature threshold in degrees C

// ---------- Display Refresh Timing ----------
unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 500; // Refresh LCD every 500 ms

// ---------- Latest Values for Web View ----------
float latestTempC = 0.0;           // Latest measured temperature
float latestCurrentA = 0.0;        // Latest measured current
float latestVoltageV = 0.0;        // Latest measured voltage
String latestFaultMsg = "System Normal"; // Latest system status text
bool latestHealthy = true;         // Latest health state

// ---------- Function: Read averaged ADC voltage ----------
// Averages multiple ADC samples to reduce reading noise.
float readAverageADCVoltage(int pin, int samples) {
  long total = 0;

  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(2);
  }

  float avgRaw = total / (float)samples;
  return (avgRaw / 4095.0) * 3.3;   // Convert ADC count to voltage
}

// ---------- Function: Read actual system voltage ----------
// Reads the divider output and reconstructs the original system voltage.
float readSystemVoltage() {
  float vAdc = readAverageADCVoltage(voltagePin, 200);
  float vIn = vAdc * ((R1 + R2) / R2);
  return vIn;
}

// ---------- Function: Read current in amps ----------
// Reads ACS712 output voltage and subtracts the zero-current offset.
float readCurrentA() {
  float sensorVoltage = readAverageADCVoltage(currentPin, 200);
  float current = (sensorVoltage - zeroCurrentVoltage) / currentSensitivity;
  return current;
}

// ---------- Function: Calibrate current sensor ----------
// Measures ACS712 output when no current is flowing and stores the offset.
void calibrateCurrentSensor() {
  Serial.println("Calibrating ACS712... keep current OFF");
  delay(1000);

  float sum = 0.0;
  const int count = 200;

  for (int i = 0; i < count; i++) {
    sum += readAverageADCVoltage(currentPin, 10);
  }

  zeroCurrentVoltage = sum / count;

  Serial.print("Zero-current voltage = ");
  Serial.print(zeroCurrentVoltage, 4);
  Serial.println(" V");
}

// ---------- Function: Set relay state ----------
// Healthy condition: pin does nothing
// Fault condition: pin actively pulls LOW
void setRelayState(bool healthy) {
  if (healthy) {
    pinMode(relayPin, INPUT);
  } else {
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW);
  }
}

// ---------- Function: Set indicator LED ----------
// LED is ON during healthy operation and OFF during a fault.
void setLedState(bool healthy) {
  digitalWrite(ledPin, healthy ? HIGH : LOW);
}

// ---------- Function: Build fault message ----------
// Returns a full message for a single fault.
// Returns a compact combined code if multiple faults occur together.
String buildFaultMessage(bool uvFault, bool ovFault, bool ocFault, bool otFault) {
  int faultCount = 0;
  if (uvFault) faultCount++;
  if (ovFault) faultCount++;
  if (ocFault) faultCount++;
  if (otFault) faultCount++;

  if (faultCount == 0) {
    return "System Normal";
  }

  if (faultCount == 1) {
    if (uvFault) return "Fault: Undervoltage";
    if (ovFault) return "Fault: Overvoltage";
    if (otFault) return "Fault: Overtemp";
    if (ocFault) return "Fault: Overcurrent";
  }

  String msg = "Fault: ";
  bool first = true;

  if (uvFault) {
    msg += "U_V";
    first = false;
  }
  if (ovFault) {
    if (!first) msg += "/";
    msg += "O_V";
    first = false;
  }
  if (otFault) {
    if (!first) msg += "/";
    msg += "O_T";
    first = false;
  }
  if (ocFault) {
    if (!first) msg += "/";
    msg += "O_C";
  }

  return msg;
}

// ---------- Function: Build phone web page ----------
// Creates a simple mobile-friendly HTML page showing the same information
// as the TFT display.
String buildWebPage() {
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<meta http-equiv='refresh' content='2'>";
  page += "<title>ESP32 Protection Monitor</title>";
  page += "<style>";
  page += "body{font-family:Arial,sans-serif;background:#111;color:#fff;text-align:center;padding:20px;}";
  page += ".box{max-width:420px;margin:auto;padding:20px;border-radius:12px;background:#1e1e1e;box-shadow:0 0 10px rgba(0,0,0,0.4);}";
  page += ".title{font-size:28px;font-weight:bold;color:cyan;margin-bottom:20px;}";
  page += ".value{font-size:22px;margin:12px 0;}";
  page += ".ok{color:#00ff66;font-weight:bold;}";
  page += ".fault{color:#ff4d4d;font-weight:bold;}";
  page += "</style></head><body>";

  page += "<div class='box'>";
  page += "<div class='title'>Protection</div>";

  if (tempSensorAvailable) {
    page += "<div class='value'>Temp: " + String(latestTempC, 2) + " C</div>";
  } else {
    page += "<div class='value'>Temp sensor not found</div>";
  }

  page += "<div class='value'>Curr: " + String(latestCurrentA, 3) + " A</div>";
  page += "<div class='value'>Volt: " + String(latestVoltageV, 2) + " V</div>";

  if (latestHealthy) {
    page += "<div class='value ok'>System Normal</div>";
    page += "<div class='value ok'>Relay: ON</div>";
  } else {
    page += "<div class='value fault'>" + latestFaultMsg + "</div>";
    page += "<div class='value fault'>Relay: OFF</div>";
  }

  page += "</div></body></html>";
  return page;
}

// ---------- Function: Handle root web page ----------
// Sends the generated HTML page to the phone browser.
void handleRoot() {
  server.send(200, "text/html", buildWebPage());
}

// ---------- Function: Update TFT display ----------
// Shows live system values and fault status on the screen.
void drawScreen(float tempC, float currentA, float voltageV,
                bool uvFault, bool ovFault, bool ocFault, bool otFault,
                bool healthy) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.setCursor(15, 15);
  tft.println("Protection");

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(15, 70);
  // Display temperature reading if the MCP9808 is available.
  // If the sensor is not detected, show a clear message instead.
  if (tempSensorAvailable) {
    tft.print("Temp: ");
    tft.print(tempC, 2);
    tft.println(" C");
  } else {
    tft.println("Temp sensor");
    tft.setCursor(15, 92);
    tft.println("not found");
  }

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(15, 120);
  tft.print("Current: ");
  tft.print(currentA, 3);
  tft.println(" A");

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(15, 155);
  tft.print("Voltage: ");
  tft.print(voltageV, 2);
  tft.println(" V");

  String faultMsg = buildFaultMessage(uvFault, ovFault, ocFault, otFault);

  if (healthy) {
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(15, 190);
    tft.println("System Normal");
    tft.setCursor(15, 220);
    tft.println("Relay: ON");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(15, 190);
    tft.println(faultMsg);
    tft.setCursor(15, 220);
    tft.println("Relay: OFF");
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);       // Start serial communication
  Wire.begin(21, 22);         // Start I2C on ESP32 pins 21 and 22

  pinMode(relayPin, INPUT);   // Leave relay pin inactive at startup
  pinMode(ledPin, OUTPUT);    // Set indicator LED pin as output
  digitalWrite(ledPin, LOW);  // Turn indicator LED off initially

  pinMode(TFT_BL, OUTPUT);    // Configure TFT backlight control pin as an output
 digitalWrite(TFT_BL, HIGH); // Turn the TFT backlight on

 SPI.begin(18, -1, 23, 5);   // Initialize SPI using SCK = 18 and MOSI = 23 for the TFT
 delay(100);                 // Short delay to allow the display interface to stabilize

 tft.init(240, 320);         // Initialize the ST7789 display with 240 x 320 resolution
 tft.setRotation(1);         // Set the screen orientation
 tft.fillScreen(ST77XX_BLACK); // Clear the screen to black

 tft.setTextColor(ST77XX_WHITE);
 tft.setTextSize(2);
 tft.setCursor(20, 20);
 tft.println("Starting...");

  // Initialize MCP9808 temperature sensor
  tempSensorAvailable = tempsensor.begin();

  if (tempSensorAvailable) {
    Serial.println("Temp sensor found");
  } else {
    Serial.println("Temp sensor not found");
  }

  // Calibrate ACS712 with no current flowing
  calibrateCurrentSensor();

  // Start in healthy state
  setRelayState(true);
  setLedState(true);

  // Connect to Wi-Fi
  // Connect to any available Wi-Fi network
if (connectToAnyWiFi()) {
  Serial.print("Connected to: ");
  Serial.println(connectedNetwork);

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();
  Serial.println("Web server started");
} else {
  Serial.println("No Wi-Fi network connected");
}
}
// ---------- Main Loop ----------
void loop() {
  // Read temperature only if the sensor is available
  float tempC = 0.0;
  if (tempSensorAvailable) {
    tempC = tempsensor.readTempC();
  }

  // Read current and voltage
  float currentA = readCurrentA();
  float voltageV = readSystemVoltage();

  // Evaluate protection conditions
  bool uvFault = (voltageV < underVoltageLimit);
  bool ovFault = (voltageV > overVoltageLimit);
  bool ocFault = (currentA > overCurrentLimit);
  bool otFault = (tempSensorAvailable && tempC > overTempLimit);

  // Healthy only if no faults are active
  bool healthy = !(uvFault || ovFault || ocFault || otFault);

  // Update relay and indicator LED
  setRelayState(healthy);
  setLedState(healthy);

  // Build system status message
  String faultMsg = buildFaultMessage(uvFault, ovFault, ocFault, otFault);

  // Save latest values for phone web page
  latestTempC = tempC;
  latestCurrentA = currentA;
  latestVoltageV = voltageV;
  latestFaultMsg = faultMsg;
  latestHealthy = healthy;

  // Print live values to serial monitor
  if (tempSensorAvailable) {
    Serial.print("Temp: ");
    Serial.print(tempC, 2);
    Serial.print(" C   ");
  } else {
    Serial.print("Temp: Sensor not found   ");
  }

  Serial.print("Current: ");
  Serial.print(currentA, 3);
  Serial.print(" A   ");

  Serial.print("Voltage: ");
  Serial.print(voltageV, 2);
  Serial.print(" V   ");

  Serial.println(faultMsg);

  // Handle incoming phone/browser requests
  server.handleClient();

  // Refresh TFT display periodically
  if (millis() - lastDisplayUpdate >= displayInterval) {
    lastDisplayUpdate = millis();
    drawScreen(tempC, currentA, voltageV, uvFault, ovFault, ocFault, otFault, healthy);
  }

  delay(300);
}