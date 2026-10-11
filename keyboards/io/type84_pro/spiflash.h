// Two SPI NOR flash chips on SPI1 that hold the stock screen images (spiflash.c)
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Chip selects, named after the stock firmware's chip flag (0x20000232)
enum {
    SPIFLASH_FACTORY = 0, // CS P0.13: built-in images of the stock firmware, never written
    SPIFLASH_USER    = 1, // CS P1.9: the user animation uploaded from the configurator
    SPIFLASH_CHIPS,
};

#define SPIFLASH_PAGE 256
#define SPIFLASH_SECTOR 4096
#define SPIFLASH_BLOCK 65536

void     spiflash_init(void);
uint32_t spiflash_jedec_id(uint8_t chip); // manufacturer, type, capacity; 0 / 0xFFFFFF = no chip
void     spiflash_read(uint8_t chip, uint32_t addr, uint8_t *buf, uint32_t len);
// Sequential read in pieces: CS stays low from begin to end, nothing else may use SPI1 meanwhile
void spiflash_read_begin(uint8_t chip, uint32_t addr);
void spiflash_read_next(uint8_t *buf, uint32_t len);
void spiflash_read_end(void);
// size = SPIFLASH_SECTOR or SPIFLASH_BLOCK, addr aligned to it. Waits for the erase to finish
bool spiflash_erase(uint8_t chip, uint32_t addr, uint32_t size);
// Up to one page, must not cross a page boundary. Waits for the write to finish
bool spiflash_program(uint8_t chip, uint32_t addr, const uint8_t *data, uint16_t len);
void spiflash_power(bool on); // deep power-down of both chips
