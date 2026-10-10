// Слой связи Quantum Painter <-> ST7789 на прямых регистрах SPI0 SN32F290.
//
// Почему не spi_master + драйвер SPI ChibiOS: на этой плате с ними qp_init()
// проходит успешно, а панель остаётся чёрной (причина не найдена). Прямая работа
// с регистрами по схеме стока (sub_14538 / sub_145a4 / sub_10420) проверена на
// железе — эксперимент keymaps/lcdraw залил экран красным.
//
// Отличия от spi_master: SPI0 включается один раз и не выключается между
// транзакциями; D/C и CS переключаются только после того, как байт полностью
// ушёл (байт вернулся в RX FIFO и BUSY = 0); прерывания SPI не используются.
//
// Подключается в lcd_init(): после qp_st7789_make_spi_device() подменяем
// comms_vtable устройства на lcd_spi_comms_vtable.

#include "quantum.h"

#ifdef QUANTUM_PAINTER_ENABLE

#    include "qp_internal.h"
#    include "qp_comms_spi.h"
#    include "qp_tft_panel.h"

#    define SPI_STAT_TX_FULL (1UL << 1)
#    define SPI_STAT_RX_EMPTY (1UL << 2)
#    define SPI_STAT_BUSY (1UL << 4)

#    define SPI_CTRL0_SPIEN (1UL << 0)
#    define SPI_CTRL0_FRESET (3UL << 6)
#    define SPI_CTRL0_DL8 (7UL << 8)
#    define SPI_CTRL0_SELDIS (1UL << 18)

#    define SYS1_AHBCLKEN_SPI0 (1UL << 12)

static inline qp_comms_spi_dc_reset_config_t *lcd_cfg(painter_device_t device) {
    return &((tft_panel_dc_reset_painter_device_t *)device)->spi_dc_reset_config;
}

// Дождаться, пока все отправленные байты ушли: вычитать RX и дождаться !BUSY
static inline void spi0_drain(uint32_t *pending) {
    while (*pending) {
        if (!(SN_SPI0->STAT & SPI_STAT_RX_EMPTY)) {
            (void)SN_SPI0->DATA;
            (*pending)--;
        }
    }
    while (SN_SPI0->STAT & SPI_STAT_BUSY) {
    }
}

// Режим «как сток»: CS поднимается после каждого байта (sub_145a4). Переключается
// из тестовой раскладки lcdtest.
bool lcd_spi_bytewise = false;
static pin_t lcd_spi_cs_pin = NO_PIN;

static void spi0_send_bytewise(const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        gpio_write_pin_low(lcd_spi_cs_pin);
        SN_SPI0->DATA = data[i];
        while (SN_SPI0->STAT & SPI_STAT_RX_EMPTY) {
        }
        (void)SN_SPI0->DATA;
        while (SN_SPI0->STAT & SPI_STAT_BUSY) {
        }
        gpio_write_pin_high(lcd_spi_cs_pin);
    }
    gpio_write_pin_low(lcd_spi_cs_pin); // транзакция QP ещё открыта
}

// Отправка буфера через FIFO: пишем, пока TX не полон, параллельно читаем RX
static void spi0_send(const uint8_t *data, uint32_t len) {
    if (lcd_spi_bytewise) {
        spi0_send_bytewise(data, len);
        return;
    }
    uint32_t pending = 0;
    for (uint32_t i = 0; i < len;) {
        if (!(SN_SPI0->STAT & SPI_STAT_RX_EMPTY)) {
            (void)SN_SPI0->DATA;
            pending--;
        }
        if (!(SN_SPI0->STAT & SPI_STAT_TX_FULL) && pending < 4) { // не больше 4 байт «в полёте»: с запасом к глубине FIFO
            SN_SPI0->DATA = data[i++];
            pending++;
        }
    }
    spi0_drain(&pending);
}

static void spi0_init(uint16_t divisor, int8_t mode) {
    SN_SYS1->AHBCLKEN |= SYS1_AHBCLKEN_SPI0;
    SN_SPI0->CTRL0  = SPI_CTRL0_DL8 | SPI_CTRL0_SELDIS; // 8 бит, мастер, CS вручную
    SN_SPI0->CTRL1  = (mode & 2 ? (1UL << 1) : 0) | (mode & 1 ? (1UL << 2) : 0); // MSB first, CPOL, CPHA
    SN_SPI0->CLKDIV = divisor >= 2 ? (divisor / 2 - 1) : 0;                    // SCK = 48 МГц / (2 * (DIV + 1))
    SN_SPI0->DFDLY  = 1;
    SN_SPI0->CTRL0 |= SPI_CTRL0_FRESET;
    SN_SPI0->CTRL0 |= SPI_CTRL0_SPIEN;

    // SCK/MISO/MOSI (P3.0-P3.2) — входы с подтяжкой, как в стоке: к SPI их подключает PFPA
    gpio_set_pin_input_high(SPI_SCK_PIN);
    gpio_set_pin_input_high(SPI_MISO_PIN);
    gpio_set_pin_input_high(SPI_MOSI_PIN);
}

/* --------------------------------------------------------------------------
 * comms vtable
 * ------------------------------------------------------------------------ */

static bool lcd_spi_comms_init(painter_device_t device) {
    qp_comms_spi_dc_reset_config_t *cfg = lcd_cfg(device);

    gpio_set_pin_output(cfg->spi_config.chip_select_pin);
    gpio_write_pin_high(cfg->spi_config.chip_select_pin);
    gpio_set_pin_output(cfg->dc_pin);
    gpio_write_pin_high(cfg->dc_pin);

    lcd_spi_cs_pin = cfg->spi_config.chip_select_pin;
    spi0_init(cfg->spi_config.divisor, cfg->spi_config.mode);

    // Сброс как в стоке: HIGH, LOW 20 мс, HIGH, 150 мс
    if (cfg->reset_pin != NO_PIN) {
        gpio_set_pin_output(cfg->reset_pin);
        gpio_write_pin_high(cfg->reset_pin);
        wait_ms(20);
        gpio_write_pin_low(cfg->reset_pin);
        wait_ms(20);
        gpio_write_pin_high(cfg->reset_pin);
        wait_ms(150);
    }
    return true;
}

static bool lcd_spi_comms_start(painter_device_t device) {
    gpio_write_pin_low(lcd_cfg(device)->spi_config.chip_select_pin);
    return true;
}

static bool lcd_spi_comms_stop(painter_device_t device) {
    while (SN_SPI0->STAT & SPI_STAT_BUSY) {
    }
    gpio_write_pin_high(lcd_cfg(device)->spi_config.chip_select_pin);
    return true;
}

static uint32_t lcd_spi_send_data(painter_device_t device, const void *data, uint32_t byte_count) {
    gpio_write_pin_high(lcd_cfg(device)->dc_pin);
    spi0_send((const uint8_t *)data, byte_count);
    return byte_count;
}

static bool lcd_spi_send_command(painter_device_t device, uint8_t cmd) {
    qp_comms_spi_dc_reset_config_t *cfg = lcd_cfg(device);
    gpio_write_pin_low(cfg->dc_pin);
    spi0_send(&cmd, 1); // возвращается, когда байт полностью ушёл
    return true;
}

// Формат как в QP: команда, задержка (мс), число байт, данные...
static bool lcd_spi_bulk_command_sequence(painter_device_t device, const uint8_t *sequence, size_t sequence_len) {
    qp_comms_spi_dc_reset_config_t *cfg = lcd_cfg(device);
    for (size_t i = 0; i < sequence_len;) {
        uint8_t command   = sequence[i];
        uint8_t delay     = sequence[i + 1];
        uint8_t num_bytes = sequence[i + 2];
        lcd_spi_send_command(device, command);
        if (num_bytes > 0) {
            if (cfg->command_params_uses_command_pin) {
                for (uint8_t j = 0; j < num_bytes; j++) {
                    lcd_spi_send_command(device, sequence[i + 3 + j]);
                }
            } else {
                lcd_spi_send_data(device, &sequence[i + 3], num_bytes);
            }
        }
        if (delay > 0) {
            wait_ms(delay);
        }
        i += (3 + num_bytes);
    }
    return true;
}

const painter_comms_with_command_vtable_t lcd_spi_comms_vtable = {
    .base =
        {
            .comms_init  = lcd_spi_comms_init,
            .comms_start = lcd_spi_comms_start,
            .comms_send  = lcd_spi_send_data,
            .comms_stop  = lcd_spi_comms_stop,
        },
    .send_command          = lcd_spi_send_command,
    .bulk_command_sequence = lcd_spi_bulk_command_sequence,
};

#endif // QUANTUM_PAINTER_ENABLE
