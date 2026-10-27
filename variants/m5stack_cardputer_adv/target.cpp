#include <Arduino.h>
#include "target.h"

// Ported from MeshCore-Cardputer-ADV (variants/m5stack_cardputer), Cap LoRa-1262 only.
// Headless: no display, keyboard UI or GPS - the bridge is driven over USB serial.

M5CardputerBoard board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi, SPISettings());

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // Hard reset so the module comes up clean even if the Cap was powered late
  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, LOW);
  delay(10);
  digitalWrite(P_LORA_RESET, HIGH);
  delay(100);

  if (!radio.std_init(&spi)) {
    Serial.println("[LoRa] ERROR: SX1262 init failed");
    return false;
  }
  // Cap LoRa-1262 has a MAX2659 PA after the SX1262
  int16_t pa = radio.setPaConfig(0x04, 0x07, 0x00, 0x01);
  Serial.printf("[LoRa] SX1262 ok, PA config %s (%d)\n", pa == 0 ? "ok" : "FAILED", pa);
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
