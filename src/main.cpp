#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "env.h"


const int POT_PIN = 34;
#define DHTPIN 32
#define DHTTYPE DHT11
const int VIBRATION_PIN = 15;
const int BTN_OFF = 18;  
const int BTN_ON = 19;   
const int R_PIN = 25;
const int G_PIN = 26;
const int B_PIN = 27;


DHT dht(DHTPIN, DHTTYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

WiFiClientSecure secureClient;

String reactorID = "";
String currentStatus = "Unknown"; 
float currentTemp = 0.0; 
float currentEff = 0.0;
int currentVib = 0; 

const int tempSensorId = 17;
const int vibSensorId = 18;

volatile int vibrationCount = 0;
unsigned long lastVibration = 0;
const int DEBOUNCE_MS = 200;

unsigned long lastSensorUpdate = 0;
const unsigned long UPDATE_INTERVAL = 5000; 

void IRAM_ATTR onVibration() {
    unsigned long now = millis();
    if (now - lastVibration > DEBOUNCE_MS) {
        vibrationCount++;
        lastVibration = now;
    }
}

// ================= LED CONTROL =================
void setLed(int r, int g, int b) {
    ledcWrite(0, r); 
    ledcWrite(1, g); 
    ledcWrite(2, b);
}

String normalizeStatus(String input) {
    String s = input;
    s.toLowerCase(); 
    
    s.replace("ă", "a");
    s.replace("â", "a");
    s.replace("î", "i");
    s.replace("ș", "s");
    s.replace("ț", "t");
    
    return s;
}

void updateDisplay() {
    display.clearDisplay(); 
    
    display.setTextSize(2); 
    display.setTextColor(SSD1306_WHITE); 
    
    display.setCursor(0, 0);
    display.print("St:");
    display.print(currentStatus);

    display.setCursor(0, 16); 
    display.print("T :");
    display.print(currentTemp, 1);
    display.print("C");

    display.setCursor(0, 32);
    display.print("Ef:");
    display.print((int)currentEff);
    display.print(" %");

    display.setCursor(0, 48);
    display.print("Vb:");
    display.print(currentVib);

    display.display(); 
}


void sendSensorData(int sensorId, float value, String sensorType) {
    HTTPClient http;
    http.begin(secureClient, ENV_BASE_URL + "/api/sensors/readings");
    http.addHeader("X-API-KEY", ENV_API_KEY);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["sensor_id"] = sensorId;
    doc["value"] = value;
    String jsonBody;
    serializeJson(doc, jsonBody);

    int postCode = http.POST(jsonBody);
    if (postCode == 200 || postCode == 201) {
        Serial.printf("[OK] %s sent: %.2f\n", sensorType.c_str(), value);
    }
    http.end();
}

void updateReactorStatus() {
    HTTPClient httpState;
    httpState.begin(secureClient, ENV_BASE_URL + "/api/reactors/status/" + reactorID);
    httpState.addHeader("X-API-KEY", ENV_API_KEY);

    int stateCode = httpState.GET();
    if (stateCode == 200) {
        JsonDocument docState; 
        deserializeJson(docState, httpState.getString());
        
        String rawStatus = docState["status"].as<String>();
        currentStatus = rawStatus; 
        
        String cleanStatus = normalizeStatus(rawStatus);
        
        Serial.println("[STATUS] Received: " + rawStatus + " | Normalized: " + cleanStatus);

        // LED Logic without Blink
        if (cleanStatus == "oprit") {
            setLed(0, 0, 0);          
        } else if (cleanStatus == "activ") {
            setLed(0, 255, 0);        
       
        } else if (cleanStatus.startsWith("mente")) {
            setLed(0, 0, 255);      
        } else if (cleanStatus == "avarie" || cleanStatus == "alerta") {
            setLed(255, 0, 0);      
        }
        
        updateDisplay(); 
    }
    httpState.end();
}

void changeReactorStatus(String newStatus) {
    HTTPClient http;
    http.begin(secureClient, ENV_BASE_URL + "/api/reactors/" + reactorID + "/statusESP");
    http.addHeader("X-API-KEY", ENV_API_KEY);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc; 
    doc["status"] = newStatus;
    String jsonBody; 
    serializeJson(doc, jsonBody);
    
    int httpCode = http.sendRequest("PATCH", jsonBody);
    if (httpCode > 0) {
        Serial.println("[INFO] Status change requested: " + newStatus);
        currentStatus = newStatus; 
        updateDisplay(); 
    }

    http.end();
}


void setup() {
    Serial.begin(115200);
    
    Wire.begin(21, 22);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("OLED alloc failed"));
    } else {
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,20);
        display.println("Booting...");
        display.display();
    }

    ledcSetup(0, 5000, 8); ledcAttachPin(R_PIN, 0);
    ledcSetup(1, 5000, 8); ledcAttachPin(G_PIN, 1);
    ledcSetup(2, 5000, 8); ledcAttachPin(B_PIN, 2);

    dht.begin();
    pinMode(VIBRATION_PIN, INPUT);
    pinMode(BTN_OFF, INPUT_PULLUP);
    pinMode(BTN_ON, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(VIBRATION_PIN), onVibration, FALLING);

    display.clearDisplay();
    display.setCursor(0,20);
    display.println("WiFi...");
    display.display();

    WiFi.begin(ENV_WIFI_SSID, ENV_WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

    display.clearDisplay();
    display.setCursor(0,20);
    display.println("Connected!");
    display.display();
    delay(1000);

    secureClient.setInsecure();

    HTTPClient http;
    http.begin(secureClient, ENV_BASE_URL + "/api/reactors/by-mac/" + WiFi.macAddress());
    http.addHeader("X-API-KEY", ENV_API_KEY);
    if (http.GET() == 200) {
        JsonDocument doc;
        deserializeJson(doc, http.getString());
        reactorID = doc["id"].as<String>();
    }
    http.end();
    
    if (reactorID != "") updateReactorStatus();
}


void loop() {
    if (reactorID == "") {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0, 20);
        display.println("ID Error!");
        display.display();
        delay(2000);
        return;
    }

    if (digitalRead(BTN_OFF) == LOW) {
        changeReactorStatus("Oprit");
        delay(500); 
    }
    if (digitalRead(BTN_ON) == LOW) {
        changeReactorStatus("Activ");
        delay(500); 
    }

    unsigned long currentMillis = millis();
    if (currentMillis - lastSensorUpdate >= UPDATE_INTERVAL) {
        lastSensorUpdate = currentMillis; 

        int potValue = analogRead(POT_PIN);
        currentEff = (potValue / 4095.0) * 100.0;
        
        HTTPClient httpPatch;
        httpPatch.begin(secureClient, ENV_BASE_URL + "/api/reactors/" + reactorID + "/efficiency");
        httpPatch.addHeader("X-API-KEY", ENV_API_KEY);
        httpPatch.addHeader("Content-Type", "application/json");
        JsonDocument docPatch; docPatch["efficiency"] = currentEff;
        String jsonBody; serializeJson(docPatch, jsonBody);
        httpPatch.sendRequest("PATCH", jsonBody);
        httpPatch.end();

        currentTemp = dht.readTemperature();
        if (!isnan(currentTemp)) sendSensorData(tempSensorId, currentTemp, "Temperature");

        detachInterrupt(digitalPinToInterrupt(VIBRATION_PIN));
        currentVib = vibrationCount; 
        vibrationCount = 0;
        attachInterrupt(digitalPinToInterrupt(VIBRATION_PIN), onVibration, FALLING);
        sendSensorData(vibSensorId, (float)currentVib, "Vibration");

        updateReactorStatus();
        updateDisplay(); 
    }
}