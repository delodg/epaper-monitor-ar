#pragma once

// GPIO redefinibles con -D. Un periférico ausente usa -1 y nunca se maneja.
#if defined(BOARD_GENERIC_ESP32)
#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "El perfil ESP32 DevKit requiere un ESP32 clásico"
#endif
#define BOARD_NAME "ESP32 DevKit + EPD 1.54"
#define BOARD_ALWAYS_ON 1
#define HW_EPD_DC 27
#define HW_EPD_CS 5
#define HW_EPD_SCK 18
#define HW_EPD_MOSI 23
#define HW_EPD_RST 26
#define HW_EPD_BUSY 25
#define HW_BTN_PWR 33
#define HW_I2C_SDA 21
#define HW_I2C_SCL 22
#else
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Este perfil requiere ESP32-S3; agregá un perfil específico para tu chip"
#endif
#if defined(BOARD_GENERIC_S3)
#define BOARD_NAME "ESP32-S3 DevKit + EPD 1.54"
#define BOARD_ALWAYS_ON 1
#elif defined(EPD_PANEL_154G)
#define BOARD_NAME "Waveshare ePaper-1.54G"
#define BOARD_ALWAYS_ON 0
#elif defined(BOARD_WAVESHARE_V1)
#define BOARD_NAME "Waveshare ePaper-1.54 V1"
#define BOARD_ALWAYS_ON 0
#else
#define BOARD_NAME "Waveshare ePaper-1.54 V2"
#define BOARD_ALWAYS_ON 0
#endif
#define HW_EPD_DC 10
#define HW_EPD_CS 11
#define HW_EPD_SCK 12
#define HW_EPD_MOSI 13
#define HW_EPD_RST 9
#define HW_EPD_BUSY 8
#define HW_BTN_PWR 18
#define HW_I2C_SDA 47
#define HW_I2C_SCL 48
#endif

#ifndef PIN_EPD_DC
#define PIN_EPD_DC HW_EPD_DC
#endif
#ifndef PIN_EPD_CS
#define PIN_EPD_CS HW_EPD_CS
#endif
#ifndef PIN_EPD_SCK
#define PIN_EPD_SCK HW_EPD_SCK
#endif
#ifndef PIN_EPD_MOSI
#define PIN_EPD_MOSI HW_EPD_MOSI
#endif
#ifndef PIN_EPD_RST
#define PIN_EPD_RST HW_EPD_RST
#endif
#ifndef PIN_EPD_BUSY
#define PIN_EPD_BUSY HW_EPD_BUSY
#endif
#ifndef PIN_BTN_BOOT
#define PIN_BTN_BOOT 0
#endif
#ifndef PIN_BTN_PWR
#define PIN_BTN_PWR HW_BTN_PWR
#endif
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA HW_I2C_SDA
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL HW_I2C_SCL
#endif

#if BOARD_ALWAYS_ON
#define HW_EPD_PWR -1
#define HW_AUDIO_PWR -1
#define HW_VBAT_EN -1
#define HW_LED -1
#define HW_BAT_ADC -1
#else
#define HW_EPD_PWR 6
#define HW_AUDIO_PWR 42
#define HW_VBAT_EN 17
#define HW_LED 3
#define HW_BAT_ADC 4
#endif
#ifndef PIN_EPD_PWR
#define PIN_EPD_PWR HW_EPD_PWR
#endif
#ifndef PIN_AUDIO_PWR
#define PIN_AUDIO_PWR HW_AUDIO_PWR
#endif
#ifndef PIN_VBAT_EN
#define PIN_VBAT_EN HW_VBAT_EN
#endif
#ifndef PIN_LED
#define PIN_LED HW_LED
#endif
#ifndef PIN_BAT_ADC
#define PIN_BAT_ADC HW_BAT_ADC
#endif
#define PIN_RTC_INT 5
#define EPD_SPI_HZ 4000000
