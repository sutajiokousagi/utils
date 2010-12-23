// $Id$
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifndef WIDTH
#define WIDTH 320
#endif

#ifndef HEIGHT
#define HEIGHT 240
#endif

#ifndef BPP
#define BPP 2
#endif

#ifdef _FBTEXT_H_OWNER
int g_width = WIDTH;
int g_height = HEIGHT;
int g_bpp = BPP;
#else
extern int g_width, g_height, g_bpp;
#endif

#include "font_6x11.c"
//#include "font_sun12x22.c"
#define FONT_TABLE fontdata_6x11
#define FONT_WIDTH 6
#define FONT_HEIGHT 11

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

