#include <WiFiS3.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Arduino_JSON.h>
#include <ArduinoHttpClient.h>

// --- WiFi & API Settings ---
const char* ssid = "thavan";
const char* password = "88888888";
const char* apiHost = "160.238.13.148";
const int apiPort = 8000;

// --- Pin Definitions ---
#define EC_PIN A0
#define PH_PIN A1
#define FLOAT_SWITCH_PIN 3
#define DHTPIN1 4
#define DHTPIN2 5

#define FAN_PIN 6  // พัดลมใหญ่
#define FAN1_PIN 7
#define FAN2_PIN 8
#define MIST1_PIN 9
#define MIST2_PIN 10
#define LIGHT1_PIN 11
#define LIGHT2_PIN 12
#define MOTOR_PUMP_PIN 13

// --- Object Declarations ---
#define DHTTYPE DHT11
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
BH1750 lux1(0x23);
BH1750 lux2(0x5C);

WiFiClient httpClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600);

// Using multiple HttpClient instances as requested
HttpClient http(httpClient, apiHost, apiPort);
HttpClient http2(httpClient, apiHost, apiPort);
HttpClient http3(httpClient, apiHost, apiPort);
HttpClient http4(httpClient, apiHost, apiPort);

// --- Global Variables ---
float t1 = NAN, h1 = NAN, t2 = NAN, h2 = NAN, l1 = NAN, l2 = NAN, ec = NAN, ph = NAN;
String waterLevel = "N/A";
String controlMode = "WebApp";
bool fan1State = false, fan2State = false, mist1State = false, mist2State = false, light1State = false, light2State = false;
bool motorPumpState = false;
bool hasSentForThisTrigger = false;

struct UserSettings {
  float temp_Max;
  float humid_Min;
  int light_On_Hour;
  int light_On_Minute;
  int light_Off_Hour;
  int light_Off_Minute;
};
UserSettings settings;


// =================================================================
// --- SETUP ---
// =================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\nInitializing Hydroponics Control System v29 (Added Big Fan)...");

  dht1.begin();
  dht2.begin();
  Wire.begin();
  lux1.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23);
  lux2.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C);

  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT);
  pinMode(MIST1_PIN, OUTPUT);
  pinMode(MIST2_PIN, OUTPUT);
  pinMode(LIGHT1_PIN, OUTPUT);
  pinMode(LIGHT2_PIN, OUTPUT);
  pinMode(MOTOR_PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  // Set all relays to default OFF state (for ACTIVE HIGH relays)
  digitalWrite(FAN1_PIN, LOW);
  digitalWrite(FAN2_PIN, LOW);
  digitalWrite(MIST1_PIN, LOW);
  digitalWrite(MIST2_PIN, LOW);
  digitalWrite(LIGHT1_PIN, LOW);
  digitalWrite(LIGHT2_PIN, LOW);
  digitalWrite(MOTOR_PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);

  checkAndReconnectWiFi();
  timeClient.begin();
}

// =================================================================
// --- MAIN LOOP ---
// =================================================================
void loop() {
  checkAndReconnectWiFi();
  timeClient.update();

  int currentSecond = timeClient.getSeconds();

  if ((currentSecond == 0 || currentSecond == 30) && !hasSentForThisTrigger) {
    hasSentForThisTrigger = true;
    Serial.println("\n==============================================");
    Serial.println("Triggered at " + timeClient.getFormattedTime());

    fetchUserSettings();
    fetchControlStates();
    readAllSensors();
    determineRelayStates();
    applyRelayStates();
    printStatus();
    sendDataToGoogleSheets();

    Serial.println("Cycle finished.");
  } else if (currentSecond != 0 && currentSecond != 30) {
    hasSentForThisTrigger = false;
  }
  delay(100);
}

// =================================================================
// --- Core Functions ---
// =================================================================

void fetchUserSettings() {
  Serial.println("Fetching user settings from API...");
  http3.get("/sheet-data");

  int statusCode = http3.responseStatusCode();
  String payload = http3.responseBody();
  Serial.print("User settings fetch status code: ");
  Serial.println(statusCode);
  
  http3.stop();

  if (statusCode == 200) {
    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) != "undefined" && response.hasOwnProperty("success") && (bool)response["success"]) {
      JSONVar data = response["data"];
      Serial.println("User settings parsed successfully.");
      settings.temp_Max = String((const char*)data["temp_Max"]).toFloat();
      settings.humid_Min = String((const char*)data["humid_Min"]).toFloat();
      settings.light_On_Hour = String((const char*)data["light_On"]).substring(0, 2).toInt();
      settings.light_Off_Hour = String((const char*)data["light_Off"]).substring(0, 2).toInt();
    } else {
      Serial.println("Failed to parse user settings JSON.");
    }
  } else {
    Serial.println("Failed to fetch user settings from server.");
  }
}

void fetchControlStates() {
  Serial.println("Fetching control states from API...");
  http2.get("/get/");

  int statusCode = http2.responseStatusCode();
  String payload = http2.responseBody();
  Serial.print("Control states fetch status code: ");
  Serial.println(statusCode);
  
  http2.stop();

  if (statusCode == 200) {
    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) == "undefined" || !(bool)response["success"]) {
      Serial.println("Error parsing control states JSON.");
      return;
    }
    JSONVar data = response["data"];

    if (data.hasOwnProperty("ControlBy")) {
      controlMode = (const char*)data["ControlBy"];
      controlMode.trim();
      Serial.print("Mode set to: '"); Serial.print(controlMode); Serial.println("'");
    } else {
      controlMode = "WebApp";
    }

    if (!controlMode.equals("Auto")) {
      if (data.hasOwnProperty("Light1")) light1State = String((const char*)data["Light1"]).equals("ON");
      if (data.hasOwnProperty("Light2")) light2State = String((const char*)data["Light2"]).equals("ON");
      if (data.hasOwnProperty("Fan1")) fan1State = String((const char*)data["Fan1"]).equals("ON");
      if (data.hasOwnProperty("Fan2")) fan2State = String((const char*)data["Fan2"]).equals("ON");
      if (data.hasOwnProperty("MistSprayer1")) mist1State = String((const char*)data["MistSprayer1"]).equals("ON");
      if (data.hasOwnProperty("MistSprayer2")) mist2State = String((const char*)data["MistSprayer2"]).equals("ON");
    }
  } else {
    Serial.println("Failed to fetch control states.");
  }
}

void readAllSensors() {
  t1 = dht1.readTemperature(); h1 = dht1.readHumidity();
  t2 = dht2.readTemperature(); h2 = dht2.readHumidity();
  l1 = lux1.readLightLevel();  l2 = lux2.readLightLevel();
  ec = analogRead(EC_PIN) * (5.0 / 1023.0) * 1000.0;
  ph = analogRead(PH_PIN) * (5.0 / 1023.0) * 3.5;
  waterLevel = (digitalRead(FLOAT_SWITCH_PIN) == LOW) ? "OK" : "LOW";

  float offset_temp = -10.0 ;
  float offset_pH = +2;
  float offset_humidity = -15;
    
  t1 += offset_temp;
  t2 += offset_temp;
  h1 += offset_humidity;
  h2 += offset_humidity;
  ph += offset_pH;
}

void determineRelayStates() {
  int currentHour = timeClient.getHours();
  
  motorPumpState = (currentHour == 7 || currentHour == 12 || currentHour == 18);

  if (controlMode.equals("Auto")) {
    int timeNow = currentHour * 60 + timeClient.getMinutes();
    int lightOn = settings.light_On_Hour * 60 + settings.light_On_Minute;
    int lightOff = settings.light_Off_Hour * 60 + settings.light_Off_Minute;

    fan1State = (!isnan(t1) && t1 > settings.temp_Max);
    fan2State = (!isnan(t2) && t2 > settings.temp_Max);
    mist1State = (!isnan(h1) && h1 < settings.humid_Min);
    mist2State = (!isnan(h2) && h2 < settings.humid_Min);
    Serial.println(settings.temp_Max);
    Serial.println(settings.humid_Min);


    if (lightOn < lightOff) {
      light1State = (timeNow >= lightOn && timeNow < lightOff);
    } else {
      light1State = (timeNow >= lightOn || timeNow < lightOff);
    }
    light2State = light1State;
  }
}

void applyRelayStates() {

  digitalWrite(FAN1_PIN, fan1State ? HIGH : LOW);
  digitalWrite(FAN2_PIN, fan2State ? HIGH : LOW);
  digitalWrite(MIST1_PIN, mist1State ? HIGH : LOW);
  digitalWrite(MIST2_PIN, mist2State ? HIGH : LOW);
  digitalWrite(LIGHT1_PIN, light1State ? HIGH : LOW);
  digitalWrite(LIGHT2_PIN, light2State ? HIGH : LOW);
  digitalWrite(MOTOR_PUMP_PIN, motorPumpState ? HIGH : LOW);

          if (fan1State || fan2State) {
    digitalWrite(FAN_PIN, HIGH); // Turn big fan ON
  } else {
    digitalWrite(FAN_PIN, LOW); // Turn big fan OFF
  }
}

void sendDataToGoogleSheets() {
  Serial.println("Sending data to API...");
  String url = "/send-data";
  url += "?t1=" + (isnan(t1) ? "0" : String(t1, 2));
  url += "&h1=" + (isnan(h1) ? "0" : String(h1, 2));
  url += "&t2=" + (isnan(t2) ? "0" : String(t2, 2));
  url += "&h2=" + (isnan(h2) ? "0" : String(h2, 2));
  url += "&lux1=" + (isnan(l1) ? "0" : String(l1, 2));
  url += "&lux2=" + (isnan(l2) ? "0" : String(l2, 2));
  url += "&ec=" + (isnan(ec) ? "0" : String(ec, 2));
  url += "&ph=" + (isnan(ph) ? "0" : String(ph, 2));  
  url += "&waterLevel=" + waterLevel;

  if (controlMode.equals("Auto")) {
    Serial.println(">>> Auto mode confirmed. Appending relay states.");
    url += "&controlBy=Auto";
    url += "&Light1=" + String(light1State ? "ON" : "OFF");
    url += "&Light2=" + String(light2State ? "ON" : "OFF");
    url += "&Fan1=" + String(fan1State ? "ON" : "OFF");
    url += "&Fan2=" + String(fan2State ? "ON" : "OFF");
    url += "&MistSprayer1=" + String(mist1State ? "ON" : "OFF");
    url += "&MistSprayer2=" + String(mist2State ? "ON" : "OFF");
  }
  
  http4.get(url);
  int statusCode = http4.responseStatusCode();
  Serial.print("API send data status code: ");
  Serial.println(statusCode);
  
  http4.stop();
}

// =================================================================
// --- Utility Functions ---
// =================================================================

void printStatus() {
  Serial.println("--- Current Status ---");
  Serial.print("Mode: "); Serial.println(controlMode);
  Serial.print("Temp1: "); Serial.print(t1); Serial.print("C, Hum1: "); Serial.print(h1); Serial.println("%");
  Serial.print("Temp2: "); Serial.print(t2); Serial.print("C, Hum2: "); Serial.print(h2); Serial.println("%");
  Serial.print("Lights: "); Serial.println(light1State ? "ON" : "OFF");
  Serial.print("Motor Pump: "); Serial.println(motorPumpState ? "ON" : "OFF");
  
  // Also print the status of the new big fan
  bool bigFanState = (fan1State || fan2State);
  Serial.print("Big Fan: "); Serial.println(bigFanState ? "ON" : "OFF");

  Serial.println("----------------------");
}

void checkAndReconnectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  Serial.println("\nWiFi connection lost! Reconnecting...");
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > 20000) {
      Serial.println("Failed to reconnect. Rebooting...");
      delay(1000);
      NVIC_SystemReset();
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi reconnected!");
}
