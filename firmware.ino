#include <Wire.h>
#include <Adafruit_INA219.h>

#define PCAADDR 0x70
#define NUM_PANELS 3
#define SSR_PIN D5

Adafruit_INA219 ina219;

// Calibrated voltage thresholds for indoor lighting
const float V_NOMINAL_MIN = 2.40;  // > 2.40V is Healthy / Nominal MPP (Green)
const float V_SHADING_MIN = 1.30;  // 1.30V - 2.40V is Partial Shading (Amber)
                                   // < 1.30V is Line Fault / Disconnected (Red)

void pcaSelect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(PCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

void pcaDeselect() {
  Wire.beginTransmission(PCAADDR);
  Wire.write(0);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // ESP8266 default I2C pins: D2 (SDA), D1 (SCL)
  Wire.begin(D2, D1);
  Wire.setClock(50000); // 50kHz conservative clock for bus stability

  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, HIGH); // Armed / Closed by default

  pcaDeselect();
  delay(50);

  // Initialize INA219 on channels 0, 1, and 2
  for (uint8_t i = 0; i < NUM_PANELS; i++) {
    pcaSelect(i);
    delay(15);
    ina219.begin();
    delay(15);
  }
  pcaDeselect();
}

void loop() {
  float voltages[NUM_PANELS] = {0.0};
  int faultCodes[NUM_PANELS] = {0};

  for (uint8_t i = 0; i < NUM_PANELS; i++) {
    pcaSelect(i);
    delay(5);

    float v = ina219.getBusVoltage_V();
    if (isnan(v) || v < 0.0) v = 0.0;
    voltages[i] = v;

    // Independent panel classification
    if (v >= V_NOMINAL_MIN) {
      faultCodes[i] = 0; // Nominal MPP (Green)
    } else if (v >= V_SHADING_MIN) {
      faultCodes[i] = 1; // Partial Shading (Amber)
    } else {
      faultCodes[i] = 2; // Fault / Line Open (Red)
    }

    pcaDeselect();
  }

  // Trip SSR only if Panel 2 drops into critical shading/fault
  bool tripCondition = (voltages[1] < V_SHADING_MIN);

  if (tripCondition) {
    digitalWrite(SSR_PIN, LOW);  // Disconnect / Trip array
  } else {
    digitalWrite(SSR_PIN, HIGH); // Re-arm array
  }

  // Transmit JSON packet over Web Serial
  Serial.print("{\"trip\":");
  Serial.print(tripCondition ? 1 : 0);
  Serial.print(",\"panels\":[");
  for (uint8_t i = 0; i < NUM_PANELS; i++) {
    Serial.print("{\"id\":");
    Serial.print(i + 1);
    Serial.print(",\"v\":");
    Serial.print(voltages[i], 2);
    Serial.print(",\"c\":");
    Serial.print(faultCodes[i]);
    Serial.print("}");
    if (i < NUM_PANELS - 1) Serial.print(",");
  }
  Serial.println("]}");

  delay(350);
}