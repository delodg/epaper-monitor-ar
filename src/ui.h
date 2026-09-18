#pragma once
#include <Arduino.h>

// Render de las secciones en la pantalla e-paper 200x200 (GxEPD2 + U8g2 para tipografías con acentos).
namespace UI {

void begin(bool coldBoot);                      // SPI + init del panel (refresco inicial sólo en arranque en frío)
void render(uint8_t page, bool fullRefresh);    // dibuja la sección y refresca (parcial o completo)
void renderSplash(const char* status);          // pantalla de arranque
void renderPortal();                            // instrucciones + QR del portal Wi-Fi
void renderMessage(const char* title, const char* line1, const char* line2, bool fullRefresh = true);
void hibernate();                               // deja el panel en deep-sleep (conserva la imagen)
void dumpBuffer(uint8_t page);                  // vuelca el buffer por serie (base64) para verlo en la PC
void dumpAllPages(uint8_t currentPage);         // dibuja y vuelca las 7 secciones (sin refrescar el panel)

}  // namespace UI
