#pragma once
#include <Arduino.h>

// Manejo de la placa: rails de energía, LED, batería, botones y deep-sleep.
namespace Board {

enum WakeReason { WAKE_COLD, WAKE_TIMER, WAKE_BTN_BOOT, WAKE_BTN_PWR, WAKE_OTHER };

void       earlyInit();                 // llamar PRIMERO en setup(): latch de batería + rails
WakeReason wakeReason();

void ledOn();
void ledOff();
void ledBlink(uint8_t times, uint16_t onMs = 60, uint16_t offMs = 60);

int  batteryMilliVolts();               // promedio de varias lecturas, ya multiplicado x2
int  batteryPercent(int mv);            // 0..100 (curva Li-ion aproximada)
bool onUsbPower(int mv);                // heurística: tensión > 4.35 V => alimentado por USB
bool usbHostConnected();                // true si hay una PC enumerando el USB (paquetes SOF)

bool     bootPressed();
bool     pwrPressed();
uint32_t measureHold(uint8_t pin, uint32_t maxMs);  // ms que el botón sigue apretado (con feedback LED)

void deepSleep(uint64_t sleepUs);       // mantiene rails, despierta por timer o botones
extern void (*onBeforeSleep)(uint64_t sleepUs);   // gancho para anotar estadísticas
void powerOff();                        // apaga rail de batería; sólo despierta con PWR

}  // namespace Board
