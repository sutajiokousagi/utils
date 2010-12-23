// $Id$
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifndef WIDTH
#define WIDTH 320
#endif

#ifndef HEIGHT
#define HEIGHT 240
#endif

#ifdef _FBTEXT_H_OWNER
int g_width = WIDTH;
int g_height = HEIGHT;
#else
extern int g_width, g_height;
#endif

#define BYTES_PER_PIXEL 2

#if (WIDTH>320)

// 800x480, 800x600, etc.
#include "font_sun12x22.c"
#define FONT_TABLE fontdata_sun12x22
#define FONT_WIDTH 12
#define FONT_HEIGHT 22

#else

// Ironforge
#include "font_6x11.c"
#define FONT_TABLE fontdata_6x11
#define FONT_WIDTH 6
#define FONT_HEIGHT 11

#endif

#define ROWS ((g_height-FONT_HEIGHT+1)/FONT_HEIGHT)
#define COLS ((g_width-FONT_WIDTH+1)/FONT_WIDTH)

#define PIXELS_PER_ROW (FONT_HEIGHT*g_width)

void fbtext_init(void);
void fbtext_clear(void);
void fbtext_scroll(void);
void assure_fb(void);
void fbtext_putc(char c);
void fbtext_setcolor(unsigned short r, unsigned short g, unsigned short b);
void fbtext_puts(char *s);
void fbtext_gotoxy(short x,short y);
void fbtext_printf(char const* fmt, ...);

void fbtext_fillrect(unsigned int top,unsigned int left,
                     unsigned int bottom,unsigned int right,
                     unsigned int r,unsigned int g, unsigned int b);

void fbtext_eraserect(unsigned int top,unsigned int left,
                      unsigned int bottom,unsigned int right);

void fbtext_gotoxc( short x );
void fbtext_gotoyc( short y );
void fbtext_gotoxyc( short x, short y );
