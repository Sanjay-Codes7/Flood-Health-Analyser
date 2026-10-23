// ESP32 + MAX30102 -> JSON over USB serial and Wi-Fi (http://192.168.4.1/vitals)
// Library to install: "SparkFun MAX3010x Pulse and Proximity Sensor Library"
// Sensor wiring: VIN->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
// LED wiring (each through a 220 ohm resistor to GND): RED=GPIO25, YELLOW=GPIO26, GREEN=GPIO27
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "MAX30105.h"          // this library class also supports the MAX30102
#include "spo2_algorithm.h"

MAX30105 sensor;
WebServer server(80);
uint32_t irBuf[100], redBuf[100];
int32_t spo2 = 0, hr = 0;
int8_t spo2Ok = 0, hrOk = 0;
int outHr = 0, outSpo2 = 0;

const int LED_R = 25, LED_Y = 26, LED_G = 27;

void setLed(char c) {   // R, Y, G, or O (all off)
  digitalWrite(LED_R, c == 'R');
  digitalWrite(LED_Y, c == 'Y');
  digitalWrite(LED_G, c == 'G');
}

void handleLed() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String c = server.arg("c");
  if (c.length()) setLed(c[0]);
  server.send(200, "text/plain", "ok");
}

void handleVitals() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json",
    "{\"hr\":" + String(outHr) + ",\"spo2\":" + String(outSpo2) + "}");
}

void readSamples(int from) {
  for (int i = from; i < 100; i++) {
    while (!sensor.available()) sensor.check();
    redBuf[i] = sensor.getRed();
    irBuf[i] = sensor.getIR();
    sensor.nextSample();
    server.handleClient();                       // keep Wi-Fi responsive
    if (Serial.available()) {                    // USB LED commands: R/Y/G/O
      char ch = Serial.read();
      if (ch == 'R' || ch == 'Y' || ch == 'G' || ch == 'O') setLed(ch);
    }
  }
}

void calc() {
  maxim_heart_rate_and_oxygen_saturation(irBuf, 100, redBuf, &spo2, &spo2Ok, &hr, &hrOk);
  bool finger = irBuf[99] > 50000;               // no finger -> report 0
  outHr   = (finger && hrOk   && hr > 30 && hr < 220)      ? hr   : 0;
  outSpo2 = (finger && spo2Ok && spo2 > 70 && spo2 <= 100) ? spo2 : 0;
  Serial.printf("{\"hr\":%d,\"spo2\":%d}\n", outHr, outSpo2);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT); pinMode(LED_Y, OUTPUT); pinMode(LED_G, OUTPUT);
  setLed('R'); delay(300); setLed('Y'); delay(300); setLed('G'); delay(300); setLed('O');  // startup test

  WiFi.softAP("TRIAGE-ESP32", "12345678");       // phone/laptop joins this network
  server.on("/vitals", handleVitals);
  server.on("/led", handleLed);
  server.begin();

  Wire.begin(21, 22);
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {     // sensor missing or wired wrongly
    while (1) {
      Serial.println("MAX30102 not found - check wiring");
      setLed('R'); delay(500); setLed('O'); delay(500);
      server.handleClient();
    }
  }
  sensor.setup(60, 4, 2, 100, 411, 4096);        // LED brightness, avg, mode, rate, pulse, adc

  readSamples(0);
  calc();
}

void loop() {
  for (int i = 25; i < 100; i++) { redBuf[i - 25] = redBuf[i]; irBuf[i - 25] = irBuf[i]; }
  readSamples(75);
  calc();
  server.handleClient();
}
