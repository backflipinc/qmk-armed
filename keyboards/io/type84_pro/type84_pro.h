// IO Type 84 Pro Wireless — общие определения для раскладок
#pragma once

#include "quantum.h"

// Клавиши беспровода (обрабатываются в wireless.c)
enum type84_keycodes {
    BT_1 = QK_KB_0, // слот Bluetooth 1..5: нажатие — переключиться, удержание 3 с — сопряжение
    BT_2,
    BT_3,
    BT_4,
    BT_5,
    RF_PAIR, // 2.4G: удержание 3 с — сопряжение с донглом
    SLEEP_T, // таймер автосна в BT/2.4G: 5 -> 10 -> 30 мин -> выкл -> 1 мин -> 5 ...
    // Интерфейс экрана (ui.c): в меню — навигация, вне меню ↑↓ яркость, ←→ эффект подсветки
    UI_UP,
    UI_DOWN,
    UI_LEFT,
    UI_RIGHT,
    UI_OK,   // Fn+Enter: открыть меню / выбрать / подтвердить
    UI_BACK, // Fn+Ctrl: назад / отмена
};

// Экран (type84_pro.c)
void type84_lcd_sleep(bool sleep);      // SLPIN/SLPOUT панели + подсветка
void type84_lcd_toast(const char *msg); // крупная надпись на 2 с вместо слоя

// Готовые цвета подсветки (type84_pro.c): RED ... WHITE, BLACK
typedef struct {
    const char *name;
    uint8_t     h, s;
} type84_color_t;
extern const type84_color_t type84_colors[];
extern const uint8_t        type84_colors_count;
int8_t                      type84_color_find(uint8_t h, uint8_t s); // -1 — не из списка

bool type84_is_black(hsv_t c); // «чёрный» из меню: H=0, S=1

// Второй цвет двухцветных эффектов и цвет подсветки Fn-слоя — номера в type84_colors
hsv_t   type84_color2(void);
uint8_t type84_color2_index(void);
void    type84_color2_set_index(uint8_t i);
hsv_t   type84_fn_color(void);
uint8_t type84_fn_color_index(void);
void    type84_fn_color_set_index(uint8_t i);
