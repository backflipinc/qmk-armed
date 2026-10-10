// Беспроводные режимы: радиомодуль BT + 2.4G на UART2 (docs/uart-protocol.md)
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WL_MODE_USB,
    WL_MODE_BT,
    WL_MODE_24G,
} wl_mode_t;

typedef enum {
    WL_LINK_UNKNOWN,      // модуль ещё ничего не сообщил
    WL_LINK_DISCONNECTED, // 5B 31
    WL_LINK_CONNECTED,    // 5A xx / 5B 32
    WL_LINK_PAIRING,      // 5B 33
} wl_link_t;

void wireless_init(void);
void wireless_task(void);

wl_mode_t wireless_mode(void);
wl_link_t wireless_link(void);
uint8_t   wireless_bt_slot(void); // 1..5
uint8_t   wireless_sleep_minutes(void); // 0 — автосон выключен

// Для меню
void    wireless_select_slot(uint8_t slot); // 1..5
bool    wireless_pair(void);                // false — в режиме USB
uint8_t wireless_sleep_index(void);
void    wireless_sleep_set_index(uint8_t idx);
uint8_t wireless_sleep_option_count(void);
uint8_t wireless_sleep_option_minutes(uint8_t idx); // 0 — выкл

// Клавиши BT_1..BT_5 / RF_PAIR (type84_pro.h): нажатие — слот, удержание 3 с — сопряжение
bool process_record_wireless(uint16_t keycode, bool pressed);
