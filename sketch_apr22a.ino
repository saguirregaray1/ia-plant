#include <math.h>
#include "DHT.h"
#include <FS.h>
#include <SPIFFS.h>

const int soilPin = 33;        // AO → ADC1 channel (GPIO13)
const int dryADC  = 4095;      // raw ADC when probe is in air
const int wetADC  = 950;       // raw ADC when probe is in water

const int ldrPin  = 27;        // light sensor LDR

#define DHTPIN  14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* ------------ helpers -------------- */
float readTemperature() { return dht.readTemperature(); }

void writeLog(uint32_t timeMs,
              int soilRaw,
              int lightRaw,
              float tempC)
{
  File f = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!f) {
    Serial.println("Cannot open /data.csv for append!");
    return;
  }
  // time_ms,soil_raw,light_raw,temp_c
  f.printf("%lu,%d,%d,%.2f\n", timeMs, soilRaw, lightRaw, tempC);
  f.close();
}

/* -------------- setup -------------- */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Sensor Reader Starting...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    while (true) delay(1000);
  }

  // Add CSV header if file is new/empty
  if (!SPIFFS.exists("/data.csv") || SPIFFS.open("/data.csv").size() == 0) {
    File f = SPIFFS.open("/data.csv", FILE_WRITE);
    if (f) {
      f.println("time_ms,soil_raw,light_raw,temp_c");
      f.close();
    }
  }
}

/* --------------- loop -------------- */
void loop() {
  /* --- Soil --- */
  int soilRaw = analogRead(soilPin);
  int soilPct = map(soilRaw, dryADC, wetADC, 0, 100);
  soilPct     = constrain(soilPct, 0, 100);

  /* --- Light --- */
  int lightRaw = analogRead(ldrPin);
  int lightPct = map(lightRaw, 570, 4095, 0, 100);
  lightPct     = constrain(lightPct, 0, 100);

  /* --- Temperature --- */
  float temperature = readTemperature();

  /* --- Serial monitor --- */
  Serial.println("----- Lecturas -----");
  Serial.printf("Humedad suelo : Raw = %4d (%3d%%)\n", soilRaw,  soilPct);
  Serial.printf("Luz ambiente  : Raw = %4d (%3d%%)\n", lightRaw, lightPct);
  Serial.printf("Temperatura   : %.2f °C\n", temperature);
  Serial.println("--------------------\n");

  /* --- Write only absolute values --- */
  writeLog(millis(), soilRaw, lightRaw, temperature);

  delay(900000);
}
