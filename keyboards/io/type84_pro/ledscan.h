// Матрица клавиш + RGB-подсветка в прерывании CT16B3 (ledscan.c)
#pragma once

#include <stdint.h>

void ledscan_start(void); // после глубокого сна
void ledscan_stop(void);  // перед глубоким сном: всё погашено, колонки — GPIO LOW

// Цвет в задний буфер (строка/колонка матрицы), 0..255; показать кадр — ledscan_update_lit()
void ledscan_set(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b);
void ledscan_update_lit(void);

// Светодиоды вне RGB Matrix (полоса под дисплеем): сразу на вывод
void ledscan_set_direct(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b);
