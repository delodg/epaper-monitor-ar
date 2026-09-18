#include "board.h"
#include "config.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <HWCDC.h>

namespace Board {

void earlyInit() {
  // 1) Configurar el estado deseado de los pines ANTES de soltar el "hold" del
  //    deep-sleep, así el rail de batería y la pantalla no tienen ningún glitch.
  pinMode(PIN_VBAT_EN, OUTPUT);
  digitalWrite(PIN_VBAT_EN, HIGH);      // latch: la placa se mantiene encendida a batería
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);       // pantalla alimentada
  pinMode(PIN_AUDIO_PWR, OUTPUT);
  digitalWrite(PIN_AUDIO_PWR, HIGH);    // audio apagado (ahorro)
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);          // LED apagado

  gpio_hold_dis((gpio_num_t)PIN_VBAT_EN);
  gpio_hold_dis((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_dis();

  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BTN_PWR, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
}

WakeReason wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT1: {
      uint64_t mask = esp_sleep_get_ext1_wakeup_status();
      if (mask & (1ULL << PIN_BTN_BOOT)) return WAKE_BTN_BOOT;
      if (mask & (1ULL << PIN_BTN_PWR))  return WAKE_BTN_PWR;
      return WAKE_OTHER;
    }
    case ESP_SLEEP_WAKEUP_UNDEFINED: return WAKE_COLD;
    default: return WAKE_OTHER;
  }
}

void ledOn()  { digitalWrite(PIN_LED, LOW); }
void ledOff() { digitalWrite(PIN_LED, HIGH); }
void ledBlink(uint8_t times, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < times; i++) { ledOn(); delay(onMs); ledOff(); delay(offMs); }
}

int batteryMilliVolts() {
  uint32_t sum = 0;
  const int N = 8;
  for (int i = 0; i < N; i++) { sum += analogReadMilliVolts(PIN_BAT_ADC); delayMicroseconds(200); }
  return (int)(sum / N) * 2;   // divisor resistivo 200k/200k
}

int batteryPercent(int mv) {
  // Curva de descarga Li-ion (aprox.) — pares (mV, %)
  static const int16_t curve[][2] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45}, {3750, 35}, {3700, 25}, {3650, 17}, {3600, 10}, {3500, 5}, {3400, 0}
  };
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (mv >= curve[0][0]) return 100;
  if (mv <= curve[n - 1][0]) return 0;
  for (int i = 0; i < n - 1; i++) {
    if (mv <= curve[i][0] && mv > curve[i + 1][0]) {
      float f = (float)(mv - curve[i + 1][0]) / (float)(curve[i][0] - curve[i + 1][0]);
      return (int)(curve[i + 1][1] + f * (curve[i][1] - curve[i + 1][1]) + 0.5f);
    }
  }
  return 0;
}

bool onUsbPower(int mv) { return mv > 4350; }

bool usbHostConnected() {
  // Un host USB activo manda un SOF cada 1 ms. El core de Arduino (HWCDC) ya vigila ese flag
  // desde un tick-hook de FreeRTOS (y lo borra cada tick, así que no conviene leerlo a mano):
  // arranca en "conectado" y pasa a "desconectado" tras ~6 ms sin SOF -> esperar antes de consultar.
  delay(30);
  return HWCDC::isPlugged();
}

bool bootPressed() { return digitalRead(PIN_BTN_BOOT) == LOW; }
bool pwrPressed()  { return digitalRead(PIN_BTN_PWR) == LOW; }

uint32_t measureHold(uint8_t pin, uint32_t maxMs) {
  uint32_t t0 = millis();
  bool ledLong = false, ledOff2 = false;
  while (digitalRead(pin) == LOW && millis() - t0 < maxMs) {
    uint32_t held = millis() - t0;
    if (!ledLong && held >= LONG_PRESS_MS) { ledOn(); ledLong = true; }              // feedback: pulsación larga
    if (!ledOff2 && held >= POWEROFF_PRESS_MS) { ledBlink(3, 40, 40); ledOff2 = true; } // feedback: apagado
    delay(10);
  }
  ledOff();
  return millis() - t0;
}

static void prepareButtonWake() {
  // BOOT (GPIO0) y PWR (GPIO18) por EXT1 (ANY_LOW): ambos son RTC-GPIO.
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_BOOT);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_BOOT);
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_PWR);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_PWR);
  esp_sleep_enable_ext1_wakeup((1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);
}

static void holdRails() {
  // Mantener el rail de batería y la pantalla encendidos durante el sueño.
  gpio_hold_en((gpio_num_t)PIN_VBAT_EN);
  gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_en();
}

void deepSleep(uint64_t sleepUs) {
  ledOff();
  Serial.flush();
  prepareButtonWake();
  esp_sleep_enable_timer_wakeup(sleepUs);
  holdRails();
  esp_deep_sleep_start();
}

void powerOff() {
  ledOff();
  Serial.flush();
  digitalWrite(PIN_EPD_PWR, HIGH);      // pantalla sin alimentación (conserva la imagen)
  digitalWrite(PIN_VBAT_EN, LOW);       // corta el rail: a batería la placa queda apagada
  gpio_hold_en((gpio_num_t)PIN_VBAT_EN);
  gpio_hold_en((gpio_num_t)PIN_EPD_PWR);
  gpio_deep_sleep_hold_en();
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_PWR);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_PWR);
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_BTN_PWR, ESP_EXT1_WAKEUP_ANY_LOW);  // con USB: despierta con PWR
  esp_deep_sleep_start();
}

}  // namespace Board
