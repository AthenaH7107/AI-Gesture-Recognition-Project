#include <AthenaH-project-1_inferencing.h>
#include <Adafruit_LSM9DS0.h>
#include <Adafruit_Sensor.h>
#include "WiFi.h"

#define SERIAL_PORT Serial
#define WIRE_PORT Wire
Adafruit_LSM9DS0 lsm = Adafruit_LSM9DS0();
#define BUTTON_PIN1 4
#define BUTTON_PIN2 5
#define BUTTON_PIN3 6

#define SAMPLE_RATE_MS 100
#define DEBOUNCE_MS 200

#include <esp_mac.h>
#define ESPNOW_WIFI_CHANNEL 6
#include <esp_now.h>
uint8_t receiver_mac[] = {0xA0,0x85,0xE3,0xDA,0xBF,0x20};

int x;
int y;
int codeSize = 4;
int currGuess = 0;
int lastGuess = 0;
int currentIndex = 0;

static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t feature_ix = 0;
volatile bool resetFeatures = false;

// Interrupts
struct Button {
  const uint8_t PIN;
  uint32_t numKeyPress;
  bool pressed;
  unsigned long lastPressTime;
};

Button buttonSend = {BUTTON_PIN1, 0, false, 0};
Button buttonConfirm = {BUTTON_PIN2, 0, false, 0};
Button buttonGuess = {BUTTON_PIN3, 0, false, 0};

void IRAM_ATTR sendISR() {
  unsigned long now = millis();
  if (now - buttonSend.lastPressTime > DEBOUNCE_MS) {
    buttonSend.pressed = true;
    buttonSend.numKeyPress++;
    buttonSend.lastPressTime = now;
  }
}
void IRAM_ATTR confirmISR() {
  unsigned long now = millis();
  if (now - buttonConfirm.lastPressTime > DEBOUNCE_MS) {
    buttonConfirm.pressed = true;
    buttonConfirm.numKeyPress++;
    buttonConfirm.lastPressTime = now;
  }
}
void IRAM_ATTR guessISR() {
  unsigned long now = millis();
  if (now - buttonGuess.lastPressTime > DEBOUNCE_MS) {
    buttonGuess.pressed = true;
    buttonGuess.numKeyPress++;
    buttonGuess.lastPressTime = now;
    resetFeatures = true;
  }
}

struct GesturePacket {
  uint8_t type;   // confirmedGuess or send
  uint8_t value;  // gestures
  uint8_t index;  // currentIndex
};
GesturePacket packet;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);  // give Serial time to connect before anything crashes
  Serial.println("Booting...");
  Serial.print("DSP frame size: ");
  Serial.println(EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());



  SERIAL_PORT.begin(115200);
  while (!SERIAL_PORT);
  WIRE_PORT.begin(21, 22);
  WIRE_PORT.setClock(400000);

  // ISM Setup
  if (!lsm.begin()) {
    SERIAL_PORT.println("Failed to initialize LSM9DS0!");
    while (1);
  }
  SERIAL_PORT.println("LSM9DS0 Connected!");
  lsm.setupAccel(lsm.LSM9DS0_ACCELRANGE_2G);
  lsm.setupMag(lsm.LSM9DS0_MAGGAIN_2GAUSS);
  lsm.setupGyro(lsm.LSM9DS0_GYROSCALE_245DPS);

  // Interrupt Setup
  Serial.println("Setting up interrupts...");
  pinMode(buttonSend.PIN, INPUT_PULLUP);
  attachInterrupt(buttonSend.PIN, sendISR, FALLING);
  Serial.println("Button 1 done");
  pinMode(buttonConfirm.PIN, INPUT_PULLUP);
  attachInterrupt(buttonConfirm.PIN, confirmISR, FALLING);
  Serial.println("Button 2 done");
  pinMode(buttonGuess.PIN, INPUT_PULLUP);
  attachInterrupt(buttonGuess.PIN, guessISR, FALLING);
  Serial.println("Button 3 done");

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }
  // ESP-NOW Setup
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiver_mac, 6);
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  Serial.println("Sender ready");
}

int print = 0;

void loop() {
  if (resetFeatures) {
    resetFeatures = false;
    feature_ix = 0;
  }

  if (buttonGuess.pressed) {
    if (print == 0) {
      Serial.println("Guess button pressed!");
      print++;
    }
    if (millis() - lastSend > SAMPLE_RATE_MS) {
      lastSend = millis();
      if (feature_ix + 6 <= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        sensors_event_t accel, gyro, mag, temp;
        lsm.getEvent(&accel, &gyro, &mag, &temp);

        features[feature_ix++] = accel.acceleration.x;
        features[feature_ix++] = accel.acceleration.y;
        features[feature_ix++] = accel.acceleration.z;
        features[feature_ix++] = gyro.gyro.x;
        features[feature_ix++] = gyro.gyro.y;
        features[feature_ix++] = gyro.gyro.z;
      }
      if (feature_ix >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        feature_ix = 0;
        buttonGuess.pressed = false;
        currGuess = getMLPrediction();
        Serial.print("Predicted: ");
        Serial.println(currGuess);
        print = 0;
      }
    }
  }

  if (currGuess != 0 && currGuess != lastGuess) {
    lastGuess = currGuess;
    packet.type = 0;
    packet.value = currGuess;
    packet.index = currentIndex;
    esp_now_send(receiver_mac, (uint8_t*)&packet, sizeof(packet));
    Serial.print("CurrGuess sent!");
    Serial.println(currGuess);
  }

  if (buttonConfirm.pressed) {
    buttonConfirm.pressed = false;
    packet.type = 1;
    packet.value = currGuess;
    packet.index = currentIndex;
    esp_now_send(receiver_mac, (uint8_t*)&packet, sizeof(packet));
    Serial.println("Confirm sent!");
    currentIndex++;
    if (currentIndex >= codeSize) {
      currentIndex = 0;
    }
    delay(10);
  }

  if (buttonSend.pressed) {
    buttonSend.pressed = false;
    packet.type = 2;
    packet.value = currGuess;
    packet.index = currentIndex;
    esp_now_send(receiver_mac, (uint8_t*)&packet, sizeof(packet));
    Serial.println("Answer sent!");
    delay(10);
  }
}

int getMLPrediction() {
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &get_feature_data;

  ei_impulse_result_t result;

  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

  if (res != EI_IMPULSE_OK) {
    Serial.print("Classifier error: ");
    Serial.println(res);
    return -1;
  }

  Serial.println("--- Predictions ---");
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.print("  ");
    Serial.print(result.classification[i].label);
    Serial.print(": ");
    Serial.println(result.classification[i].value, 4);
  }

  int best_index = 0;
  float best_value = 0;

  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > best_value) {
      best_value = result.classification[i].value;
      best_index = i;
    }
  }

  Serial.print("Best: ");
  Serial.print(result.classification[best_index].label);
  Serial.print(" (");
  Serial.print(best_value, 4);
  Serial.println(")");
  Serial.println("-------------------");
  return best_index;
}

int get_feature_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}