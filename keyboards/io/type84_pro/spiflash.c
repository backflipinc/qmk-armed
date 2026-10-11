// SPI NOR flash on SPI1, polled like the display's SPI0 (lcd_spi.c) and the stock firmware
// (sub_14690 init, sub_06c54 byte transfer, sub_06c64 / 06cb4 / 06d32 / 06e0e commands).
//
// SPI1: SCK P0.12, MOSI P1.11, MISO P1.10 (routed by PFPA in pfpa_init()), mode 0, 24 MHz.
// Chip selects are GPIO: P0.13 (factory images) and P1.9 (user animation).

#include "quantum.h"
#include "spiflash.h"

void safety_wdt_feed(void); // safety.c

// screen.c sends animation frames from this flash to the display by DMA in the background;
// every command here waits for that first. Builds without the display have no DMA.
__attribute__((weak)) void screen_dma_wait(void) {}

#define SPI_STAT_TX_FULL (1UL << 1)
#define SPI_STAT_RX_EMPTY (1UL << 2)
#define SPI_STAT_BUSY (1UL << 4)

#define SPI_CTRL0_SPIEN (1UL << 0)
#define SPI_CTRL0_FRESET (3UL << 6)
#define SPI_CTRL0_DL8 (7UL << 8)
#define SPI_CTRL0_SELDIS (1UL << 18)

#define SYS1_AHBCLKEN_SPI1 (1UL << 13)

#define CMD_WRITE_ENABLE 0x06
#define CMD_READ_STATUS 0x05
#define CMD_READ 0x03
#define CMD_PAGE_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_BLOCK_ERASE 0xD8
#define CMD_JEDEC_ID 0x9F
#define CMD_POWER_DOWN 0xB9
#define CMD_RELEASE_POWER_DOWN 0xAB

#define STATUS_WIP 0x01

static const pin_t cs_pins[SPIFLASH_CHIPS] = {A13, B9};

static uint8_t xfer(uint8_t b) {
    SN_SPI1->DATA = b;
    while (SN_SPI1->STAT & SPI_STAT_RX_EMPTY) {
    }
    return SN_SPI1->DATA;
}

static void select(uint8_t chip) {
    screen_dma_wait();
    gpio_write_pin_low(cs_pins[chip]);
}

static void deselect(void) {
    while (SN_SPI1->STAT & SPI_STAT_BUSY) {
    }
    gpio_write_pin_high(cs_pins[SPIFLASH_FACTORY]);
    gpio_write_pin_high(cs_pins[SPIFLASH_USER]);
}

static void command(uint8_t chip, uint8_t cmd) {
    select(chip);
    xfer(cmd);
    deselect();
}

static void command_addr(uint8_t cmd, uint32_t addr) {
    xfer(cmd);
    xfer(addr >> 16);
    xfer(addr >> 8);
    xfer(addr);
}

// Erases take 45 ms (sector) to 2 s (block): the watchdog keeps being fed meanwhile
static bool wait_ready(uint8_t chip, uint32_t timeout_ms) {
    uint32_t start = timer_read32();
    select(chip);
    xfer(CMD_READ_STATUS);
    bool ok = true;
    while (xfer(0) & STATUS_WIP) {
        safety_wdt_feed();
        if (timer_elapsed32(start) > timeout_ms) {
            ok = false;
            break;
        }
    }
    deselect();
    return ok;
}

void spiflash_init(void) {
    for (uint8_t i = 0; i < SPIFLASH_CHIPS; i++) {
        gpio_write_pin_high(cs_pins[i]);
        gpio_set_pin_output(cs_pins[i]);
    }
    SN_SYS1->AHBCLKEN |= SYS1_AHBCLKEN_SPI1;
    SN_SPI1->CTRL0  = SPI_CTRL0_DL8 | SPI_CTRL0_SELDIS; // 8 bit, master, CS by GPIO
    SN_SPI1->CTRL1  = 0;                                // MSB first, mode 0
    SN_SPI1->CLKDIV = 0;                                // 48 MHz / 2 = 24 MHz, as stock
    SN_SPI1->DFDLY  = 1;
    SN_SPI1->CTRL0 |= SPI_CTRL0_FRESET;
    SN_SPI1->CTRL0 |= SPI_CTRL0_SPIEN;
    // SCK / MISO / MOSI stay pulled-up inputs (type84_pro.c): PFPA hands them to SPI1
    spiflash_power(true);
}

void spiflash_power(bool on) {
    for (uint8_t i = 0; i < SPIFLASH_CHIPS; i++) command(i, on ? CMD_RELEASE_POWER_DOWN : CMD_POWER_DOWN);
    if (on) {
        for (volatile uint32_t i = 0; i < 2000; i++) { // tRES1 is 3 us; wait_us() is too coarse here
        }
    }
}

uint32_t spiflash_jedec_id(uint8_t chip) {
    select(chip);
    xfer(CMD_JEDEC_ID);
    uint32_t id = (uint32_t)xfer(0) << 16;
    id |= (uint32_t)xfer(0) << 8;
    id |= xfer(0);
    deselect();
    return id;
}

static uint8_t reading_chip;

void spiflash_read_begin(uint8_t chip, uint32_t addr) {
    reading_chip = chip;
    select(chip);
    command_addr(CMD_READ, addr);
}

// Keeps up to 4 bytes in flight through the FIFO, like spi0_send() in lcd_spi.c
void spiflash_read_next(uint8_t *buf, uint32_t len) {
    uint32_t sent = 0, got = 0;
    while (got < len) {
        if (!(SN_SPI1->STAT & SPI_STAT_RX_EMPTY)) {
            buf[got++] = SN_SPI1->DATA;
        }
        if (sent < len && sent - got < 4 && !(SN_SPI1->STAT & SPI_STAT_TX_FULL)) {
            SN_SPI1->DATA = 0;
            sent++;
        }
    }
}

void spiflash_read_end(void) {
    deselect();
}

void spiflash_read(uint8_t chip, uint32_t addr, uint8_t *buf, uint32_t len) {
    spiflash_read_begin(chip, addr);
    spiflash_read_next(buf, len);
    spiflash_read_end();
}

bool spiflash_erase(uint8_t chip, uint32_t addr, uint32_t size) {
    if (chip != SPIFLASH_USER) return false; // the factory chip is never written
    command(chip, CMD_WRITE_ENABLE);
    select(chip);
    command_addr(size == SPIFLASH_BLOCK ? CMD_BLOCK_ERASE : CMD_SECTOR_ERASE, addr);
    deselect();
    return wait_ready(chip, size == SPIFLASH_BLOCK ? 3000 : 1000);
}

bool spiflash_program(uint8_t chip, uint32_t addr, const uint8_t *data, uint16_t len) {
    if (chip != SPIFLASH_USER) return false;
    command(chip, CMD_WRITE_ENABLE);
    select(chip);
    command_addr(CMD_PAGE_PROGRAM, addr);
    for (uint16_t i = 0; i < len; i++) xfer(data[i]);
    deselect();
    return wait_ready(chip, 20);
}
