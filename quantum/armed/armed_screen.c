// Copyright 2026 backflipinc
// SPDX-License-Identifier: GPL-2.0-or-later

// qmk-armed screen animation upload: commands and the stream decoder (format in docs/armed.md).
// The stream is a list of operations on 16-bit pixels, so a 32-byte report carries more than
// 14 pixels whenever the picture repeats itself:
//   control byte: op << 6 | n, n = 1..63; n = 0 means a 16-bit length (LE) follows
//   op 0 LIT  - n pixels follow, 2 bytes each
//   op 1 RUN  - one pixel follows, repeated n times
//   op 2 PREV - n pixels copied from the previous frame (same place, one frame back)

#include "armed.h"
#include "quantum.h"
#include "via.h"

__attribute__((weak)) bool armed_screen_info_kb(armed_screen_info_t *info) {
    return false;
}

__attribute__((weak)) bool armed_screen_begin_kb(uint32_t size) {
    return false;
}

__attribute__((weak)) bool armed_screen_write_kb(const uint8_t *data, uint16_t length) {
    return false;
}

__attribute__((weak)) bool armed_screen_end_kb(bool commit) {
    return false;
}

__attribute__((weak)) bool armed_screen_read_kb(uint32_t offset, uint8_t *data, uint16_t length) {
    return false;
}

enum { OP_LIT, OP_RUN, OP_PREV };
enum { S_CTRL, S_LEN0, S_LEN1, S_LIT0, S_LIT1, S_RUN0, S_RUN1 };

static struct {
    bool     active, failed;
    uint8_t  state, op;
    uint16_t count; // pixels left in the operation
    uint8_t  lo;    // first byte of a pixel
    uint32_t size, pos, frame_bytes;
    uint8_t  out[64];
    uint8_t  out_len;
} up;

static void flush(void) {
    if (up.out_len && !armed_screen_write_kb(up.out, up.out_len)) up.failed = true;
    up.out_len = 0;
}

static void put(uint8_t b) {
    up.out[up.out_len++] = b;
    up.pos++;
    if (up.out_len == sizeof(up.out)) flush();
}

static bool room(uint32_t pixels) {
    return up.pos + pixels * 2 <= up.size;
}

static void prev_copy(uint16_t n) {
    uint32_t src = up.pos - up.frame_bytes;
    if (up.pos < ARMED_SCREEN_HEADER + up.frame_bytes || !room(n)) {
        up.failed = true;
        return;
    }
    uint8_t buf[64];
    for (uint32_t left = (uint32_t)n * 2; left && !up.failed;) {
        uint16_t chunk = MIN(left, sizeof(buf));
        if (!armed_screen_read_kb(src, buf, chunk)) {
            up.failed = true;
            return;
        }
        for (uint16_t i = 0; i < chunk; i++) put(buf[i]);
        src += chunk;
        left -= chunk;
    }
}

static void start_op(void) {
    if (up.count == 0 || !room(up.count)) {
        up.failed = true;
        return;
    }
    switch (up.op) {
        case OP_LIT:
            up.state = S_LIT0;
            break;
        case OP_RUN:
            up.state = S_RUN0;
            break;
        case OP_PREV:
            prev_copy(up.count);
            up.state = S_CTRL;
            break;
        default:
            up.failed = true;
    }
}

static void feed(uint8_t b) {
    switch (up.state) {
        case S_CTRL:
            up.op    = b >> 6;
            up.count = b & 0x3F;
            if (up.count) {
                start_op();
            } else {
                up.state = S_LEN0;
            }
            break;
        case S_LEN0:
            up.count = b;
            up.state = S_LEN1;
            break;
        case S_LEN1:
            up.count |= (uint16_t)b << 8;
            start_op();
            break;
        case S_LIT0:
            put(b);
            up.state = S_LIT1;
            break;
        case S_LIT1:
            put(b);
            up.state = --up.count ? S_LIT0 : S_CTRL;
            break;
        case S_RUN0:
            up.lo    = b;
            up.state = S_RUN1;
            break;
        case S_RUN1:
            while (up.count-- && !up.failed) {
                put(up.lo);
                put(b);
            }
            up.state = S_CTRL;
            break;
    }
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static uint32_t get32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool info(uint8_t *data) {
    armed_screen_info_t i = {0};
    if (!armed_screen_info_kb(&i)) return false;
    data[2] = i.width;
    data[3] = i.width >> 8;
    data[4] = i.height;
    data[5] = i.height >> 8;
    data[6] = i.format;
    data[7] = i.max_frames;
    data[8] = up.active ? 0 : i.frames;
    put32(&data[9], i.capacity);
    data[13] = ARMED_SCREEN_HEADER & 0xFF;
    data[14] = ARMED_SCREEN_HEADER >> 8;
    return true;
}

static bool begin(uint8_t *data) {
    armed_screen_info_t i = {0};
    if (up.active) armed_screen_end_kb(false);
    up.active = false;
    uint32_t size = get32(&data[2]);
    if (!armed_screen_info_kb(&i)) return false;
    up.frame_bytes = (uint32_t)i.width * i.height * 2;
    if (size <= ARMED_SCREEN_HEADER || size > i.capacity || (size - ARMED_SCREEN_HEADER) % up.frame_bytes) return false;
    if (!armed_screen_begin_kb(size)) return false;
    up.active  = true;
    up.failed  = false;
    up.state   = S_CTRL;
    up.size    = size;
    up.pos     = 0;
    up.out_len = 0;
    return true;
}

// [2] sequence number (echoed), [3] length, [4..] stream -> [3] status, [4..7] bytes decoded
static void stream(uint8_t *data, uint8_t length) {
    uint8_t n = data[3];
    if (!up.active || n > length - 4) {
        data[3] = 1;
    } else {
        for (uint8_t i = 0; i < n && !up.failed; i++) feed(data[4 + i]);
        data[3] = up.failed;
    }
    put32(&data[4], up.pos);
    if (data[3]) data[0] = id_unhandled;
}

// [2] 1 = commit, 0 = abort
static bool end(uint8_t *data) {
    if (!up.active) return false;
    up.active = false;
    if (data[2]) flush();
    bool commit = data[2] && !up.failed && up.state == S_CTRL && up.pos == up.size;
    return armed_screen_end_kb(commit) && commit;
}

// [2..5] offset, [6] length -> [7..] data
static bool read_back(uint8_t *data, uint8_t length) {
    uint8_t n = data[6];
    if (up.active || n > length - 7) return false;
    return armed_screen_read_kb(get32(&data[2]), &data[7], n);
}

void armed_screen_receive(uint8_t *data, uint8_t length) {
    bool ok = false;
    switch (data[1]) {
        case ARMED_SCREEN_INFO:
            ok = info(data);
            break;
        case ARMED_SCREEN_BEGIN:
            ok = begin(data);
            break;
        case ARMED_SCREEN_DATA:
            stream(data, length);
            return;
        case ARMED_SCREEN_END:
            ok = end(data);
            break;
        case ARMED_SCREEN_READ:
            ok = read_back(data, length);
            break;
    }
    if (!ok) data[0] = id_unhandled;
}
