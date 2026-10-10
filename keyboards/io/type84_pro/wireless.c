// Беспроводные режимы: радиомодуль (BT + 2.4G) на UART2.
//
// Протокол восстановлен из стока v1.32 — docs/uart-protocol.md:
//   кадр [CMD][DATA...][SUM], SUM = (CMD + ΣDATA) & 0xFF, 115200 8N1;
//   на каждый принятый кадр получатель отвечает "a\r\n" (61 0D 0A).
//
// Режим выбирает переключатель на корпусе (P1.12 / P1.13, sub_0236c в стоке):
//   USB  — модулю A6 11, отчёты идут по USB;
//   BT   — модулю A6 31..35 (слот), отчёты кадрами A1/A3;
//   2.4G — модулю A6 30.
// Сопряжение (сток sub_03010 + sub_007fe): выбрать слот/2.4G, затем A6 51.
//
// Отчёты в QMK уходят через встроенный механизм «свой Bluetooth-драйвер»
// (BLUETOOTH_DRIVER = custom): в BT и 2.4G активный хост — CONNECTION_HOST_BLUETOOTH.

#include "quantum.h"
#include "bluetooth.h"
#include "connection.h"
#include "usb_main.h"
#include "usb_util.h"
#include "suspend.h"
#include "wireless.h"
#include "type84_pro.h"
#include "battery.h"
#include "ledscan.h"

#ifndef USB_GETSTATUS_REMOTE_WAKEUP_ENABLED
#    define USB_GETSTATUS_REMOTE_WAKEUP_ENABLED (2U) // как в tmk_core/protocol/chibios/chibios.c
#endif

/* --------------------------------------------------------------------------
 * Настройки
 * ------------------------------------------------------------------------ */

#ifndef WL_ACK_TIMEOUT_MS
#    define WL_ACK_TIMEOUT_MS 15 // ждём "a\r\n" (в стоке 5 тиков)
#endif
#ifndef WL_RETRIES
#    define WL_RETRIES 3 // повторов кадра, как в стоке
#endif
#ifndef WL_FRAME_GAP_MS
#    define WL_FRAME_GAP_MS 2 // пауза между кадрами (в стоке 2 тика после конца передачи)
#endif
#ifndef WL_PAIR_HOLD_MS
#    define WL_PAIR_HOLD_MS 3000 // удержание клавиши слота для сопряжения
#endif
#ifndef WL_SWITCH_DEBOUNCE_MS
#    define WL_SWITCH_DEBOUNCE_MS 50
#endif
// WIRELESS_KEEP_USB: не отключать USB в режимах BT/2.4G (для отладки — работает консоль).
// Сток в беспроводных режимах USB отключает (sub_00fd2).

#define MODE_PIN_BT B12  // P1.12 LOW -> BT
#define MODE_PIN_24G B13 // P1.13 LOW -> 2.4G

// Коды режима для A6
#define A6_USB 0x11
#define A6_24G 0x30
#define A6_BT1 0x31 // 0x31..0x35 — слоты 1..5
#define A6_PAIR 0x51
#define A6_WAKE 0x00

/* --------------------------------------------------------------------------
 * UART2 на регистрах (TX P1.7, RX P1.6 через PFPA UART = 0x0F00)
 * ------------------------------------------------------------------------ */

#define UART_LS_RDR (1UL << 0)
#define UART_LS_THRE (1UL << 5)
#define UART_LS_TEMT (1UL << 6)
#define UART_IE_RDAIE (1UL << 0)
#define UART_IE_RLSIE (1UL << 2)
#define SYS1_AHBCLKEN_UART2 (1UL << 18)

#define RX_BUF_SIZE 128 // степень двойки
static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head; // пишет прерывание
static uint8_t          rx_tail; // читает wireless_task

OSAL_IRQ_HANDLER(SN32_UART2_HANDLER) {
    OSAL_IRQ_PROLOGUE();
    while (SN_UART2->LS & UART_LS_RDR) {
        uint8_t b                         = SN_UART2->RB;
        rx_buf[rx_head & (RX_BUF_SIZE - 1)] = b;
        rx_head++;
    }
    (void)SN_UART2->II;
    OSAL_IRQ_EPILOGUE();
}

static void uart2_init(void) {
    // как в стоке (sub_1481a): 8N1, делитель 26 -> 48 МГц / (16 * 26) = 115385 бод
    SN_SYS1->AHBCLKEN |= SYS1_AHBCLKEN_UART2;
    SN_UART2->LC       = 0x83; // 8 бит, DLAB = 1
    SN_UART2->FD       = 0;
    SN_UART2->DLM      = 0;
    SN_UART2->DLL      = 26;
    SN_UART2->LC       = 0x03; // DLAB = 0
    SN_UART2->FIFOCTRL = 0x07; // FIFO вкл, сброс RX/TX
    SN_UART2->IE       = UART_IE_RDAIE | UART_IE_RLSIE;
    SN_UART2->CTRL     = 0xC1; // UARTEN, RXEN, TXEN
    nvicEnableVector(UART2_IRQn, 3);
}

static void uart2_write(const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        while (!(SN_UART2->LS & UART_LS_THRE)) {
        }
        SN_UART2->TH = data[i];
    }
}

static bool uart2_read(uint8_t *b) {
    if (rx_tail == rx_head) {
        return false;
    }
    *b = rx_buf[rx_tail & (RX_BUF_SIZE - 1)];
    rx_tail++;
    return true;
}

/* --------------------------------------------------------------------------
 * Очередь кадров MCU -> модуль
 * ------------------------------------------------------------------------ */

#define FRAME_MAX 34
#define QUEUE_LEN 16

typedef struct {
    uint8_t len;
    uint8_t data[FRAME_MAX];
} frame_t;

static frame_t  queue[QUEUE_LEN];
static uint8_t  q_head, q_tail; // q_head — следующий на отправку
static bool     waiting_ack;
static uint8_t  retries;
static uint16_t sent_at;
static uint16_t last_tx_done;
static bool     module_asleep; // кадр так и не подтвердили — перед следующим будим (A6 00)
static bool     need_ack;      // модуль прислал кадр — ответить "a\r\n"

static uint8_t q_count(void) {
    return (uint8_t)(q_tail - q_head);
}

static void wl_debug_frame(const char *dir, const uint8_t *d, uint8_t len) {
#ifdef CONSOLE_ENABLE
    if (!debug_enable) return;
    dprintf("WL %s", dir);
    for (uint8_t i = 0; i < len; i++) dprintf(" %02X", d[i]);
    dprintf("\n");
#endif
}

static void wl_service_tx(void);
static void wl_service_rx(void);

// Поставить кадр: CMD + данные, сумма считается здесь
static void wl_queue(uint8_t cmd, const uint8_t *data, uint8_t n) {
    if (n + 2 > FRAME_MAX) return;
    // очередь полна — дать ей разойтись (не теряем нажатия), но не дольше 200 мс
    uint16_t t0 = timer_read();
    while (q_count() >= QUEUE_LEN && timer_elapsed(t0) < 200) {
        wl_service_rx(); // ACK приходят сюда
        wl_service_tx();
    }
    if (q_count() >= QUEUE_LEN) {
        q_head++; // модуль молчит — выбросить самый старый
    }
    frame_t *f = &queue[q_tail % QUEUE_LEN];
    uint8_t  s = cmd;
    f->data[0] = cmd;
    for (uint8_t i = 0; i < n; i++) {
        f->data[1 + i] = data[i];
        s += data[i];
    }
    f->data[1 + n] = s;
    f->len         = n + 2;
    q_tail++;
}

static void wl_queue_a6(uint8_t code) {
    wl_queue(0xA6, &code, 1);
}

static void wl_send_head(void) {
    frame_t *f = &queue[q_head % QUEUE_LEN];
    uart2_write(f->data, f->len);
    wl_debug_frame(">", f->data, f->len);
    waiting_ack = true;
    sent_at     = timer_read();
}

static void wl_service_tx(void) {
    if (need_ack) {
        static const uint8_t ack[] = {0x61, 0x0D, 0x0A};
        need_ack                   = false;
        uart2_write(ack, sizeof(ack));
    }

    if (waiting_ack) {
        if (timer_elapsed(sent_at) < WL_ACK_TIMEOUT_MS) return;
        if (++retries <= WL_RETRIES) {
            wl_send_head();
            return;
        }
        // не подтвердили — выбрасываем, модуль, видимо, спит
        dprintf("WL no ACK, drop\n");
        waiting_ack   = false;
        retries       = 0;
        module_asleep = true;
        q_head++;
        last_tx_done = timer_read();
        return;
    }

    if (q_count() == 0 || timer_elapsed(last_tx_done) < WL_FRAME_GAP_MS) return;

    if (module_asleep) {
        // как в стоке (sub_05954): A6 00 + сумма, без ожидания ACK, потом 3 мс
        static const uint8_t wake[] = {0xA6, A6_WAKE, 0xA6};
        uart2_write(wake, sizeof(wake));
        wl_debug_frame("> wake", wake, sizeof(wake));
        module_asleep = false;
        wait_ms(3);
    }
    retries = 0;
    wl_send_head();
}

/* --------------------------------------------------------------------------
 * Разбор кадров модуль -> MCU
 * ------------------------------------------------------------------------ */

static wl_link_t link_state = WL_LINK_UNKNOWN;
static uint8_t   host_leds;

static void wl_on_ack(void) {
    if (waiting_ack) {
        waiting_ack  = false;
        retries      = 0;
        q_head++;
        last_tx_done = timer_read();
    }
}

static void wl_on_frame3(const uint8_t *f) {
    switch (f[0]) {
        case 0x5A: // подключено + LED хоста (Num/Caps/Scroll)
            if (f[1] < 8) host_leds = f[1];
            link_state = WL_LINK_CONNECTED;
            break;
        case 0x5B:
            switch (f[1]) {
                case 0x31:
                    link_state = WL_LINK_DISCONNECTED;
                    host_leds  = 0;
                    break;
                case 0x32:
                    link_state = WL_LINK_CONNECTED;
                    break;
                case 0x33:
                    link_state = WL_LINK_PAIRING;
                    host_leds  = 0;
                    break;
                default: // 21/22/23 — игнор в стоке; 42 — пинг (ответ ACK ниже)
                    break;
            }
            break;
        default: // 5C xx — сток принимает без действия
            break;
    }
}

static void wl_service_rx(void) {
    static uint8_t  frame[FRAME_MAX];
    static uint8_t  len;
    static uint16_t last_byte;
    uint8_t         b;

    if (len && timer_elapsed(last_byte) > 20) {
        wl_debug_frame("< junk", frame, len);
        len = 0; // обрывок кадра
    }

    while (uart2_read(&b)) {
        last_byte = timer_read();
        if (len == 0 && b != 0x61 && b != 0x5A && b != 0x5B && b != 0x5C && b != 0x81) {
            wl_debug_frame("< skip", &b, 1);
            continue;
        }
        frame[len++] = b;

        uint8_t need = frame[0] == 0x81 ? 34 : 3;
        if (len < need) continue;

        wl_debug_frame("<", frame, len);
        if (frame[0] == 0x61) {
            if (frame[1] == 0x0D && frame[2] == 0x0A) wl_on_ack();
        } else {
            uint8_t s = 0;
            for (uint8_t i = 0; i < len - 1; i++) s += frame[i];
            if (s == frame[len - 1]) {
                need_ack = true;
                if (len == 3) wl_on_frame3(frame);
                // 81 — туннель конфигуратора через донгл: не поддерживаем
            } else {
                dprintf("WL bad sum\n");
            }
        }
        len = 0;
    }
}

/* --------------------------------------------------------------------------
 * Режим, слот, USB
 * ------------------------------------------------------------------------ */

static wl_mode_t mode = WL_MODE_USB;
static uint8_t   bt_slot = 1;
static bool      usb_stopped;

static wl_mode_t read_switch(void) {
    if (!gpio_read_pin(MODE_PIN_BT)) return WL_MODE_BT;
    if (!gpio_read_pin(MODE_PIN_24G)) return WL_MODE_24G;
    return WL_MODE_USB;
}

static uint8_t mode_code(void) {
    switch (mode) {
        case WL_MODE_BT:
            return A6_BT1 + bt_slot - 1;
        case WL_MODE_24G:
            return A6_24G;
        default:
            return A6_USB;
    }
}

static uint32_t idle_ref;
static uint8_t  battery_sent = 0xFF; // последний отправленный модулю процент

// A7 <0..100>: процент батареи модулю — он передаёт его хосту (BT Battery Service / донгл)
static void send_battery(uint8_t level) {
    if (mode == WL_MODE_USB || level == battery_sent) return;
    battery_sent = level;
    wl_queue(0xA7, &level, 1);
}

void battery_percent_changed_kb(uint8_t level) {
    send_battery(level);
    battery_percent_changed_user(level);
}

static void reset_report_cache(void);

static void apply_mode(wl_mode_t m) {
    reset_report_cache();
    idle_ref   = timer_read32();
    mode       = m;
    link_state = WL_LINK_UNKNOWN;
    host_leds  = 0;
    dprintf("WL mode %u slot %u\n", m, bt_slot);
    wl_queue_a6(mode_code());
    battery_sent = 0xFF;
    send_battery(battery_get_percent());

    if (m == WL_MODE_USB) {
        connection_set_host_noeeprom(CONNECTION_HOST_USB);
        if (usb_stopped) {
            usb_stopped = false;
            restart_usb_driver(&USB_DRIVER);
        }
    } else {
        connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
#ifndef WIRELESS_KEEP_USB
        if (!usb_stopped) {
            usb_stopped = true;
            usb_disconnect();
        }
#endif
    }
}

// EEPROM (eeconfig_kb): биты 0-2 — слот BT, биты 3-5 — индекс таймера сна
static void save_slot(void) {
    uint32_t kb = eeconfig_read_kb();
    eeconfig_update_kb((kb & ~7UL) | bt_slot);
}

void eeconfig_init_kb(void) {
    eeconfig_update_kb(1 | (1UL << 12)); // слот 1, сон — индекс 0 (5 мин); миграции не нужны
    eeconfig_init_user();
}

/* --------------------------------------------------------------------------
 * Автосон в BT/2.4G
 * ------------------------------------------------------------------------ */

#ifndef WL_SLEEP_DEFAULT_MIN
#    define WL_SLEEP_DEFAULT_MIN 5
#endif
// Индекс 0 — значение по умолчанию (так старый EEPROM с нулями даёт 5 мин)
static const uint8_t sleep_options[] = {WL_SLEEP_DEFAULT_MIN, 10, 30, 0, 1};
static uint8_t       sleep_idx;

void wireless_sleep_set_index(uint8_t idx) {
    if (idx >= ARRAY_SIZE(sleep_options)) idx = 0;
    sleep_idx   = idx;
    uint32_t kb = eeconfig_read_kb();
    eeconfig_update_kb((kb & ~(7UL << 3)) | ((uint32_t)sleep_idx << 3));
}

uint8_t wireless_sleep_index(void) {
    return sleep_idx;
}

uint8_t wireless_sleep_option_count(void) {
    return ARRAY_SIZE(sleep_options);
}

uint8_t wireless_sleep_option_minutes(uint8_t idx) {
    return idx < ARRAY_SIZE(sleep_options) ? sleep_options[idx] : 0;
}

static void sleep_cycle(void) {
    wireless_sleep_set_index((sleep_idx + 1) % ARRAY_SIZE(sleep_options));
    char msg[12];
    if (sleep_options[sleep_idx]) {
        snprintf(msg, sizeof(msg), "SLEEP %um", sleep_options[sleep_idx]);
    } else {
        strcpy(msg, "SLEEP OFF");
    }
    type84_lcd_toast(msg);
}

uint8_t wireless_sleep_minutes(void) {
    return sleep_options[sleep_idx];
}

/* --------------------------------------------------------------------------
 * Клавиши слотов и сопряжения
 * ------------------------------------------------------------------------ */

static uint16_t hold_key;   // какая клавиша удерживается (0 — никакая)
static uint16_t hold_since;
static bool     hold_fired;

void wireless_select_slot(uint8_t slot);

static void start_pairing(void) {
    dprintf("WL pairing (mode %u slot %u)\n", mode, bt_slot);
    wl_queue_a6(mode_code()); // как в стоке: сначала выбор слота/2.4G, затем A6 51
    wl_queue_a6(A6_PAIR);
    link_state = WL_LINK_PAIRING;
}

bool process_record_wireless(uint16_t keycode, bool pressed) {
    if (keycode < BT_1 || keycode > SLEEP_T) return true;

    if (keycode == SLEEP_T) {
        if (pressed) sleep_cycle();
        return false;
    }

    if (!pressed) {
        if (keycode == hold_key) hold_key = 0;
        return false;
    }

    hold_key   = keycode;
    hold_since = timer_read();
    hold_fired = false;

    if (keycode <= BT_5 && mode == WL_MODE_BT) {
        wireless_select_slot(keycode - BT_1 + 1);
    }
    return false;
}

// Выбор слота BT 1..5: сохраняется всегда, модулю — только в режиме BT
void wireless_select_slot(uint8_t slot) {
    if (slot < 1 || slot > 5 || slot == bt_slot) return;
    bt_slot = slot;
    save_slot();
    if (mode != WL_MODE_BT) return;
    link_state = WL_LINK_UNKNOWN;
    host_leds  = 0;
    clear_keyboard();
    wl_queue_a6(mode_code());
}

// Сопряжение на текущем слоте / 2.4G; false — в режиме USB
bool wireless_pair(void) {
    if (mode == WL_MODE_USB) return false;
    start_pairing();
    return true;
}

static void hold_task(void) {
    if (!hold_key || hold_fired || timer_elapsed(hold_since) < WL_PAIR_HOLD_MS) return;
    hold_fired = true;
    if ((hold_key <= BT_5 && mode == WL_MODE_BT) || (hold_key == RF_PAIR && mode == WL_MODE_24G)) {
        start_pairing();
    }
}

/* --------------------------------------------------------------------------
 * USB-сон в режиме USB.
 * С BLUETOOTH_ENABLE ядро собирается с NO_USB_STARTUP_CHECK и само не входит
 * в цикл сна USB — повторяем его (tmk_core/protocol/chibios/chibios.c).
 * ------------------------------------------------------------------------ */

static void usb_suspend_task(void) {
    if (mode != WL_MODE_USB || USB_DRIVER.state != USB_SUSPENDED) return;
    while (USB_DRIVER.state == USB_SUSPENDED) {
        suspend_power_down();
        if (suspend_wakeup_condition() && (USB_DRIVER.status & USB_GETSTATUS_REMOTE_WAKEUP_ENABLED)) {
            usbWakeupHost(&USB_DRIVER);
        }
        // переключатель могли перевести в BT/2.4G прямо во сне
        if (read_switch() != WL_MODE_USB) break;
    }
}

/* --------------------------------------------------------------------------
 * Глубокий сон MCU (как в стоке, sub_001cc):
 *   экран SLPIN + подсветка off, WDT off, SysTick off;
 *   все колонки HIGH -> нажатие любой клавиши поднимает строку;
 *   пробуждение по обоим фронтам: строки, крутилки, переключатель режима,
 *   SnapTap, VBUS, P0.10/P1.2;
 *   такт на ILRC 32 кГц, PLL и IHRC выключены, PMU deep sleep, WFI.
 * Прерывания на время сна запрещены (PRIMASK): WFI просыпается от
 * отложенного прерывания GPIO, обработчик не вызывается.
 * ------------------------------------------------------------------------ */

void safety_wdt_stop(void);
void safety_wdt_restart(void);
void safety_wdt_feed(void);

// Источники пробуждения (сток sub_01088)
#define WAKE_P0 0x000C0406UL // P0.1/P0.2 крутилка Esc, P0.10, P0.18/P0.19 строки
#define WAKE_P1 0x000AF004UL // P1.2, P1.12/P1.13 режим, P1.14/P1.15/P1.19 строки, P1.17 VBUS
#define WAKE_P3 0x00088060UL // P3.5/P3.6 крутилка PrtSc, P3.15 SnapTap, P3.19 строка
// Колонки матрицы (как в matrix.c / safety.c)
#define COLS_P0 0x00F1UL
#define COLS_P2 0x43FFUL

#define PMU_CTRL_DEEP_SLEEP 2
#define SYS0_CLK_IHRC 0
#define SYS0_CLK_ILRC 1

static void wake_irq_setup(SN_GPIO1_Type *port, uint32_t mask) {
    port->IS &= ~mask; // по фронту
    port->IBS |= mask; // оба фронта
    port->IC = mask;
    port->IE |= mask;
}

static void wake_irq_clear(SN_GPIO1_Type *port, uint32_t mask) {
    port->IE &= ~mask;
    port->IC = mask;
}

static void flush_uart_tx(uint16_t timeout_ms) {
    uint16_t t0 = timer_read();
    while ((q_count() || waiting_ack) && timer_elapsed(t0) < timeout_ms) {
        wl_service_rx();
        wl_service_tx();
    }
    while (!(SN_UART2->LS & UART_LS_TEMT)) {
    }
}

static void deep_sleep(void) {
    dprintf("WL sleep\n");

    // Модулю: в BT сток шлёт A6 55 перед сном
    if (mode == WL_MODE_BT) wl_queue_a6(0x55);
    flush_uart_tx(100);

    type84_lcd_sleep(true);
    ledscan_stop(); // подсветка off, колонки — GPIO (иначе их держит ШИМ)

    bool usb_was_on = !usb_stopped;
    if (usb_was_on) usb_disconnect(); // отладочная сборка с WIRELESS_KEEP_USB

    __disable_irq();
    safety_wdt_stop();

    uint32_t systick = SysTick->CTRL;
    SysTick->CTRL    = 0;

    // все прерывания NVIC выключить, оставить только GPIO-пробуждение
    uint32_t iser    = NVIC->ISER[0];
    NVIC->ICER[0]    = 0xFFFFFFFFUL;
    NVIC->ICPR[0]    = 0xFFFFFFFFUL;

    // колонки HIGH
    uint32_t p0_data = SN_GPIO0->DATA, p2_data = SN_GPIO2->DATA;
    SN_GPIO0->BSET   = COLS_P0;
    SN_GPIO2->BSET   = COLS_P2;

    wake_irq_setup((SN_GPIO1_Type *)SN_GPIO0, WAKE_P0);
    wake_irq_setup((SN_GPIO1_Type *)SN_GPIO1, WAKE_P1);
    wake_irq_setup((SN_GPIO1_Type *)SN_GPIO3, WAKE_P3);
    NVIC->ISER[0] = (1UL << P0_IRQn) | (1UL << P1_IRQn) | (1UL << P3_IRQn);

    // такт: ILRC, PLL и IHRC выключить (сток sub_01360)
    SN_SYS0->CLKCFG = SYS0_CLK_ILRC;
    while (((SN_SYS0->CLKCFG >> 4) & 7) != SYS0_CLK_ILRC) {
    }
    SN_SYS0->PLLCTRL &= ~(1UL << 15);
    SN_SYS0->ANBCTRL &= ~1UL; // IHRCEN

    SN_PMU->CTRL = PMU_CTRL_DEEP_SLEEP;
    __WFI();
    SN_PMU->CTRL = 0;
    safety_wdt_restart(); // сразу: если восстановление тактов зависнет — WDT -> бутлоадер

    // такт обратно: IHRC, затем PLL 48 МГц как при старте (сток sub_01190)
    SN_SYS0->ANBCTRL |= 1UL;
    SN_SYS0->CLKCFG = SYS0_CLK_IHRC;
    while (((SN_SYS0->CLKCFG >> 4) & 7) != SYS0_CLK_IHRC) {
    }
    SystemInit();
    sn32_clock_init();

    wake_irq_clear((SN_GPIO1_Type *)SN_GPIO0, WAKE_P0);
    wake_irq_clear((SN_GPIO1_Type *)SN_GPIO1, WAKE_P1);
    wake_irq_clear((SN_GPIO1_Type *)SN_GPIO3, WAKE_P3);

    SN_GPIO0->DATA = p0_data;
    SN_GPIO2->DATA = p2_data;

    NVIC->ICER[0] = 0xFFFFFFFFUL;
    NVIC->ICPR[0] = 0xFFFFFFFFUL;
    NVIC->ISER[0] = iser;
    SysTick->CTRL = systick;
    __enable_irq();

    ledscan_start();
    if (usb_was_on) restart_usb_driver(&USB_DRIVER);

    // Модуль будим перед первым кадром (A6 00); в 2.4G сток после сна шлёт AB A6 FF 01
    module_asleep = true;
    if (mode == WL_MODE_24G) {
        static const uint8_t ab[] = {0xA6, 0xFF, 0x01};
        wl_queue(0xAB, ab, sizeof(ab));
    }

    battery_sent = 0xFF;
    send_battery(battery_get_percent());

    type84_lcd_sleep(false);
    dprintf("WL wake\n");
}

// Отсчёт простоя (idle_ref) — от последнего нажатия/поворота крутилки, смены режима или пробуждения

static void sleep_task(void) {
    uint8_t minutes = sleep_options[sleep_idx];
    if (mode == WL_MODE_USB || minutes == 0) return;
    uint32_t last = last_input_activity_time();
    if ((int32_t)(last - idle_ref) > 0) idle_ref = last;
    if (timer_elapsed32(idle_ref) < (uint32_t)minutes * 60000UL) return;
    deep_sleep();
    idle_ref = timer_read32();
}

/* --------------------------------------------------------------------------
 * Bluetooth-драйвер QMK (drivers/bluetooth/bluetooth.h)
 * ------------------------------------------------------------------------ */

void bluetooth_init(void) {
    gpio_set_pin_input_high(MODE_PIN_BT);
    gpio_set_pin_input_high(MODE_PIN_24G);
    uint32_t kb   = eeconfig_read_kb();
    uint8_t  slot = kb & 7;
    bt_slot       = (slot >= 1 && slot <= 5) ? slot : 1;
    sleep_idx     = (kb >> 3) & 7;
    if (sleep_idx >= ARRAY_SIZE(sleep_options)) sleep_idx = 0;
    uart2_init();
    wait_ms(2);
    apply_mode(read_switch());
}

void bluetooth_task(void) {
    static wl_mode_t pending = WL_MODE_USB;
    static uint16_t  pending_since;

    wl_mode_t sw = read_switch();
    if (sw != pending) {
        pending       = sw;
        pending_since = timer_read();
    } else if (sw != mode && timer_elapsed(pending_since) > WL_SWITCH_DEBOUNCE_MS) {
        clear_keyboard();
        apply_mode(sw);
    }

    // при каждом подключении повторить процент: отправленный до связи модуль мог отбросить
    static wl_link_t last_link;
    if (link_state == WL_LINK_CONNECTED && last_link != WL_LINK_CONNECTED) {
        battery_sent = 0xFF;
        send_battery(battery_get_percent());
    }
    last_link = link_state;

    wl_service_rx();
    wl_service_tx();
    hold_task();
    usb_suspend_task();
    sleep_task();
}

bool bluetooth_is_connected(void) {
    return link_state == WL_LINK_CONNECTED;
}

uint8_t bluetooth_keyboard_leds(void) {
    return host_leds;
}

// Последние отправленные кадры: A1 (mods + 6 клавиш) и A2 (битовая карта «лишних» клавиш)
static uint8_t a1_last[8];
static uint8_t a2_last[14];

static void reset_report_cache(void) {
    memset(a1_last, 0xFF, sizeof(a1_last)); // форсировать следующую отправку
    memset(a2_last, 0, sizeof(a2_last));
}

static void send_a1(const uint8_t *d) {
    if (memcmp(d, a1_last, sizeof(a1_last)) == 0) return;
    memcpy(a1_last, d, sizeof(a1_last));
    wl_queue(0xA1, d, sizeof(a1_last));
}

static void send_a2(const uint8_t *d) {
    if (memcmp(d, a2_last, sizeof(a2_last)) == 0) return;
    memcpy(a2_last, d, sizeof(a2_last));
    wl_queue(0xA2, d, sizeof(a2_last));
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    uint8_t d[8];
    d[0] = report->mods;
    d[1] = 0;
    for (uint8_t i = 0; i < 6; i++) d[2 + i] = report->keys[i];
    send_a1(d);
    static const uint8_t zero[14] = {0};
    send_a2(zero); // если до этого был NKRO
}

// NKRO по воздуху — как в стоке (sub_02568): первые 6 клавиш (по возрастанию кода)
// идут в обычный кадр A1, остальные — битами в A2 (14 байт: коды 0x00-0x6F,
// бит (код & 7) в байте (код >> 3)). Модуль сам собирает их в один отчёт хоста.
bool bluetooth_can_send_nkro(void) {
    return keymap_config.nkro;
}

void bluetooth_send_nkro(report_nkro_t *report) {
    uint8_t a1[8]  = {report->mods, 0};
    uint8_t a2[14] = {0};
    uint8_t n      = 0;
    for (uint16_t kc = 0; kc < NKRO_REPORT_BITS * 8; kc++) {
        if (!(report->bits[kc >> 3] & (1 << (kc & 7)))) continue;
        if (n < 6) {
            a1[2 + n++] = kc;
        } else if (kc < sizeof(a2) * 8) {
            a2[kc >> 3] |= 1 << (kc & 7);
        }
    }
    // Клавиша могла переехать между кадрами. Если в A2 что-то добавилось
    // (нажата клавиша с меньшим кодом и вытеснила другую из A1) — сначала A2,
    // иначе хост на мгновение увидит вытесненную клавишу отпущенной.
    bool a2_adds = false;
    for (uint8_t i = 0; i < sizeof(a2); i++) a2_adds |= (a2[i] & ~a2_last[i]) != 0;
    if (a2_adds) {
        send_a2(a2);
        send_a1(a1);
    } else {
        send_a1(a1);
        send_a2(a2);
    }
}

void bluetooth_send_consumer(uint16_t usage) {
    uint8_t d[2] = {usage & 0xFF, usage >> 8};
    wl_queue(0xA3, d, sizeof(d));
}

// A4 (system), A8 (мышь) — формат не проверен, пока не шлём.

/* --------------------------------------------------------------------------
 * Состояние для экрана
 * ------------------------------------------------------------------------ */

wl_mode_t wireless_mode(void) {
    return mode;
}

wl_link_t wireless_link(void) {
    return link_state;
}

uint8_t wireless_bt_slot(void) {
    return bt_slot;
}
