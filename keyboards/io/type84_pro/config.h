#pragma once

/* SPI0 -> дисплей. Пины переназначены через PFPA (вариант 3), см. keyboard_pre_init_kb() */
#define SPI_DRIVER SPID0
#define SPI_SCK_PIN D0   // P3.0
#define SPI_MOSI_PIN D2  // P3.2
#define SPI_MISO_PIN D1  // P3.1, дисплей не читается, но так настроено в стоке

/* Дисплей ST7789, панель 172x320 (в стоке используется горизонтально 320x172) */
#define LCD_CS_PIN B8    // P1.8, ручной CS
#define LCD_DC_PIN D14   // P3.14, LOW = команда
#define LCD_RST_PIN A17  // P0.17
#define LCD_BL_PIN A16   // P0.16, HIGH = подсветка вкл

#define LCD_NATIVE_WIDTH 172
#define LCD_NATIVE_HEIGHT 320
#define LCD_OFFSET_X 0   // смещение в повёрнутых координатах (как в стоке, MADCTL 0x60)
#define LCD_OFFSET_Y 34
#define LCD_SPI_MODE 0   // CPOL=0, CPHA=0 (как в стоке)
#define LCD_SPI_DIVISOR 4 // 48/4 = 12 МГц; в стоке 2 (24 МГц) — поднять после проверки

// Встроенный таймер QP гасит дисплей (DISPOFF без подсветки) через 30 с простоя.
// Экран гаснет только вместе с клавиатурой: USB-сон хоста или автосон BT/2.4G (wireless.c)
#define QUANTUM_PAINTER_DISPLAY_TIMEOUT 0

// Опрос счётчика заряда (quantum/battery/battery.c)
#define BATTERY_SAMPLE_INTERVAL 10000

// Имена эффектов подсветки для меню (ui.c)
#define RGB_MATRIX_MODE_NAME_ENABLE
