// Страховки на время разработки: как вернуться в ROM-бутлоадер Sonix,
// если прошивка зависла или повела себя не так.
//
// 1. Ранний выход: зажатый Esc при подключении -> ROM-бутлоадер.
//    Проверяется самым первым делом после сброса, напрямую через регистры,
//    до настройки тактов, ChibiOS, USB и дисплея.
// 2. Сторожевой таймер (~4 с, кормится из housekeeping_task_kb в type84_pro.c): если главный цикл перестал крутиться,
//    WDT сбрасывает чип, а ранний код видит причину сброса и уходит в ROM.
// 3. QK_BOOT выключает WDT перед перезагрузкой в бутлоадер: ROM его не кормит.
//
// Регистры — SN32F290.h / SVD Sonix, формула WDT — даташит SN32F260 §7:
//   интервал = 31.25 мкс (ILRC 32 кГц) * WDTPRE * 128 * (TC + 1)

#include "quantum.h"
#include "bootloader.h"

#define WDT_KEY 0x5AFA0000UL
#define WDT_FEED_VALUE (WDT_KEY | 0x55AAUL)
#define WDT_TC_RELOAD 0xFF // 256
#define WDT_PRE_DIV4 2     // SYS1_APBCP1.WDTPRE: 2 = /4  ->  31.25us*4*128*256 ~= 4.1 s

#define RSTST_WDTRSTF (1UL << 1)

// Esc = матрица (0,0): колонка P0.4, строка P1.14
#define ESC_COL_PORT SN_GPIO0
#define ESC_COL_BIT 4
#define ESC_ROW_PORT SN_GPIO1
#define ESC_ROW_BIT 14

// Все колонки матрицы (как в matrix.c): P0.0, P0.4-P0.7 и P2.0-P2.9, P2.14
#define COLS_P0_MASK 0x00F1UL
#define COLS_P2_MASK 0x43FFUL

static inline void wdt_disable(void) {
    SN_WDT->CFG = WDT_KEY; // WDTEN = 0
}

static inline void wdt_feed(void) {
    SN_WDT->FEED = WDT_FEED_VALUE;
}

static void wdt_start(void) {
    SN_SYS1->AHBCLKEN |= (1UL << 24); // WDTCLKEN
    wdt_disable();
    SN_SYS1->APBCP1 = (SN_SYS1->APBCP1 & ~(7UL << 20)) | ((uint32_t)WDT_PRE_DIV4 << 20);
    SN_WDT->TC = WDT_KEY | WDT_TC_RELOAD;
    wdt_feed();
    SN_WDT->CFG = WDT_KEY | 1UL; // WDTEN = 1, WDTIE = 0 -> сброс по тайм-ауту
}

static void busy_wait(uint32_t loops) {
    while (loops--) {
        __asm__ volatile("nop");
    }
}

__attribute__((noreturn)) static void jump_to_rom_bootloader(void) {
    wdt_disable();
    void (*rom)(void) = (void (*)(void))SN32_BOOTLOADER_ADDRESS;
    rom();
    while (1) {
    }
}

// Чтение Esc до любой инициализации. Тактовая частота после сброса — IHRC 12 МГц.
static bool esc_held_raw(void) {
    const uint32_t row_cfg_shift = ESC_ROW_BIT * 2;
    const uint32_t row_cfg_saved = ESC_ROW_PORT->CFG;
    const uint32_t p0_mode_saved = SN_GPIO0->MODE;
    const uint32_t p2_mode_saved = SN_GPIO2->MODE;

    // строка: вход без подтяжки (на плате внешние резисторы к земле)
    ESC_ROW_PORT->CFG = (row_cfg_saved & ~(3UL << row_cfg_shift)) | (2UL << row_cfg_shift);

    // все колонки — выходы LOW, колонка Esc — HIGH
    SN_GPIO0->BCLR = COLS_P0_MASK;
    SN_GPIO2->BCLR = COLS_P2_MASK;
    SN_GPIO0->MODE = p0_mode_saved | COLS_P0_MASK;
    SN_GPIO2->MODE = p2_mode_saved | COLS_P2_MASK;
    ESC_COL_PORT->BSET = 1UL << ESC_COL_BIT;

    bool held = true;
    for (uint8_t i = 0; i < 2 && held; i++) {
        busy_wait(60000); // ~20 мс при 12 МГц (≈4 такта на проход): установка уровня + антидребезг
        held = (ESC_ROW_PORT->DATA >> ESC_ROW_BIT) & 1UL;
    }

    // вернуть всё как было после сброса
    ESC_COL_PORT->BCLR = 1UL << ESC_COL_BIT;
    SN_GPIO0->MODE = p0_mode_saved;
    SN_GPIO2->MODE = p2_mode_saved;
    ESC_ROW_PORT->CFG = row_cfg_saved;

    return held;
}

/* --------------------------------------------------------------------------
 * Хуки раннего старта (tmk_core/protocol/chibios/chibios.c)
 * ------------------------------------------------------------------------ */

// Самое первое, что выполняется после сброса (до __early_init платы).
void early_hardware_init_pre(void) {
    // WDT мог пережить программный сброс (QK_BOOT, сброс из ROM) — выключить сразу
    wdt_disable();

    // Сброс от сторожевого таймера -> прошивка зависла -> в бутлоадер
    if (SN_SYS0->RSTST & RSTST_WDTRSTF) {
        SN_SYS0->RSTST = SN_SYS0->RSTST & ~RSTST_WDTRSTF; // запись 0 сбрасывает флаг
        jump_to_rom_bootloader();
    }

    // Зажат Esc при подключении -> в бутлоадер
    if (esc_held_raw()) {
        jump_to_rom_bootloader();
    }

    // Штатный путь QK_BOOT (магическое слово в конце RAM)
    void enter_bootloader_mode_if_requested(void);
    enter_bootloader_mode_if_requested();
}

// После настройки тактов — запускаем сторожевой таймер
void early_hardware_init_post(void) {
    wdt_start();
}

/* --------------------------------------------------------------------------
 * Кормление WDT и QK_BOOT
 * ------------------------------------------------------------------------ */

void safety_wdt_feed(void) {
    wdt_feed();
}

// Глубокий сон (wireless.c): WDT на ILRC продолжает считать — выключаем на время сна
void safety_wdt_stop(void) {
    wdt_disable();
}

void safety_wdt_restart(void) {
    wdt_start();
}

// housekeeping_task_kb() — в type84_pro.c: кормит WDT через safety_wdt_feed() и обновляет экран

// QK_BOOT: выключить WDT, затем штатный путь SonixQMK (магическое слово + сброс)
void bootloader_jump(void) {
    wdt_disable();
    extern uint32_t __ram0_end__;
    volatile uint32_t *magic = (volatile uint32_t *)((uintptr_t)&__ram0_end__ - 4); // как MAGIC_ADDR в sn32_dfu.c
    *magic = 0xDEADBEEF;
    wait_us(1);
    NVIC_SystemReset();
}
