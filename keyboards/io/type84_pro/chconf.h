// ChibiOS: системный таймер — периодический SysTick вместо CT16B0.
// По умолчанию порт SN32F290 держит системное время на CT16B0 (бестиковый режим),
// а CT16B0 нужен ШИМу колонок подсветки (ledscan.c) — как в стоке.
#pragma once

#include_next <chconf.h>

#undef CH_CFG_ST_TIMEDELTA
#define CH_CFG_ST_TIMEDELTA 0 // периодический тик -> SysTick (hal_st_lld.c)

#undef CH_CFG_ST_FREQUENCY
#define CH_CFG_ST_FREQUENCY 10000 // тик 100 мкс

#undef CH_CFG_ST_RESOLUTION
#define CH_CFG_ST_RESOLUTION 32
