// Интерфейс дисплея: главный экран, меню, всплывашки (ui.c)
#pragma once

#include <stdbool.h>
#include "quantum.h"
#include "qp.h"

void ui_init(painter_device_t lcd);
void ui_task(void);
void ui_redraw(void);              // полная перерисовка (после сна панели и т.п.)
void ui_toast(const char *msg);    // крупная надпись на 1,5 с
bool ui_menu_active(void);
bool ui_process(uint16_t keycode, keyrecord_t *record); // false — событие съедено
