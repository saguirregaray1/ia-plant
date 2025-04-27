#include <FS.h>
#include <SPIFFS.h>

const char* kFile = "/data.csv";

void dumpCSV() {
  if (!SPIFFS.exists(kFile)) {
    Serial.println("❌ File not found.");
    return;
  }

  File f = SPIFFS.open(kFile, FILE_READ);
  if (!f) {
    Serial.println("❌ Failed to open file.");
    return;
  }

  Serial.println("─── BEGIN /data.csv ───");
  while (f.available()) Serial.write(f.read());
  Serial.println("\n───  END  /data.csv ───");
  f.close();
}

void listDir() {
  Serial.println("── DIR ──");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.printf("%-12s %6u bytes\n", file.name(), file.size());
    file = root.openNextFile();
  }
  Serial.println("──────────\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!SPIFFS.begin(true)) {          // true = format if mount fails
    Serial.println("❌ SPIFFS mount failed (check partition scheme!)");
    while (true) delay(1000);
  }
  Serial.println("✅ SPIFFS mounted");
  listDir();

  Serial.println("Press any key to dump /data.csv …");
}

void loop() {
  if (Serial.available()) {           // key pressed → dump
    Serial.read();                    // consume byte
    dumpCSV();
    Serial.println("Press any key to dump again …");
  }
  delay(20);
}