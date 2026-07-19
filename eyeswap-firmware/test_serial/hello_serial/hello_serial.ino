// ==========================================
// EyeSwap Serial Test — ESP32-S3
// Bare-minimum "can this board talk"
// ==========================================

void setup() {
  Serial.begin(115200);
  
  // Wait for serial port to connect (native USB boards like S3)
  while (!Serial) { ; }
  
  delay(500);
  
  Serial.println("\n\n========================================");
  Serial.println("  EyeSwap Serial Test");
  Serial.println("  If you can read this, baud rate OK");
  Serial.println("========================================");
  Serial.print("  Chip Model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("  CPU Freq: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  Serial.print("  Flash Size: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");
  Serial.println("========================================\n");
}

void loop() {
  static unsigned long lastPrint = 0;
  static int count = 0;
  
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    count++;
    
    Serial.print("[TEST] Tick ");
    Serial.print(count);
    Serial.print(" | Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println("s");
  }
}
