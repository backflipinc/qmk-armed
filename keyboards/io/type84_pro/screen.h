// User animation on the screen, stored like the stock firmware does (screen.c)
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "qp.h"

void    screen_init(void);
uint8_t screen_frames(void);          // frames stored, 0 = none
int8_t  screen_upload_progress(void); // 0..100 while an upload runs, -1 otherwise
void    screen_play_restart(void);    // repaint from the first frame
bool    screen_dma_busy(void);        // a frame is being sent to the display by DMA
void    screen_dma_wait(void);        // let it finish
// Starts sending the next frame when it is time; returns true if it did
bool screen_play_task(painter_device_t lcd);
