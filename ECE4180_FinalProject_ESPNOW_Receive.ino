#include <Arduino.h>
#include <esp_now.h>
#include "WiFi.h"
#include <Goldelox_Serial_4DLib.h>
#include <Goldelox_Const4D.h>
#include <esp_mac.h>
#include <vector>

#define DisplaySerial Serial1
Goldelox_Serial_4DLib Display(&DisplaySerial);

#define ESPNOW_WIFI_CHANNEL 6

int code[6];
int codeSize = 4;

typedef struct {
  uint8_t type;
  uint8_t value;
  uint8_t index;
} GesturePacket;

int lastGuess = 0;
int currGuess = 0;
int currIndex = 0;
int packetType = 0;
int guess[4] = {0};
int confirmed[4] = {0};
bool guessRowDirty = false;

void drawTriangle(int x, int y, uint16_t color, int dir) {
  /* Notes on drawing triangles: 128 / 6 = 21.3. Center of each "frame": 0 - 10 - 20, 20 - 30 - 40, 40 - 50 - 60,
  60 - 70 - 80, 80 - 90 - 100, 100 - 110 - 120. 
  Up triangles: x - 5, x, x + 5, y + 8, y - 8, y + 8
  Down triangles: x - 5, x, x + 5, y - 8, y + 8, y - 8
  Left triangles: x - 8, x + 8, x - 8, y - 5, y, y + 5
  Right triangles: x + 8, x - 8, x + 8, y - 5, y, y + 5
  */
  // MODEL SAYS RIGHT=3, UP=4, LEFT=2, DOWN=0, and IDLE = 1
  if (dir == 4) {
    Display.gfx_Triangle(x - 5, y + 8, x, y - 8, x + 5, y + 8, color);
  } else if (dir == 0) {
    Display.gfx_Triangle(x - 5, y - 8, x, y + 8, x + 5, y - 8, color);
  } else if (dir == 3) {
    Display.gfx_Triangle(x - 8, y - 5, x + 8, y, x - 8, y + 5, color);
  } else if (dir == 2) {
    Display.gfx_Triangle(x + 8, y - 5, x - 8, y, x + 8, y + 5, color);
  } else {
    //draw nothing
  }
}

void generateCode(int size) {
  for (int i = 0; i < size; i++) {
    code[i] = random(1, 5);
  }
}

boolean checkGuess(int listSize) {
  Serial.print("Actual Code: ");
  for (int i = 0; i < listSize; i++) {
    Serial.print(code[i]);
  }
  Serial.println();
  Serial.print("Guess: ");
  for (int i = 0; i < listSize; i++) {
    Serial.print(confirmed[i]);
  }
  Serial.println();
  for (int i = 0; i < listSize; i++) {
    if (code[i] != confirmed[i]) {
      return false;
    }
  }
  return true;
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  GesturePacket packet;
  memcpy(&packet, data, sizeof(packet));

  packetType = packet.type;
  currIndex = packet.index;

  if (packet.type == 0) {
    Serial.println("Current Guess Received!");
    currGuess = packet.value;
    guessRowDirty = true;
  }

  if (packet.type == 1) {
    Serial.println("Confirmed Guess Received!");
    if (packet.index < 4) {
      confirmed[packet.index] = packet.value;
      guess[packet.index] = packet.value;
      guessRowDirty = true;
    }
  }

  if (packet.type == 2) {
    Serial.println("Checking answer...");
    bool win = checkGuess(codeSize);
    if (win) {
      Display.gfx_CircleFilled(64, 64, 20, GREEN);
    } else {
      Display.gfx_CircleFilled(64, 64, 20, RED);
    }
  }
}

void redrawGuessRow() {
  for (int i = 0; i < codeSize; i++) {
    int locationIndex = 20 + i * 20;
    drawTriangle(locationIndex, 40, BLACK, guess[i]);
  }

  for (int i = 0; i < codeSize; i++) {
    int locationIndex = 20 + i * 20;
    if (confirmed[i] != 0) {
      drawTriangle(locationIndex, 40, GREEN, confirmed[i]);
    } 
    else if (i == currIndex) {
      drawTriangle(locationIndex, 40, BLUE, currGuess);
    }
  }

  for (int i = 0; i < currIndex; i++) {
    guess[i] = confirmed[i];
  }
  guess[currIndex] = currGuess;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) {
    delay(100);
  }
  //Serial.print("Receiver MAC: ");
  //Serial.println(WiFi.macAddress());
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  Serial.println("Receiver ready");

  // uLCD Setup
  DisplaySerial.begin(9600, SERIAL_8N1, 4, 5);
  Display.TimeLimit4D = 500;
  Display.gfx_Cls();

  // Initializing Game
  generateCode(codeSize);
  Serial.println("Code generated!");
  for (int i = 0; i < codeSize; i++) {
    int locationIndex = 20 + i * 20;
    drawTriangle(locationIndex, 20, WHITE, code[i]);
  }
}

void loop() {
  if (guessRowDirty) {
    guessRowDirty = false;
    redrawGuessRow();
  }
}