#include <math.h>
#include "DHT.h"
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

/* === ML model === */
extern "C" {
  #include "rf.h"
}

#define N_FEAT   5
#define N_CLASS  3


/* === Wi‑Fi === */
const char* ssid     = "CAMPAS";
const char* password = "Bianca1221";
WebServer server(80);

/* === GPIO & sensors === */
const int soilPin = 33;
const int ldrPin  = 27;
#define DHTPIN  14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* === timing === */
unsigned long lastSensorReading = 0;
const unsigned long SENSOR_INTERVAL = 900000UL; // 15 mins

/* === Constants === */
const float DHT_ERROR_VALUE = -999.0f;
const int MAX_ANALOG_VALUE = 4095;
const int SOIL_MIN = 913;
const int SOIL_MAX = 4095;
const int LIGHT_MIN = 745;
const int LIGHT_MAX = 4095;
const float TEMP_MIN = 16.3f;
const float TEMP_MAX = 24.7f;

/* === Helpers === */
float readTemperature() { 
    float temp = dht.readTemperature(); 
    if (isnan(temp)) {
        Serial.println("❌ DHT11 read error");
        return DHT_ERROR_VALUE;
    }
    return temp;
}

bool writeLog(uint32_t timeMs, int soilRaw, int lightRaw, float tempC, int predCls)
{
    File f = SPIFFS.open("/data.csv", FILE_APPEND);
    if (!f) { 
        Serial.println("❌ Cannot open /data.csv for append"); 
        return false; 
    }
    
    size_t bytesWritten = f.printf("%lu,%d,%d,%.2f,%d\n", 
                                   timeMs, soilRaw, lightRaw, tempC, predCls);
    f.close();
    
    if (bytesWritten == 0) {
        Serial.println("❌ Failed to write to CSV");
        return false;
    }
    
    return true;
}

int findMaxProbabilityClass(const double probs[N_CLASS]) {
    int maxClass = 0;
    for (int i = 1; i < N_CLASS; ++i) {
        if (probs[i] > probs[maxClass]) {
            maxClass = i;
        }
    }
    return maxClass;
}

bool isValidAnalogReading(int value) {
    return (value >= 0 && value <= MAX_ANALOG_VALUE);
}

static void buildFeatureVector(double soilRaw, double lightRaw, double tempC,
                                 double out[N_FEAT])
{

   // MinMaxScaling
    double soil_n  = ( (double)soilRaw  - (double)SOIL_MIN ) 
                   / (double)(SOIL_MAX - SOIL_MIN);

    double light_n = ( (double)lightRaw - (double)LIGHT_MIN ) 
                    / (double)(LIGHT_MAX - LIGHT_MIN);

    double temp_n  = ( (double)tempC  - (double)TEMP_MIN ) 
                    / (double)(TEMP_MAX - TEMP_MIN);

    soil_n  = constrain(soil_n,  0.0, 1.0);
    light_n = constrain(light_n, 0.0, 1.0);
    temp_n  = constrain(temp_n,  0.0, 1.0);

    // Get time
    time_t now = time(NULL);
    int hr;
    if (now < 100000) {
        hr = (millis() / 3600000UL) % 24;
        Serial.println("⚠️  Using millis() for hour (SNTP not synced)");
    } else {
        struct tm *t = localtime(&now);
        hr = t->tm_hour;
    }
    
    double hour_sin = sin(2.0 * M_PI * hr / 24.0);
    double hour_cos = cos(2.0 * M_PI * hr / 24.0);

    // Fill feature vector
    out[0] = soil_n;
    out[1] = light_n;
    out[2] = temp_n;
    out[3] = hour_sin;
    out[4] = hour_cos;
    
}

/* === Web Server Handlers === */
void handleRoot() {
    String html = "<!DOCTYPE html><html><body>";
    html += "<h1>🌱 ESP32 Sensor Data (MinMaxScaler)</h1>";
    html += "<p><a href='/download'>Download CSV</a></p>";
    html += "<p><a href='/status'>View Status</a></p>";
    html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    html += "<p>Uptime: " + String(millis() / 1000) + " seconds</p>";
    html += "<p>Free heap: " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleDownload() {
    File file = SPIFFS.open("/data.csv", "r");
    if (!file) { 
        server.send(404, "text/plain", "❌ File not found"); 
        return; 
    }
    
    server.sendHeader("Content-Disposition", 
                     "attachment; filename=sensor_data.csv");
    server.streamFile(file, "text/csv");
    file.close();
    
    Serial.println("📁 CSV file downloaded by client");
}

void handleStatus() {
    int soilRaw  = analogRead(soilPin);
    int lightRaw = analogRead(ldrPin);
    float temperature = readTemperature();

    bool validReadings = true;
    String errorMsg = "";
    
    if (!isValidAnalogReading(soilRaw)) {
        validReadings = false;
        errorMsg += "Invalid soil reading. ";
    }
    
    if (!isValidAnalogReading(lightRaw)) {
        validReadings = false;
        errorMsg += "Invalid light reading. ";
    }
    
    if (temperature == DHT_ERROR_VALUE) {
        validReadings = false;
        errorMsg += "DHT sensor error. ";
    }

    String html = "<!DOCTYPE html><html><body>";
    html += "<h1>Current Status</h1>";
    
    if (!validReadings) {
        html += "<p style='color: red;'>❌ " + errorMsg + "</p>";
        html += "<p><a href='/'>Back</a></p></body></html>";
        server.send(200, "text/html", html);
        return;
    }

    double x[N_FEAT];
    buildFeatureVector(soilRaw, lightRaw, temperature, x);
    
    double probs[N_CLASS];
    score(x, probs);
    
    int pred = findMaxProbabilityClass(probs);

    Serial.println("--- /status debug ---");
    Serial.printf("Raw readings -> Soil: %d, Light: %d, Temp: %.2f°C\n", 
                  soilRaw, lightRaw, temperature);
    Serial.printf("Feature vector -> [%.3f, %.3f, %.3f, %.3f, %.3f]\n", 
                  x[0], x[1], x[2], x[3], x[4]);

    html += "<h3>Raw Sensor Readings:</h3>";
    html += "<p>Soil moisture: " + String(soilRaw) + "</p>";
    html += "<p>Light level: " + String(lightRaw) + "</p>";
    html += "<p>Temperature: " + String(temperature, 2) + "C</p>";
    
    html += "<h3>ML Inference (MinMaxScaler):</h3>";
    html += "<pre>Feature vector = [" + String(x[0],3) + ", " + String(x[1],3) + ", " + String(x[2],3) + ", " + String(x[3],3) + ", " + String(x[4],3) + "]\n";
    html += "Probabilities  = [" + String(probs[0],3) + ", " + String(probs[1],3) + ", " + String(probs[2],3) + "]</pre>";
    html += "<p>Predicted class: <strong>" + String(pred) + "</strong></p>";
    
    html += "<p><a href='/'>Back</a></p></body></html>";
    server.send(200, "text/html", html);
}

void handleNotFound() { 
    server.send(404, "text/plain", "❌ Page not found"); 
}


/* === Setup === */
void setup() {
    Serial.begin(115200);
    Serial.println("\n🚀 ESP32 Sensor + ML Logger (MinMaxScaler) starting...");
    
    dht.begin();
    delay(2000);

    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS mount failed");
        while (true) {
            delay(1000);
            Serial.print(".");
        }
    }
    Serial.println("✅ SPIFFS mounted");

    if (!SPIFFS.exists("/data.csv") || SPIFFS.open("/data.csv").size() == 0) {
        File f = SPIFFS.open("/data.csv", FILE_WRITE);
        if (f) { 
            f.println("time_ms,soil_raw,light_raw,temp_c,pred_class"); 
            f.close();
            Serial.println("✅ Created CSV file with header");
        } else {
            Serial.println("❌ Failed to create CSV file");
        }
    }

    Serial.printf("🌐 Connecting to WiFi: %s", ssid);
    WiFi.begin(ssid, password);
    
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 30) { 
        delay(500); 
        Serial.print('.'); 
        wifiTimeout++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n❌ WiFi connection failed!");
    } else {
        Serial.println("\n✅ WiFi connected");
        Serial.printf("📡 IP address: %s\n", WiFi.localIP().toString().c_str());
    }

    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("🕐 Configuring NTP...");

    server.on("/", handleRoot);
    server.on("/download", handleDownload);
    server.on("/status", handleStatus);
    server.onNotFound(handleNotFound);
    server.begin();
    
    Serial.println("🌐 Web server started");
    Serial.println("📊 System ready! Using MinMaxScaler normalization...");
}

/* === Loop === */
void loop() {
    server.handleClient();

    if (millis() - lastSensorReading >= SENSOR_INTERVAL) {
        Serial.println("\n----- Starting sensor reading cycle -----");
        
        int soilRaw = analogRead(soilPin);
        int lightRaw = analogRead(ldrPin);
        float temperature = readTemperature();

        bool validReadings = true;
        
        if (!isValidAnalogReading(soilRaw)) {
            Serial.printf("❌ Invalid soil reading: %d\n", soilRaw);
            validReadings = false;
        }
        
        if (!isValidAnalogReading(lightRaw)) {
            Serial.printf("❌ Invalid light reading: %d\n", lightRaw);
            validReadings = false;
        }
        
        if (temperature == DHT_ERROR_VALUE) {
            Serial.println("❌ Temperature sensor error");
            validReadings = false;
        }

        if (!validReadings) {
            Serial.println("❌ Skipping ML inference due to invalid readings");
            lastSensorReading = millis();
            return;
        }

        double x[N_FEAT];
        buildFeatureVector(soilRaw, lightRaw, temperature, x);
        
        double probs[N_CLASS];
        score(x, probs);
        
        int predictedClass = findMaxProbabilityClass(probs);

        Serial.println("✅ Sensor readings successful:");
        Serial.printf("   Raw: Soil=%d, Light=%d, Temp=%.2f°C\n", 
                      soilRaw, lightRaw, temperature);
        Serial.printf("   Normalized: [%.3f, %.3f, %.3f, %.3f, %.3f]\n",
                      x[0], x[1], x[2], x[3], x[4]);
        Serial.printf("   Probabilities: [%.3f, %.3f, %.3f]\n", 
                      probs[0], probs[1], probs[2]);
        Serial.printf("   Predicted class: %d\n", predictedClass);

        if (writeLog(millis(), soilRaw, lightRaw, temperature, predictedClass)) {
            Serial.println("✅ Data logged to CSV");
        } else {
            Serial.println("❌ Failed to log data");
        }
        
        lastSensorReading = millis();
        Serial.println("----- Sensor reading cycle complete -----\n");
    }
    
    delay(100);
}