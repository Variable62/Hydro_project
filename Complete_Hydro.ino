#include <WiFiS3.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <WiFiSSLClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Arduino_JSON.h>
#include <ArduinoHttpClient.h>

const char* ssid = "kabincom2";
const char* password = "72704059";
const char* defaultHost = "script.google.com";
const char* scriptId = "AKfycbzwuzYyBQIZ-XOq9sPXslMkCgG0WVDkNSRZ0DyXF4aj3qjo_o3rqQRHCEhszSl4LXiu2w";

//AKfycbzwuzYyBQIZ-XOq9sPXslMkCgG0WVDkNSRZ0DyXF4aj3qjo_o3rqQRHCEhszSl4LXiu2w
const String scriptPath = String("/macros/s/") + scriptId + "/exec";

// Declaration
#define EC_PIN A0
#define PH_PIN A1
#define FLOAT_SWITCH_PIN 3
#define DHTPIN1 4
#define DHTPIN2 5
#define FAN1_PIN 7
#define FAN2_PIN 8
#define MIST1_PIN 9
#define MIST2_PIN 10
#define LIGHT1_PIN 11
#define LIGHT2_PIN 12
#define MOTOR_PUMP_PIN 13

#define DHTTYPE DHT22
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
BH1750 lux1(0x23);
BH1750 lux2(0x5C);
WiFiSSLClient client;   //For Google App script
WiFiClient httpClient; 

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 *  3600);

HttpClient http(httpClient, "160.238.13.148", 8000);
HttpClient http2(httpClient, "160.238.13.148", 8000);
HttpClient http3(httpClient, "160.238.13.148", 8000);
HttpClient http4(httpClient, "160.238.13.148", 8000);

float t1 = NAN, h1 = NAN, t2 = NAN, h2 = NAN, l1 = NAN, l2 = NAN, ec = NAN, ph = NAN;
String waterLevel = "N/A";
String controlMode = "WebApp";
bool fan1State = false, fan2State = false, mist1State = false, mist2State = false, light1State = false, light2State = false;
bool hasSentForThisTrigger = false;
int currentSecond = timeClient.getSeconds();
int currentHour  = timeClient.getHours();

struct UserSettings {
  float temp_Max = 35.0;
  float humid_Min = 60.0;
  int light_On_Hour = 6;
  int light_On_Minute = 0;   // เปิดไฟ 06:00
  int light_Off_Hour = 21;
  int light_Off_Minute = 0;  // ปิดไฟ 21:00
};
UserSettings settings;

void setup() {
  Serial.begin(115200);

  Serial.println("Initializing Hydroponics Control System v23 (Patched)...");

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

  connectToWiFi();
  timeClient.begin();
}

void loop() {
  timeClient.update();
  //connectToWiFi();
  if ((currentSecond == 0 || currentSecond == 30) && !hasSentForThisTrigger) {
    hasSentForThisTrigger = true;
    Serial.println("\n==============================================");
    Serial.println("Triggered at " + timeClient.getFormattedTime());

    MOTOR_PUMP_SET_TIME();
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
}

String httpPOSTRequest_NonSSL(String reqHost, int reqPort, String reqPath, String jsonPayload);
String httpPOSTRequestWithResponse(String reqHost, String reqPath, String jsonPayload);
void httpPOSTRequest(String reqHost, String reqPath, String jsonPayload);

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > 20000) {  // บังคับเชื่อมต่อ
      Serial.println("\nFailed to connect to WiFi! Rebooting...");
      delay(1000);
      NVIC_SystemReset();  
    }
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
   if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
 }else {
    digitalWrite(LED_BUILTIN, LOW);
 }
}

void fetchUserSettings() {
  http3.get("/sheet-data");

  int statusCode = http3.responseStatusCode();
  String payload = http3.responseBody();
  JSONVar response = JSON.parse(payload);
  Serial.println(statusCode);
  http3.stop();
 if (statusCode == 200) {
    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) != "undefined" && (bool)response["success"]) {
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

  Serial.println("Fetching control states via POST...");
  http2.get("/get/");

  int statusCode = http2.responseStatusCode();
  String payload = http2.responseBody();

  JSONVar response = JSON.parse(payload);
  Serial.print("Control states fetch status code: ");
  Serial.println(statusCode);

  JSONVar data = response["data"];
  Serial.println(data);
  if (true) {
    JSONVar response = JSON.parse(payload);
    if (JSON.typeof(response) == "undefined" || !(bool)response["success"]) {
      Serial.println("Error parsing control states JSON.");
      return;
    }
    JSONVar data = response["data"];

    if (data.hasOwnProperty("ControlBy")) {
      String fetchedMode = (const char*)data["ControlBy"];
      fetchedMode.trim();
      controlMode = fetchedMode;
      Serial.print("Mode set to: '");
      Serial.print(controlMode);
      Serial.println("'");
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
    http2.stop();
}

void readAllSensors() {
  t1 = dht1.readTemperature();
  h1 = dht1.readHumidity();
  t2 = dht2.readTemperature();
  h2 = dht2.readHumidity();
  l1 = lux1.readLightLevel();
  l2 = lux2.readLightLevel();
  ec = analogRead(EC_PIN) * (5.0 / 1023.0) * 1000.0;
  ph = analogRead(PH_PIN) * (5.0 / 1023.0) * 3.5;
  waterLevel = (digitalRead(FLOAT_SWITCH_PIN) == LOW) ? "NOT" : "OK";
}

void determineRelayStates() {
  if (controlMode.equals("Auto")) {
    int timeNow = timeClient.getHours() * 60 + timeClient.getMinutes();

    int lightOn  = settings.light_On_Hour * 60 + settings.light_On_Minute;
    int lightOff = settings.light_Off_Hour * 60 + settings.light_Off_Minute;

    fan1State = (!isnan(t1) && t1 > settings.temp_Max);
    fan2State = (!isnan(t2) && t2 > settings.temp_Max);
    mist1State = (!isnan(h1) && h1 < settings.humid_Min);
    mist2State = (!isnan(h2) && h2 < settings.humid_Min);

    if (lightOn < lightOff) {
      // เปิด-ปิดในวันเดียวกัน
      light1State = (timeNow >= lightOn && timeNow < lightOff);
      light2State = (timeNow >= lightOn && timeNow < lightOff);
    } else {
      // เปิดตอนเย็น ปิดข้ามวัน (เช่น 21:00 → 04:20)
      light1State = (timeNow >= lightOn || timeNow < lightOff);
      light2State = (timeNow >= lightOn || timeNow < lightOff);
    }
  }
}
// ACTIVE LOW
void applyRelayStates() { 
  digitalWrite(FAN1_PIN, fan1State ?  HIGH: LOW );
  digitalWrite(FAN2_PIN, fan2State ? HIGH : LOW);
  digitalWrite(MIST1_PIN, mist1State ? HIGH : LOW);
  digitalWrite(MIST2_PIN, mist2State ? HIGH : LOW);
  digitalWrite(LIGHT1_PIN, light1State ? HIGH : LOW);
  digitalWrite(LIGHT2_PIN, light2State ? HIGH : LOW);
  digitalWrite(MOTOR_PUMP_PIN,LOW);
}

void sendDataToGoogleSheets() {
  JSONVar root;
  root["action"] = "logData";

  Serial.println("Acivate Send Google Sheet");
  JSONVar payload;
  // Create URL
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
    Serial.println(">>> Auto mode confirmed. Sending relay states.");
    payload["controlBy"] = "Auto";
    payload["Light1"] = String(light1State ? "ON" : "OFF");
    payload["Light2"] = String(light2State ? "ON" : "OFF");
    payload["Fan1"] = String(fan1State ? "ON" : "OFF");
    payload["Fan2"] = String(fan2State ? "ON" : "OFF");
    payload["MistSprayer1"] = String(mist1State ? "ON" : "OFF");
    payload["MistSprayer2"] = String(mist2State ? "ON" : "OFF");
    url += "&controlBy=" + String(payload["controlBy"]);
    url += "&Light1=" + String(payload["Light1"]);
    url += "&Light2=" + String(payload["Light2"]);
    url += "&Fan1=" + String(payload["Fan1"]);
    url += "&Fan2=" + String(payload["Fan2"]);
    url += "&MistSprayer1=" + String(payload["MistSprayer1"]);
    url += "&MistSprayer2=" + String(payload["MistSprayer2"]);
  }
  Serial.println(url);
  http4.get(url);
  int statusCode = http4.responseStatusCode();
  String pay = http4.responseBody();
  http4.stop();
}
void printStatus() {
  Serial.println("--- Current Status ---");
  Serial.print("Mode: ");
  Serial.println(controlMode);
  Serial.print("Temp1: ");
  Serial.print(t1);
  Serial.print("C, Hum1: ");
  Serial.print(h1);
  Serial.println("%");
  Serial.print("Temp2: ");
  Serial.print(t2);
  Serial.print("C, Hum2: ");
  Serial.print(h2);
  Serial.println("%");
  Serial.print("Light 1: ");
  Serial.println(light1State ? "ON" : "OFF");
  Serial.println("----------------------");
}

String httpPOSTRequest_NonSSL(String reqHost, int reqPort, String reqPath, String jsonPayload) {
  String responseBody = "";
  Serial.println("Attempting Non-SSL POST to " + reqHost + ":" + reqPort + reqPath);
  if (httpClient.connect(reqHost.c_str(), reqPort)) {
    httpClient.println("POST " + reqPath + " HTTP/1.1");
    httpClient.println("Host: " + reqHost);
    httpClient.println("Content-Type: application/json");
    httpClient.print("Content-Length: ");
    httpClient.println(jsonPayload.length());
    httpClient.println("Connection: close");
    httpClient.println();
    httpClient.println(jsonPayload);
  }
  return responseBody;
}

String httpPOSTRequestWithResponse(String reqHost, String reqPath, String jsonPayload) {
  String responseBody = "";
  if (client.connect(reqHost.c_str(), 200)) {
    client.println("POST " + reqPath + " HTTP/1.1");
    client.println("Host: " + reqHost);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonPayload.length());
    client.println("Connection: close");
    client.println();
    client.println(jsonPayload);
  }
  return responseBody;
}

void httpPOSTRequest(String reqHost, String reqPath, String jsonPayload) {
  if (client.connect(reqHost.c_str(), 200)) {
    client.println("POST " + reqPath + " HTTP/1.1");
    client.println("Host: " + reqHost);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonPayload.length());
    client.println("Connection: close");
    client.println();
    client.println(jsonPayload);
  }
}
void MOTOR_PUMP_SET_TIME(){

  switch(currentHour) {
    case 7 :  // 7 โมง - 7.59 ปั้มจะทำงาน
          digitalWrite(MOTOR_PUMP_PIN, HIGH);
    break;

    case 12 :  // 12.00 - 12.59 ปั้มจะทำงาน
          digitalWrite(MOTOR_PUMP_PIN, HIGH);
    break;

    default : //นอกเหนือจากนั้นไม่เปิด
          digitalWrite(MOTOR_PUMP_PIN, LOW);
  }// อยากให้ติดช่วงไหนใช้เลขนั้น
}