// User animation on the screen: stored and played like the stock firmware, uploaded over qmk-armed.
//
// Stock layout (configurator command 0x50 SET_TFT_USER_ANIMATION, firmware sub_04e96 / sub_15f4c),
// on the flash chip at CS P1.9 from address 0:
//   header, 256 bytes: [0] frames, [1 + i] delay of frame i in 2 ms units, rest 0xFF
//   frames: 320x172 RGB565, low byte first, 110080 bytes each
// The stock firmware writes up to 152 frames, but never a 4 KiB block at 0xFF5000 or above, so
// the last 256 bytes of a 152-frame animation are whatever was there. We play such animations
// and upload at most 151 frames, staying below 0xFF5000 too. The other chip
// (CS P0.13) holds the stock built-in images and is left alone, so an animation uploaded here
// still plays after going back to stock, and the stock one plays here.
//
// Playback sends each frame straight from the flash (SPI1) to the display (SPI0) by DMA in the
// background: 37 ms per frame, the CPU stays free for the keys.

#include "quantum.h"

#ifdef QUANTUM_PAINTER_ENABLE

#    include "screen.h"
#    include "spiflash.h"
#    include "qp_comms.h"

#    ifdef ARMED_ENABLE
#        include "armed.h"
#    endif

#    define W 320
#    define H 172
#    define ROW_BYTES (W * 2)
#    define FRAME_BYTES ((uint32_t)ROW_BYTES * H)
#    define HEADER 256
#    define WRITE_LIMIT 0xFF5000UL
#    define CHIP_SIZE 0x1000000UL
#    define MAX_FRAMES ((WRITE_LIMIT - HEADER) / FRAME_BYTES) // 151 for uploads
#    define PLAY_MAX_FRAMES 152                                // the stock configurator's limit

static uint8_t header[HEADER];
static uint8_t frames;

static uint8_t header_frames(void) {
    uint8_t n = header[0];
    return n && n <= PLAY_MAX_FRAMES ? n : 0;
}

void screen_init(void) {
    spiflash_init();
    spiflash_read(SPIFLASH_USER, 0, header, HEADER);
    frames = header_frames();
}

uint8_t screen_frames(void) {
    return frames;
}

/* --------------------------------------------------------------------------
 * Playback
 * ------------------------------------------------------------------------ */

static struct {
    uint8_t  frame;
    bool     drawn;    // the current frame has been sent
    uint32_t shown_at; // when the current frame started
    bool     idle;     // a single still image is on screen
} play;

static uint32_t delay_ms(uint8_t frame) {
    return (uint32_t)header[1 + frame] * 2;
}

void screen_play_restart(void) {
    play.frame = 0;
    play.drawn = false;
    play.idle  = false;
    play.shown_at = timer_read32();
}

static bool uploading(void);

#    define SPI_STAT_BUSY (1UL << 4)
#    define SPI_CTRL0_FRESET (3UL << 6)
#    define SPI_DMA_EN (1UL << 0)
#    define SPI_DMA_DIR (1UL << 1)
#    define SPI_IE_RXFIFOTH (1UL << 2)
#    define SPI_INT_DMATC (1UL << 5)

// SPI0's SPI-to-SPI DMA, as stock sub_107b6 / SPI0 IRQ sub_145f6: SPI0 clocks a whole frame out
// of SPI1 (the flash, read command already sent) straight into the display, 110 KiB in 37 ms,
// while the main loop goes on. Both interrupt enables are needed (stock sets SPI1 RXFIFOTHIE in
// sub_14690) though no IRQ is taken: without them the transfer never completes. Pixels reach the
// panel in the right order as stored, low byte first.
// Anything else that talks to the display or the flash waits for the frame first: lcd_spi.c
// before it sends anything, spiflash.c before every command.
static struct {
    bool             active;
    systime_t        t0;
    uint32_t         div;
} dma;

static void dma_start(painter_device_t lcd, uint32_t addr, uint32_t len) {
    qp_viewport(lcd, 0, 0, W - 1, H - 1); // ends with RAMWR
    qp_comms_start(lcd);                  // CS low
    gpio_write_pin_high(LCD_DC_PIN);      // pixel data
    spiflash_read_begin(SPIFLASH_USER, addr);

    dma.t0  = chVTGetSystemTimeX();
    dma.div = SN_SPI0->CLKDIV;
    SN_SPI0->CLKDIV = 0;                // 24 MHz like stock; the HUD keeps its own rate
    SN_SPI1->CTRL0 |= SPI_CTRL0_FRESET; // drop the read command's RX bytes
    SN_SPI0->CTRL0 |= SPI_CTRL0_FRESET;
    SN_SPI0->DMACTRL &= ~SPI_DMA_DIR;
    SN_SPI0->DMACNT   = len;
    SN_SPI0->DMAHTCNT = len / 2;
    SN_SPI0->IC       = 0x3F;
    SN_SPI1->IC       = 0x3F;
    SN_SPI1->IE |= SPI_IE_RXFIFOTH;
    SN_SPI0->IE |= SPI_INT_DMATC;
    dma.active = true;
    SN_SPI0->DMACTRL |= SPI_DMA_EN;
}

static void dma_finish(void) {
    SN_SPI0->DMACTRL &= ~SPI_DMA_EN;
    SN_SPI0->IE &= ~SPI_INT_DMATC;
    SN_SPI1->IE &= ~SPI_IE_RXFIFOTH;
    SN_SPI0->IC = 0x3F;
    SN_SPI1->IC = 0x3F;
    while (SN_SPI0->STAT & SPI_STAT_BUSY) {
    }
    SN_SPI0->CTRL0 |= SPI_CTRL0_FRESET;
    SN_SPI1->CTRL0 |= SPI_CTRL0_FRESET;
    SN_SPI0->CLKDIV = dma.div;
    dma.active      = false;
    spiflash_read_end();
    gpio_write_pin_high(LCD_CS_PIN); // not qp_comms_stop(): a QP transaction may be waiting on us
}

// true while a frame is still being sent
static bool dma_poll(void) {
    if (!dma.active) return false;
    if (SN_SPI0->RIS & SPI_INT_DMATC) {
        dma_finish();
        return false;
    }
    if (chVTGetSystemTimeX() - dma.t0 > TIME_MS2I(200)) { // never seen; a frame takes 37 ms
        dma_finish();
        play.idle = true; // a still screen rather than a stall per frame
        return false;
    }
    return true;
}

bool screen_dma_busy(void) {
    return dma_poll();
}

void screen_dma_wait(void) {
    while (dma_poll()) {
    }
}

bool screen_play_task(painter_device_t lcd) {
    if (dma_poll() || !frames || uploading() || play.idle) return false;

    if (play.drawn) {
        if (frames == 1) {
            play.idle = true;
            return false;
        }
        if (timer_elapsed32(play.shown_at) < delay_ms(play.frame)) return false;
        play.frame    = (play.frame + 1) % frames;
        play.shown_at = timer_read32();
    }
    dma_start(lcd, HEADER + play.frame * FRAME_BYTES, FRAME_BYTES);
    play.drawn = true;
    return true;
}

/* --------------------------------------------------------------------------
 * Upload (qmk-armed ARMED_F_SCREEN)
 * ------------------------------------------------------------------------ */

#    ifdef ARMED_ENABLE

static struct {
    bool     active;
    uint32_t size, pos;
    uint32_t erased; // flash below this address is erased
    uint8_t  page[SPIFLASH_PAGE];
    bool     failed;
} up;

static bool uploading(void) {
    return up.active;
}

int8_t screen_upload_progress(void) {
    return up.active ? (int8_t)(up.pos / (up.size / 100 + 1)) : -1;
}

// Erase ahead of a page write: 64 KiB blocks where the upload covers the whole block
static bool erase_to(uint32_t end) {
    while (up.erased < end) {
        bool block = up.erased % SPIFLASH_BLOCK == 0 && up.erased + SPIFLASH_BLOCK <= up.size;
        if (!spiflash_erase(SPIFLASH_USER, up.erased, block ? SPIFLASH_BLOCK : SPIFLASH_SECTOR)) return false;
        up.erased += block ? SPIFLASH_BLOCK : SPIFLASH_SECTOR;
    }
    return true;
}

static bool program_page(uint32_t addr, uint16_t len) {
    return erase_to(addr + SPIFLASH_PAGE) && spiflash_program(SPIFLASH_USER, addr, up.page, len);
}

bool armed_screen_info_kb(armed_screen_info_t *info) {
    info->width      = W;
    info->height     = H;
    info->format     = ARMED_SCREEN_RGB565_LE;
    info->max_frames = MAX_FRAMES;
    info->frames     = frames;
    info->capacity   = HEADER + MAX_FRAMES * FRAME_BYTES;
    return true;
}

// The header is kept in RAM and written last: an upload cut short leaves no animation, not a broken one
bool armed_screen_begin_kb(uint32_t size) {
    frames    = 0;
    up.active = true;
    up.failed = false;
    up.size   = size;
    up.pos    = 0;
    up.erased = 0;
    memset(header, 0xFF, sizeof(header));
    if (!erase_to(SPIFLASH_SECTOR)) {
        up.active = false;
        return false;
    }
    return true;
}

bool armed_screen_write_kb(const uint8_t *data, uint16_t length) {
    for (uint16_t i = 0; i < length && !up.failed; i++, up.pos++) {
        if (up.pos < HEADER) {
            header[up.pos] = data[i];
            continue;
        }
        up.page[up.pos % SPIFLASH_PAGE] = data[i];
        if (up.pos % SPIFLASH_PAGE == SPIFLASH_PAGE - 1 && !program_page(up.pos - (SPIFLASH_PAGE - 1), SPIFLASH_PAGE)) up.failed = true;
    }
    return !up.failed;
}

bool armed_screen_end_kb(bool commit) {
    up.active = false;
    uint16_t tail = up.pos % SPIFLASH_PAGE;
    if (commit && tail && up.pos > HEADER && !program_page(up.pos - tail, tail)) up.failed = true;
    if (commit && !up.failed && header_frames() && header_frames() <= MAX_FRAMES && (uint32_t)header_frames() * FRAME_BYTES + HEADER == up.size) {
        if (!spiflash_program(SPIFLASH_USER, 0, header, HEADER)) return false;
        frames = header_frames();
    } else {
        memset(header, 0xFF, sizeof(header));
        frames = 0;
    }
    screen_play_restart();
    return frames || !commit;
}

bool armed_screen_read_kb(uint32_t offset, uint8_t *data, uint16_t length) {
    if (offset + length > CHIP_SIZE) return false;
    if (up.active && offset + length > up.pos - up.pos % SPIFLASH_PAGE) return false; // not written yet
    if (offset < HEADER) {
        uint16_t n = MIN(length, HEADER - offset);
        memcpy(data, &header[offset], n);
        offset += n;
        data += n;
        length -= n;
    }
    if (length) spiflash_read(SPIFLASH_USER, offset, data, length);
    return true;
}

#    else

static bool uploading(void) {
    return false;
}

int8_t screen_upload_progress(void) {
    return -1;
}

#    endif

#endif // QUANTUM_PAINTER_ENABLE
