/*************************************************************
  Download latest ERa library here:
    https://github.com/eoh-jsc/era-lib/releases/latest
    https://www.arduino.cc/reference/en/libraries/era
    https://registry.platformio.org/libraries/eoh-ltd/ERa/installation

    ERa website:                https://e-ra.io
    ERa blog:                   https://iotasia.org
    ERa forum:                  https://forum.eoh.io
    Follow us:                  https://www.fb.com/EoHPlatform
 *************************************************************/

// Enable debug console
#define ERA_DEBUG

/* Define MQTT host */
#define DEFAULT_MQTT_HOST "mqtt1.eoh.io"

// You should get Auth Token in the ERa App or ERa Dashboard
#define ERA_AUTH_TOKEN "_yourToken__"

/* Define setting button */
// #define BUTTON_PIN              0

#if defined(BUTTON_PIN)
// Active low (false), Active high (true)
#define BUTTON_INVERT false
#define BUTTON_HOLD_TIMEOUT 5000UL

// This directive is used to specify whether the configuration should be erased.
// If it's set to true, the configuration will be erased.
#define ERA_ERASE_CONFIG false
#endif

// Declare fingerprint sensor connection pins
#define FINGERPRINT_RX_PIN 16
#define FINGERPRINT_TX_PIN 17

// Simple and fast fingerprint authentication configuration
#define CONFIDENCE_THRESHOLD 60 // Minimum confidence threshold (lower)
#define MAX_RETRY_ATTEMPTS 3    // Maximum retry attempts per finger placement
#define RETRY_DELAY 100         // Delay between retries (ms)
#define BUZZER_PIN 19
// Door control variables
static bool isSendingData = false;
unsigned long int currentTimeOpen = 0;
const int timedoorInterval = 15000;
bool isDoorOpen = false;
// Buzzer control variable
static int consecutiveAuthFailures = 0;
bool doorShouldOpen = false; // Flag to ensure door opening

#include <Arduino.h>
#include <ERa.hpp>
#include <Adafruit_Fingerprint.h>
#include <WiFiClientSecure.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

// LCD Display control variables
unsigned long lastLCDUpdate = 0;
unsigned long LCD_UPDATE_INTERVAL = 2000; // 2 seconds display time
int currentDisplayState = 0;
String currentMessage1 = "";
String currentMessage2 = "";
bool needLCDClear = false;

const char ssid[] = "";
const char pass[] = "";

WiFiClient mbTcpClient;

#if defined(ERA_AUTOMATION)
#include <Automation/ERaSmart.hpp>

#if defined(ESP32) || defined(ESP8266)
#include <Time/ERaEspTime.hpp>
/* NTP Time */
ERaEspTime syncTime;
TimeElement_t ntpTime;
#else
#define USE_BASE_TIME

#include <Time/ERaBaseTime.hpp>
ERaBaseTime syncTime;
#endif

ERaSmart smart(ERa, syncTime);
#endif

#if defined(BUTTON_PIN)
#include <ERa/ERaButton.hpp>

ERaButton button;

#if ERA_VERSION_NUMBER >= ERA_VERSION_VAL(1, 6, 0)
static void eventButton(uint16_t pin, ButtonEventT event)
{
  if (event != ButtonEventT::BUTTON_ON_HOLD)
  {
    return;
  }
  ERa.switchToConfig(ERA_ERASE_CONFIG);
  (void)pin;
}
#elif ERA_VERSION_NUMBER >= ERA_VERSION_VAL(1, 2, 0)
static void eventButton(uint8_t pin, ButtonEventT event)
{
  if (event != ButtonEventT::BUTTON_ON_HOLD)
  {
    return;
  }
  ERa.switchToConfig(ERA_ERASE_CONFIG);
  (void)pin;
}
#else
static void eventButton(ButtonEventT event)
{
  if (event != ButtonEventT::BUTTON_ON_HOLD)
  {
    return;
  }
  ERa.switchToConfig(ERA_ERASE_CONFIG);
}
#endif

#if defined(ESP32)
#include <pthread.h>

pthread_t pthreadButton;

static void *handlerButton(void *args)
{
  for (;;)
  {
    button.run();
    ERaDelay(10);
  }
  pthread_exit(NULL);
}

void initButton()
{
  pinMode(BUTTON_PIN, INPUT);
  button.setButton(BUTTON_PIN, digitalRead, eventButton,
                   BUTTON_INVERT)
      .onHold(BUTTON_HOLD_TIMEOUT);
  pthread_create(&pthreadButton, NULL, handlerButton, NULL);
}
#elif defined(ESP8266)
#include <Ticker.h>

Ticker ticker;

static void handlerButton()
{
  button.run();
}

void initButton()
{
  pinMode(BUTTON_PIN, INPUT);
  button.setButton(BUTTON_PIN, digitalRead, eventButton,
                   BUTTON_INVERT)
      .onHold(BUTTON_HOLD_TIMEOUT);
  ticker.attach_ms(100, handlerButton);
}
#elif defined(ARDUINO_AMEBA)
#include <GTimer.h>

const uint32_t timerIdButton{0};

static void handlerButton(uint32_t data)
{
  button.run();
  (void)data;
}

void initButton()
{
  pinMode(BUTTON_PIN, INPUT);
  button.setButton(BUTTON_PIN, digitalRead, eventButton,
                   BUTTON_INVERT)
      .onHold(BUTTON_HOLD_TIMEOUT);
  GTimer.begin(timerIdButton, (100 * 1000), handlerButton);
}
#endif
#endif

// Forward declaration for LCD functions
void displayLCDMessage(String line1, String line2, unsigned long displayTime);

/* This function will run every time ERa is connected */
ERA_CONNECTED()
{
  ERA_LOG(ERA_PSTR("ERa"), ERA_PSTR("ERa connected!"));
  displayLCDMessage("ERa Platform", "Connected!", 3000);
}

/* This function will run every time ERa is disconnected */
ERA_DISCONNECTED()
{
  ERA_LOG(ERA_PSTR("ERa"), ERA_PSTR("ERa disconnected!"));
  displayLCDMessage("ERa Platform", "Disconnected!", 3000);
}

// Initialize HardwareSerial object
HardwareSerial mySerial(1);

// Initialize Adafruit_Fingerprint object
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// Define Google Script
const char *GOOGLE_SCRIPT_HOST = "script.google.com";
const char *GOOGLE_SCRIPT_PATH = "/macros/s/____________/exec";
#define RELAY_PIN 18

// Structure to store authentication information
struct FingerprintAuth
{
  uint16_t id;
  uint16_t confidence;
  bool isValid;
};

// Global variables to avoid sending duplicates
static uint16_t lastValidatedID = 0;
static unsigned long lastValidationTime = 0;
static bool authenticationInProgress = false;
// Function to make single beep sound
void singleBeep()
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
}

// Function to make double beep sound for successful authentication
void successBeep()
{
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(150);
  digitalWrite(BUZZER_PIN, LOW);
}

// Function to make long alarm sound for multiple failures
void alarmBeep()
{
  for (int i = 0; i < 10; i++)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(250);
    digitalWrite(BUZZER_PIN, LOW);
    delay(250);
  }
}
// Function to clear LCD completely and display new message
void displayLCDMessage(String line1, String line2, unsigned long displayTime = 2000)
{
  // Clear entire LCD display
  lcd.clear();

  // Set cursor to beginning and display first line
  lcd.setCursor(0, 0);
  // Pad string to 16 characters to ensure complete overwrite
  String paddedLine1 = line1;
  while (paddedLine1.length() < 16)
  {
    paddedLine1 += " ";
  }
  if (paddedLine1.length() > 16)
  {
    paddedLine1 = paddedLine1.substring(0, 16);
  }
  lcd.print(paddedLine1);

  // Set cursor to second line and display
  lcd.setCursor(0, 1);
  // Pad string to 16 characters to ensure complete overwrite
  String paddedLine2 = line2;
  while (paddedLine2.length() < 16)
  {
    paddedLine2 += " ";
  }
  if (paddedLine2.length() > 16)
  {
    paddedLine2 = paddedLine2.substring(0, 16);
  }
  lcd.print(paddedLine2);

  // Store current message and update time
  currentMessage1 = line1;
  currentMessage2 = line2;
  lastLCDUpdate = millis();
}

// Function to display scrolling text for long messages
void displayScrollingMessage(String line1, String line2, unsigned long displayTime = 3000)
{
  lcd.clear();

  // Handle line 1
  if (line1.length() <= 16)
  {
    lcd.setCursor(0, 0);
    String paddedLine1 = line1;
    while (paddedLine1.length() < 16)
    {
      paddedLine1 += " ";
    }
    lcd.print(paddedLine1);
  }
  else
  {
    // Scroll line 1
    for (int i = 0; i <= line1.length() - 16; i++)
    {
      lcd.setCursor(0, 0);
      lcd.print(line1.substring(i, i + 16));
      delay(300);
    }
  }

  // Handle line 2
  if (line2.length() <= 16)
  {
    lcd.setCursor(0, 1);
    String paddedLine2 = line2;
    while (paddedLine2.length() < 16)
    {
      paddedLine2 += " ";
    }
    lcd.print(paddedLine2);
  }
  else
  {
    // Scroll line 2
    for (int i = 0; i <= line2.length() - 16; i++)
    {
      lcd.setCursor(0, 1);
      lcd.print(line2.substring(i, i + 16));
      delay(300);
    }
  }

  lastLCDUpdate = millis();
}

// Function to display system status
void displaySystemStatus()
{
  String wifiStatus = WiFi.isConnected() ? "WiFi: OK" : "WiFi: ERROR";
  String eraStatus = ERa.connected() ? "ERa: OK" : "ERa: ERROR";
  displayLCDMessage("System Status", wifiStatus + " " + eraStatus, 2000);
}

// Function to display fingerprint sensor info
void displayFingerprintInfo()
{
  String line1 = "FP Templates: " + String(finger.templateCount);
  String line2 = "Confidence: " + String(CONFIDENCE_THRESHOLD);
  displayLCDMessage(line1, line2, 2000);
}

// Function to send data to Google Sheet
void sendToGoogleSheet(int id, const char *time, const char *date)
{
  displayLCDMessage("Sending Data", "To Google Sheet", 2000);

  WiFiClientSecure client;
  client.setInsecure();

  // Create JSON payload
  String payload = "{\"time\":\"" + String(time) + "\",\"id\":\"" + String(id) + "\",\"date\":\"" + String(date) + "\"}";

  // Create manual HTTP request
  String request =
      "POST " + String(GOOGLE_SCRIPT_PATH) + " HTTP/1.1\r\n" +
      "Host: " + GOOGLE_SCRIPT_HOST + "\r\n" +
      "Content-Type: application/json\r\n" +
      "Content-Length: " + String(payload.length()) + "\r\n" +
      "Connection: close\r\n\r\n" +
      payload;

  // Connect and send request
  if (client.connect(GOOGLE_SCRIPT_HOST, 443))
  {
    client.print(request);
    Serial.println("[HTTP] Data sent to Google Sheet!");
    displayLCDMessage("Data Sent", "Successfully!", 2000);

    // Read response (debug)
    while (client.connected())
    {
      String line = client.readStringUntil('\n');
      if (line == "\r")
        break;
    }
    String response = client.readString();
    Serial.println(response);
  }
  else
  {
    Serial.println("[HTTP] Connection to Google failed!");
    displayLCDMessage("Send Failed", "Network Error", 2000);
  }
  client.stop();
}

// Function to get a single fingerprint sample
FingerprintAuth getSingleFingerprintSample()
{
  FingerprintAuth result = {0, 0, false};

  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK)
  {
    return result;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK)
  {
    return result;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK)
  {
    result.id = finger.fingerID;
    result.confidence = finger.confidence;
    result.isValid = true;

    Serial.printf("[SAMPLE] ID: %d, Confidence: %d\n", result.id, result.confidence);
  }

  return result;
}

// Function to reliably open the door
void openDoor()
{
  Serial.println("[DOOR] Opening door...");
  displayLCDMessage("Door Control", "Opening Door...", 1500);
  // Success beep - double beep for successful authentication
  successBeep();

  // Reset consecutive failure counter on successful authentication
  consecutiveAuthFailures = 0;
  // Set door should open flag first
  doorShouldOpen = true;

  // Send signal to E-Ra platform with retry mechanism
  for (int attempt = 0; attempt < 3; attempt++)
  {
    ERa.virtualWrite(V1, HIGH);
    digitalWrite(RELAY_PIN, LOW);
    delay(70); // Small delay to ensure signal is sent
    Serial.printf("[DOOR] Door open signal sent (attempt %d)\n", attempt + 1);

    // Verify the signal was sent by checking connection
    if (ERa.connected())
    {
      break;
    }
    delay(100);
  }

  // Set door state variables
  isDoorOpen = true;
  currentTimeOpen = millis();

  Serial.println("[DOOR] Door opened successfully!");
  displayLCDMessage("Access Granted", "Door Opened!", 2000);
}

// Function to close the door
void closeDoor()
{
  Serial.println("[DOOR] Closing door...");
  displayLCDMessage("Door Control", "Closing Door...", 1500);

  // Send close signal to E-Ra platform
  ERa.virtualWrite(V1, LOW);
  digitalWrite(RELAY_PIN, HIGH);
  // Reset door state variables
  isDoorOpen = false;
  doorShouldOpen = false;

  Serial.println("[DOOR] Door closed!");
  displayLCDMessage("Security Active", "Door Closed", 2000);
}

// Simple and fast fingerprint authentication function
FingerprintAuth authenticateFingerprint()
{
  Serial.println("[AUTH] Quick fingerprint authentication...");
  displayLCDMessage("Authenticating", "Please Wait...", 1000);

  FingerprintAuth result = {0, 0, false};

  // Try maximum MAX_RETRY_ATTEMPTS times
  for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS; attempt++)
  {
    Serial.printf("[AUTH] Attempt %d/%d\n", attempt, MAX_RETRY_ATTEMPTS);
    displayLCDMessage("Auth Attempt", "Try " + String(attempt) + "/" + String(MAX_RETRY_ATTEMPTS), 500);

    uint8_t p = finger.getImage();
    if (p != FINGERPRINT_OK)
    {
      Serial.printf("[AUTH] Get image failed: %d\n", p);
      if (attempt < MAX_RETRY_ATTEMPTS)
      {
        displayLCDMessage("Scan Failed", "Try Again...", 500);
        delay(RETRY_DELAY);
        continue;
      }
      displayLCDMessage("Auth Failed", "No Image", 2000);
      return result;
    }

    p = finger.image2Tz();
    if (p != FINGERPRINT_OK)
    {
      Serial.printf("[AUTH] Image2Tz failed: %d\n", p);
      if (attempt < MAX_RETRY_ATTEMPTS)
      {
        displayLCDMessage("Process Failed", "Try Again...", 500);
        delay(RETRY_DELAY);
        continue;
      }
      displayLCDMessage("Auth Failed", "Process Error", 2000);
      return result;
    }

    displayLCDMessage("Processing", "Fingerprint...", 500);

    p = finger.fingerFastSearch();
    if (p == FINGERPRINT_OK)
    {
      // Fingerprint found!
      Serial.printf("[AUTH] ✓ Found ID: %d, Confidence: %d\n", finger.fingerID, finger.confidence);

      // Check confidence
      if (finger.confidence >= CONFIDENCE_THRESHOLD)
      {
        result.id = finger.fingerID;
        result.confidence = finger.confidence;
        result.isValid = true;
        Serial.printf("[AUTH] ✓ AUTHENTICATED: ID=%d, Confidence=%d\n", result.id, result.confidence);
        displayLCDMessage("Auth Success!", "ID: " + String(result.id) + " (" + String(result.confidence) + "%)", 2000);
        return result;
      }
      else
      {
        Serial.printf("[AUTH] Low confidence: %d < %d, retry...\n", finger.confidence, CONFIDENCE_THRESHOLD);
        displayLCDMessage("Low Quality", "Conf: " + String(finger.confidence) + "%", 1000);
        if (attempt < MAX_RETRY_ATTEMPTS)
        {
          delay(RETRY_DELAY);
          continue;
        }
      }
    }
    else
    {
      Serial.printf("[AUTH] Fingerprint search failed: %d\n", p);
      if (attempt < MAX_RETRY_ATTEMPTS)
      {
        displayLCDMessage("Not Found", "Try Again...", 500);
        delay(RETRY_DELAY);
        continue;
      }
    }
  }

  Serial.println("[AUTH] ✗ Authentication failed after all attempts");
  displayLCDMessage("Auth Failed", "Access Denied", 3000);
  // Increment consecutive failures counter
  consecutiveAuthFailures++;

  // Single beep for each failed attempt
  singleBeep();

  // Check if this is the 3rd consecutive failure
  if (consecutiveAuthFailures >= 3)
  {
    Serial.println("[SECURITY] 3 consecutive failures - triggering alarm");
    displayLCDMessage("Security Alert", "Multiple Failures", 2000);
    alarmBeep();                 // 5-second alarm
    consecutiveAuthFailures = 0; // Reset counter after alarm
  }

  return result;
}

// Function to handle successful fingerprint authentication
void handleAuthenticatedFingerprint(FingerprintAuth auth)
{
  // Avoid sending duplicates within short time
  if (auth.id == lastValidatedID &&
      (millis() - lastValidationTime) < 2000)
  { // 2 second cooldown
    Serial.println("[SKIP] Same fingerprint within cooldown period");
    displayLCDMessage("Duplicate Scan", "Please Wait", 2000);
    return;
  }

  // Prevent data sending conflicts
  if (isSendingData)
  {
    Serial.println("[SKIP] Data sending in progress");
    displayLCDMessage("System Busy", "Please Wait", 2000);
    return;
  }

  isSendingData = true;

  // CRITICAL: Open door immediately after successful authentication
  openDoor();

  // Get time from NTP
  syncTime.getTime(ntpTime);

  // Format time and date
  char timeStr[12];
  sprintf(timeStr, "%02d:%02d:%02d", ntpTime.hour, ntpTime.minute, ntpTime.second);

  char dateStr[12];
  sprintf(dateStr, "%02d/%02d/%04d", ntpTime.day, ntpTime.month, ntpTime.year + 1970);

  // Display current access info
  displayLCDMessage("Access Time", String(timeStr), 2000);
  delay(2000);
  displayLCDMessage("Access Date", String(dateStr), 2000);
  delay(2000);

  // Send data to Google Sheet
  sendToGoogleSheet(auth.id, timeStr, dateStr);

  // Send ID to E-Ra platform
  String idString = "ID" + String(auth.id);
  ERa.virtualWrite(V0, idString.c_str());

  // Update status
  lastValidatedID = auth.id;
  lastValidationTime = millis();

  Serial.printf("[SUCCESS] Processed fingerprint: ID=%d, Time=%s, Date=%s, Confidence=%d\n",
                auth.id, timeStr, dateStr, auth.confidence);

  displayLCDMessage("Process Complete", "Data Logged", 2000);
  isSendingData = false;
}

/* This function handles fingerprint detection and authentication */
void timerEvent()
{
  ERA_LOG(ERA_PSTR("Timer"), ERA_PSTR("Uptime: %d"), ERaMillis() / 1000L);

  // Avoid running multiple authentication processes in parallel
  if (authenticationInProgress)
  {
    return;
  }

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 500)
  { // Check every 0.5 seconds for faster response
    return;
  }
  lastCheck = millis();

  // Quick check if there's a finger
  uint8_t p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER)
  {
    return; // No finger, exit
  }

  Serial.println("[DETECT] Finger detected!");
  displayLCDMessage("Finger Detected", "Scanning...", 1000);

  // Finger detected, start authentication process immediately
  authenticationInProgress = true;

  FingerprintAuth result = authenticateFingerprint();

  if (result.isValid)
  {
    handleAuthenticatedFingerprint(result);
  }
  else
  {
    Serial.println("[AUTH] Authentication failed - door remains closed");
  }

  authenticationInProgress = false;
}

// Door control monitoring function
void doorControlEvent()
{
  // Check if door should be opened but isn't
  if (doorShouldOpen && !isDoorOpen)
  {
    Serial.println("[DOOR] Door should be open but isn't - forcing open");
    openDoor();
  }

  // Check if door should be closed after timeout
  if (isDoorOpen && (millis() - currentTimeOpen >= timedoorInterval))
  {
    closeDoor();
  }
}

// Function to cycle through different display states when idle
void cycleLCDDisplay()
{
  // Only cycle display when not showing important messages
  if (millis() - lastLCDUpdate > 5000 && !authenticationInProgress && !isSendingData)
  {
    switch (currentDisplayState)
    {
    case 0:
      displayLCDMessage("Security System", "Ready to Scan", 4000);
      break;
    case 1:
      displaySystemStatus();
      break;
    case 2:
      displayFingerprintInfo();
      break;
    case 3:
    {
      // Get current time for display
      syncTime.getTime(ntpTime);
      char timeStr[12];
      sprintf(timeStr, "%02d:%02d:%02d", ntpTime.hour, ntpTime.minute, ntpTime.second);
      char dateStr[12];
      sprintf(dateStr, "%02d/%02d/%04d", ntpTime.day, ntpTime.month, ntpTime.year + 1970);
      displayLCDMessage(String(timeStr), String(dateStr), 4000);
    }
    break;
    case 4:
    {
      String doorStatus = isDoorOpen ? "Door: OPEN" : "Door: CLOSED";
      String uptime = "Up: " + String(millis() / 1000) + "s";
      displayLCDMessage(doorStatus, uptime, 4000);
    }
    break;
    }

    currentDisplayState = (currentDisplayState + 1) % 5;
  }
}

#if defined(USE_BASE_TIME)
unsigned long getTimeCallback()
{
  // Please implement your own function
  // to get the current time in seconds.
  return 0;
}
#endif

void setup()
{
  /* Setup debug console */
#if defined(ERA_DEBUG)
  Serial.begin(115200);
#endif

#if defined(BUTTON_PIN)
  /* Initializing button. */
  initButton();
  /* Enable read/write WiFi credentials */
  ERa.setPersistent(true);
#endif

#if defined(USE_BASE_TIME)
  syncTime.setGetTimeCallback(getTimeCallback);
#endif

  /* Setup Client for Modbus TCP/IP */
  ERa.setModbusClient(mbTcpClient);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off initially
  /* Set scan WiFi. If activated, the board will scan
     and connect to the best quality WiFi. */
  ERa.setScanWiFi(true);

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  // Display startup messages
  displayLCDMessage("System Starting", "Please Wait...", 2000);
  delay(2000);

  displayLCDMessage("Initializing", "WiFi Connection", 2000);

  /* Initializing the ERa library. */
  ERa.begin(ssid, pass);

  /* Setup timer called function every 0.5 seconds for faster fingerprint detection */
  ERa.addInterval(500L, timerEvent);

  /* Setup door control monitoring every 100ms */
  ERa.addInterval(100L, doorControlEvent);

  /* Setup LCD display cycling every 100ms */
  ERa.addInterval(100L, cycleLCDDisplay);

  Serial.println("=== Enhanced Reliable Fingerprint Door System ===");
  Serial.printf("Confidence Threshold: %d\n", CONFIDENCE_THRESHOLD);
  Serial.printf("Max Retry Attempts: %d\n", MAX_RETRY_ATTEMPTS);
  Serial.printf("Door Open Duration: %d ms\n", timedoorInterval);

  displayLCDMessage("System Config", "Loading...", 2000);
  delay(2000);

  // Initialize Serial for fingerprint sensor communication
  mySerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
  finger.begin(57600);

  displayLCDMessage("Fingerprint", "Initializing...", 2000);
  delay(2000);

  // Initialize fingerprint sensor
  if (finger.verifyPassword())
  {
    Serial.println("✓ Found fingerprint sensor!");
    displayLCDMessage("FP Sensor", "Connected!", 2000);
  }
  else
  {
    Serial.println("✗ Did not find fingerprint sensor :(");
    displayLCDMessage("FP Sensor", "ERROR!", 5000);
    while (1)
    {
      delay(1);
    }
  }

  delay(2000);

  Serial.println("Reading sensor parameters...");
  displayLCDMessage("Reading Sensor", "Parameters...", 2000);

  finger.getParameters();
  Serial.printf("Status: 0x%02X\n", finger.status_reg);
  Serial.printf("Sys ID: 0x%02X\n", finger.system_id);
  Serial.printf("Capacity: %d\n", finger.capacity);
  Serial.printf("Security level: %d\n", finger.security_level);
  Serial.printf("Device address: 0x%02X\n", finger.device_addr);
  Serial.printf("Packet len: %d\n", finger.packet_len);
  Serial.printf("Baud rate: %d\n", finger.baud_rate);

  finger.getTemplateCount();

  if (finger.templateCount == 0)
  {
    Serial.println("⚠ Sensor doesn't contain any fingerprint data. Please enroll fingerprints first.");
    displayLCDMessage("Warning!", "No Templates", 3000);
  }
  else
  {
    Serial.printf("✓ Sensor contains %d fingerprint templates\n", finger.templateCount);
    displayLCDMessage("Templates: " + String(finger.templateCount), "System Ready!", 3000);
    Serial.println("Ready for enhanced reliable fingerprint authentication...");
  }

  delay(3000);

  // Final ready message
  displayLCDMessage("Security System", "Online & Ready", 3000);
}

void loop()
{
  ERa.run();
}
