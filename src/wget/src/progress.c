/* Download progress.
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.

This file is part of GNU Wget.

GNU Wget is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GNU Wget is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Wget; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

In addition, as a special exception, the Free Software Foundation
gives permission to link the code of its release of Wget with the
OpenSSL project's "OpenSSL" library (or with modified versions of it
that use the same license as the "OpenSSL" library), and distribute
the linked executables.  You must obey the GNU General Public License
in all respects for all of the code used other than "OpenSSL".  If you
modify this file, you may extend this exception to your version of the
file, but you are not obligated to do so.  If you do not wish to do
so, delete this exception statement from your version.

- Added support for --progressbar - Ken Steele <ksteele@chumby.com>, 6/29/07
*/

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#else
# include <strings.h>
#endif /* HAVE_STRING_H */
#include <assert.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif

#include <linux/fb.h>


#include "wget.h"
#include "progress.h"
#include "utils.h"
#include "retr.h"

#define _FBTEXT_H_OWNER
#include "fbtext.h"


static FILE *fb_file;
unsigned short *fb;
static int cur_x,cur_y;
static unsigned short *fb_background;
static unsigned short fb_color   = 0; // font color
static unsigned short fb_color32 = 0; // 32-bit font color


void fbtext_init(void)
{
	FILE *video_res = fopen( "/psp/video_res", "r" );
	// Check for VIDEO_X_RES and VIDEO_Y_RES in environment
	char *envx = getenv( "VIDEO_X_RES" );
	char *envy = getenv( "VIDEO_Y_RES" );

	if (envx != NULL && envy != NULL)
	{
		g_width = atoi( envx );
		g_height = atoi( envy );
	}

	if (video_res)
	{
		// Scan for VIDEO_RES=<width>x<height>
		char buff[256];
		while (fgets( buff, sizeof(buff), video_res ))
		{
			const char *cmd = strtok( buff, "=x\n" );
			const char *width = strtok( NULL, "=x\n" );
			const char *height = strtok( NULL, "=x\n" );
			if (cmd == NULL)
			{
				continue;
			}
			if (strcmp( cmd, "VIDEO_RES" ))
			{
				continue;
			}
			if (width == NULL || height == NULL)
			{
				continue;
			}
			if (atoi( width ) <= 0 || atoi( height ) <= 0)
			{
				continue;
			}
			g_width = atoi( width );
			g_height = atoi( height );
		}
		fclose( video_res );
	}
}

void fbtext_clear(void)
{
    if (fb)
    {
        //memset(fb,0,g_width*g_height*g_bpp);
        memcpy(fb,fb_background,g_width*g_height*g_bpp);
        cur_x = 0;
        cur_y = 0;
    }
}

void fbtext_scroll(void)
{
    short *dst = (short*)fb;
    short *src = (short*)&fb[FONT_HEIGHT*g_width];
    int offset;
    memmove(dst,src,ROWS*PIXELS_PER_ROW*g_bpp);
    offset = (ROWS*PIXELS_PER_ROW);
    memset(&fb[offset],0,((g_width*g_height)-offset)*g_bpp);
}

#ifdef CNPLATFORM_yume
struct fb_var_screeninfo fb_var;
struct fb_fix_screeninfo fb_fix;
char * fb_base_addr = NULL;
int fb_write_off = 0;
long int screensize = 0, pagesize;
#endif

void assure_fb(void)
{
#ifdef CNPLATFORM_yume
    int fh;
#endif

    if (!fb_file)
    {
		struct fb_var_screeninfo info;
    	fbtext_init();
        fb_file = fopen("/dev/fb0","r+");
        if (!fb_file)
        {
            fprintf(stderr,"Can't open /dev/fb0\n");
            exit(1);
        }
#ifdef CNPLATFORM_yume
        fh = fileno( fb_file );
        // Get fixed screen information
        if (ioctl(fh, FBIOGET_FSCREENINFO, &fb_fix)) {
                printf("Error reading fb fixed information.\n");
                exit(1);
        }

        // Get variable screen information
        if (ioctl(fh, FBIOGET_VSCREENINFO, &fb_var)) {
                printf("Error reading fb variable information.\n");
                exit(1);
        }

        screensize = fb_var.xres * fb_var.yres * fb_var.bits_per_pixel / 8;

        pagesize = sysconf(_SC_PAGESIZE);
        //printf("%dx%d, %dbpp, size in Bytes=%ld, pagesize=%ld\n", fb_var.xres, fb_var.yres, fb_var.bits_per_pixel,
        //       screensize, pagesize );

        /* fix #6351 comment26 e.m. 2006oct20 */
        fb = (unsigned short *)mmap(NULL , screensize+pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, fh, 0);

        if (fb == (unsigned short *)-1) {
                printf("error mapping fb\n");
                exit(1);
        }

        /* temporary fix for 5159, mapping is paged aligned */
        if (fb_fix.smem_start & (pagesize-1)) {
                *((unsigned char **)&fb) += fb_fix.smem_start & (pagesize-1);
                //fprintf(stderr, "Fix alignment 0x%08lx -> %lx, x=%d, y=%d\n",
                //      fb_fix.smem_start, fb, cur_x, cur_y);
        }
#else
        // Dynamically grab screen info
        if(ioctl(fileno(fb_file), FBIOGET_VSCREENINFO, &info) != -1) {
        	g_width  = info.xres;
        	g_height = info.yres;
        	g_bpp    = info.bits_per_pixel / 8;
        }
    
        fb = (unsigned short *)mmap(0,
            g_width*g_height*g_bpp,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fileno(fb_file),
            0);

        if (fb==(unsigned short *)-1)
        {
            fprintf(stderr,"Can't mmap framebuffer, errno=%d\n",errno);
            exit(1);
        }
#endif
        fb_background = (unsigned short *)malloc(g_width*g_height*g_bpp);
        memcpy(fb_background,fb,g_width*g_height*g_bpp);
        fbtext_clear();
    }
}

void fbtext_putc(char c)
{
    assure_fb();
    if (fb)
    {
        switch (c)
        {
            case '\r':
                cur_x = 0;
                break;
            case '\n':
                cur_x = 0;
                cur_y+=FONT_HEIGHT;
                if (cur_y>g_height-FONT_HEIGHT)
                {
                    fbtext_scroll();
                    cur_y-=FONT_HEIGHT;
                }
                break;
            case 0x08: // backspace
                cur_x-=FONT_WIDTH;
                if (cur_x<0)
                {
                    if (cur_y>0)
                    {
                        cur_x = (COLS-1)*FONT_WIDTH;
                        cur_y-=FONT_HEIGHT;
                    } else
                        cur_x = 0;
                }
                break;
            case '\t':
                {
                    int new_x = (((cur_x/FONT_WIDTH+1)/8)+1)*8*FONT_WIDTH;
                    int delta = (new_x-cur_x)/FONT_WIDTH;
                    while (delta--)
                    {
                        fbtext_putc(' ');
                    }
                }
                break;
            default:
            {
                unsigned char *cp = &FONT_TABLE[((unsigned)c)*FONT_HEIGHT*(FONT_WIDTH>8?2:1)];
                uint32_t *cp32 = (uint32_t *)&FONT_TABLE[((unsigned)c)*FONT_HEIGHT*(FONT_WIDTH>8?2:1)];
                int y;
                for (y=0;y<FONT_HEIGHT;y++)
                {
                    unsigned long offset = (cur_y+y)*g_width+cur_x;
                    if (offset+FONT_WIDTH<g_width*g_height)
                    {
                        unsigned short *line = &fb[offset];
                        unsigned short *bg = &fb_background[offset];
                        unsigned short b = *cp++;
						uint32_t *line32 = (uint32_t *)&fb[offset];
						uint32_t *bg32   = (uint32_t *)&fb_background[offset];
						uint32_t b32     = *cp32++;
                        int x;
                        if (FONT_WIDTH>8)
                        {
							if(g_bpp == 2)
								b = (b<<8)+*cp++;
							else if(g_bpp == 4)
								b32 = (b32<<8)+*cp32++;
                        }
                        for (x=0;x<FONT_WIDTH;x++)
                        {
                            // hacked to skip first column
							if(g_bpp == 2) {
								if (b&(1<<((FONT_WIDTH>8)?16:8)-(x+1)))
								{
									*line++ = fb_color;
									bg++;
								}
								else
								{
									*line++ = *bg++;
								}
							}
							else if(g_bpp == 4) {
								if (b32&(1<<((FONT_WIDTH>8)?16:8)-(x+1)))
								{
									*line32++ = fb_color32;
									bg32++;
								}
								else
								{
									*line32++ = *bg32++;
								}
							}
                        }
                    }
                }
                cur_x+=FONT_WIDTH;
                if (cur_x>(COLS*FONT_WIDTH))
                {
                    fbtext_putc('\n');
                }
            }
        }
    }
}

//
// set the color of the text
//
void fbtext_setcolor(unsigned short r, unsigned short g, unsigned short b)
{
	fb_color = ((r&0xf8)<<8) + ((g&0xfc)<<3) + ((b&0xf8)>>3);
	fb_color32 = (r<<16) | (g<<8) | b;
}

//
// put text on the display
//
void fbtext_puts(char *s)
{
    while (*s)
    {
        fbtext_putc(*s++);
    }
}

//
// x and y are pixel coordinates
//
void fbtext_gotoxy(short x,short y)
{
    cur_x = x<0?0:(x>=g_width?g_width-1:x);
    cur_y = y<0?0:(y>=g_height?g_height-1:y);
}

//
// a printf-like function for formatting and outputting text
//
void fbtext_printf(char const* fmt, ...)
{
    va_list arg;
    va_start(arg,fmt);
    char *s;
    vasprintf(&s,fmt,arg);
    fbtext_puts(s);
    free(s);
    va_end(arg);
}

//
// bottom and right are *not* in the rectangle, so top==bottom and/or left==right results in nothing
//
void fbtext_fillrect(unsigned int top,unsigned int left,
                     unsigned int bottom,unsigned int right,
                     unsigned int r,unsigned int g, unsigned int b)
{
	unsigned int y;
	uint16_t color16 = ((r&0xf8)<<8) + ((g&0xfc)<<3) + ((b&0xf8)>>3);
	uint32_t color32 = (r<<16) | (g<<8) | b;

    assure_fb();

	// Sanity check the values
	if (top>=bottom || left>=right)
		return;

	if(right>g_width)
		right = g_width;

	if (bottom>g_height)
		bottom = g_height;

	if(g_bpp == 2) {
		uint16_t *g_16 = (uint16_t *)fb;
		for(y=top; y<bottom; y++) {
			uint16_t *line = &g_16[y*g_width+left];
			unsigned width = right-left;
			while(width--)
				*line++ = color16;
		}
	}
	else {
		uint32_t *g_32 = (uint32_t *)fb;
		for(y=top; y<bottom; y++) {
			uint32_t *line = &g_32[y*g_width+left];
			unsigned width = right-left;
			while(width--)
				*line++ = color32;
		}
	}

}

//
// restores the background image for this rectangle
//
void fbtext_eraserect(unsigned int top,unsigned int left,
                      unsigned int bottom,unsigned int right)
{
    unsigned int y;
    assure_fb();
    if (right>g_width)
        right = g_width;

    if (bottom>g_height)
        bottom = g_height;

    for(y=top;y<bottom;y++) {
        unsigned long offset = y*g_width+left;
        unsigned width = right-left;
		if(g_bpp == 2) {
			unsigned short *bg = &fb_background[offset];
			unsigned short *line = &fb[offset];
			while (width--)
				*line++ = *bg++;
		}
		else if(g_bpp == 4) {
			unsigned long *bg   = (unsigned long *) &fb_background[offset];
			unsigned long *line = (unsigned long *) &fb[offset];
			while (width--)
				*line++ = *bg++;
		}
    }
}


struct progress_implementation {
  const char *name;
  int interactive;
  void *(*create) PARAMS ((wgint, wgint));
  void (*update) PARAMS ((void *, wgint, double));
  void (*finish) PARAMS ((void *, double));
  void (*set_params) PARAMS ((const char *));
};

/* Necessary forward declarations. */

static void *dot_create PARAMS ((wgint, wgint));
static void dot_update PARAMS ((void *, wgint, double));
static void dot_finish PARAMS ((void *, double));
static void dot_set_params PARAMS ((const char *));

static void *bar_create PARAMS ((wgint, wgint));
static void bar_update PARAMS ((void *, wgint, double));
static void bar_finish PARAMS ((void *, double));
static void bar_set_params PARAMS ((const char *));

static struct progress_implementation implementations[] = {
  { "dot", 0, dot_create, dot_update, dot_finish, dot_set_params },
  { "bar", 1, bar_create, bar_update, bar_finish, bar_set_params }
};
static struct progress_implementation *current_impl;
static int current_impl_locked;

/* Progress implementation used by default.  Can be overriden in
   wgetrc or by the fallback one.  */

#define DEFAULT_PROGRESS_IMPLEMENTATION "bar"

/* Fallnback progress implementation should be something that works
   under all display types.  If you put something other than "dot"
   here, remember that bar_set_params tries to switch to this if we're
   not running on a TTY.  So changing this to "bar" could cause
   infloop.  */

#define FALLBACK_PROGRESS_IMPLEMENTATION "dot"

/* Return non-zero if NAME names a valid progress bar implementation.
   The characters after the first : will be ignored.  */

int
valid_progress_implementation_p (const char *name)
{
  int i;
  struct progress_implementation *pi = implementations;
  char *colon = strchr (name, ':');
  int namelen = colon ? colon - name : strlen (name);

  for (i = 0; i < countof (implementations); i++, pi++)
    if (!strncmp (pi->name, name, namelen))
      return 1;
  return 0;
}

/* Set the progress implementation to NAME.  */

void
set_progress_implementation (const char *name)
{
  int i, namelen;
  struct progress_implementation *pi = implementations;
  char *colon;

  if (!name)
    name = DEFAULT_PROGRESS_IMPLEMENTATION;

  colon = strchr (name, ':');
  namelen = colon ? colon - name : strlen (name);

  for (i = 0; i < countof (implementations); i++, pi++)
    if (!strncmp (pi->name, name, namelen))
      {
	current_impl = pi;
	current_impl_locked = 0;

	if (colon)
	  /* We call pi->set_params even if colon is NULL because we
	     want to give the implementation a chance to set up some
	     things it needs to run.  */
	  ++colon;

	if (pi->set_params)
	  pi->set_params (colon);
	return;
      }
  abort ();
}

static int output_redirected;

void
progress_schedule_redirect (void)
{
  output_redirected = 1;
}

/* Create a progress gauge.  INITIAL is the number of bytes the
   download starts from (zero if the download starts from scratch).
   TOTAL is the expected total number of bytes in this download.  If
   TOTAL is zero, it means that the download size is not known in
   advance.  */

void *
progress_create (wgint initial, wgint total)
{
  /* Check if the log status has changed under our feet. */
  if (output_redirected)
    {
      if (!current_impl_locked)
	set_progress_implementation (FALLBACK_PROGRESS_IMPLEMENTATION);
      output_redirected = 0;
    }

  return current_impl->create (initial, total);
}

/* Return non-zero if the progress gauge is "interactive", i.e. if it
   can profit from being called regularly even in absence of data.
   The progress bar is interactive because it regularly updates the
   ETA and current update.  */

int
progress_interactive_p (void *progress)
{
  return current_impl->interactive;
}

void write_fp_event( int percentage )
{
    char* fileName = "/tmp/flashplayer.event";
    struct stat buf;
    int i = stat( fileName, &buf );
    int ret = 0;
    FILE* fp = NULL;

    if( 100 == percentage )
    {
        // if we're at 100%, wait for the flash player to process the event and write out 100%
        while( 0 == stat( fileName, &buf ) )
        {
            // flashplayer.event still exists, so the flash player hasn't processed the event yet
            usleep( 100 );
        }
        // flashplayer.event got deleted, so lets send the 100% event
        if( fp = fopen( fileName, "w" ) )
        {
            fprintf( fp, "<event type=\"Progress\" value=\"progress\" comment=\"100\"/>\n" );
            fclose( fp );
            ret = system( "/usr/bin/chumbyflashplayer.x -F1 >/dev/null 2>&1 &" );
            usleep( 1000 );
        }
    }
    else if( 0 != i )
    {
        // event file doesn't exist, so lets write to it - the flash player is responsible for
        // deleting the event file when signaled.
        if( fp = fopen( fileName, "w" ) )
        {
            fprintf( fp, "<event type=\"Progress\" value=\"progress\" comment=\"%d\"/>\n", percentage );
            fclose( fp );
            ret = system( "/usr/bin/chumbyflashplayer.x -F1 >/dev/null 2>&1 &" );
        }
    }
}

void updateProgressBar( int perc )
{
//ATB - note: the scaling values are based on the location of the white bar in the "Downloading Control Panel" screen
#ifdef CNPLATFORM_yume
    int barValue = (int)( ( (int)(0.6448*g_width) * ( perc * 0.01 ) ) + (int)(0.178*g_width) );
    fbtext_fillrect( (int)(0.491*g_height), (int)(0.178*g_width), (int)(0.511833*g_height), barValue, 70, 125, 200 );
#elif CNPLATFORM_silvermoon
	#define BAR_LEFT 265.
	#define BAR_RIGHT 266.
	#define BAR_TOP 520.
	#define BAR_BOTTOM 63.

	// What the assumed screen width and height are for the above values.
	#define BAR_WIDTH 800.
	#define BAR_HEIGHT 600.

	// Scaled values.
	#define BAR_LEFT_S (BAR_LEFT/BAR_WIDTH)
	#define BAR_TOP_S (BAR_TOP/BAR_HEIGHT)
	#define BAR_BOTTOM_S ((BAR_HEIGHT-BAR_BOTTOM)/BAR_HEIGHT)
	#define BAR_RIGHT_S ((BAR_WIDTH-BAR_RIGHT)/BAR_WIDTH)

	int barValue = (int)( (BAR_LEFT_S*g_width) + ((perc*0.01) * (((BAR_WIDTH-BAR_RIGHT-BAR_LEFT)/BAR_WIDTH)*g_width)));
    fbtext_fillrect( (int)(BAR_TOP_S*g_height), (int)(BAR_LEFT_S*g_width), (int)(BAR_BOTTOM_S*g_height), barValue, 0x3d, 0x56, 0x6a );
//void fbtext_fillrect(unsigned int top,    unsigned int left,
//                     unsigned int bottom, unsigned int right,
//                     unsigned int r,unsigned int g, unsigned int b)
#else
    int barValue = (int)( ( (int)(0.8375*g_width) * ( perc * 0.01 ) ) + (int)(0.065625*g_width) );
    fbtext_fillrect( (int)(0.6*g_height), (int)(0.065625*g_width), (int)(0.670834*g_height), barValue, 0, 71, 182 );
#endif
}



/* Inform the progress gauge of newly received bytes.  DLTIME is the
   time in milliseconds since the beginning of the download.  */

void
progress_update (void *progress, wgint howmuch, double dltime)
{
  current_impl->update (progress, howmuch, dltime);
}

/* Tell the progress gauge to clean up.  Calling this will free the
   PROGRESS object, the further use of which is not allowed.  */

void
progress_finish (void *progress, double dltime)
{
  current_impl->finish (progress, dltime);
}

/* Dot-printing. */

struct dot_progress {
  wgint initial_length;		/* how many bytes have been downloaded
				   previously. */
  wgint total_length;		/* expected total byte count when the
				   download finishes */

  int accumulated;

  int rows;			/* number of rows printed so far */
  int dots;			/* number of dots printed in this row */
  double last_timer_value;
};

/* Dot-progress backend for progress_create. */

static void *
dot_create (wgint initial, wgint total)
{
  struct dot_progress *dp = xnew0 (struct dot_progress);
  dp->initial_length = initial;
  dp->total_length   = total;

  if (dp->initial_length)
    {
      int dot_bytes = opt.dot_bytes;
      wgint row_bytes = opt.dot_bytes * opt.dots_in_line;

      int remainder = (int) (dp->initial_length % row_bytes);
      wgint skipped = dp->initial_length - remainder;

      if (skipped)
	{
	  int skipped_k = (int) (skipped / 1024); /* skipped amount in K */
	  int skipped_k_len = numdigit (skipped_k);
	  if (skipped_k_len < 5)
	    skipped_k_len = 5;

	  /* Align the [ skipping ... ] line with the dots.  To do
	     that, insert the number of spaces equal to the number of
	     digits in the skipped amount in K.  */
	  logprintf (LOG_VERBOSE, _("\n%*s[ skipping %dK ]"),
		     2 + skipped_k_len, "", skipped_k);
	}

      logprintf (LOG_VERBOSE, "\n%5ldK", (long) (skipped / 1024));
      for (; remainder >= dot_bytes; remainder -= dot_bytes)
	{
	  if (dp->dots % opt.dot_spacing == 0)
	    logputs (LOG_VERBOSE, " ");
	  logputs (LOG_VERBOSE, ",");
	  ++dp->dots;
	}
      assert (dp->dots < opt.dots_in_line);

      dp->accumulated = remainder;
      dp->rows = skipped / row_bytes;
    }

  return dp;
}

static void
print_percentage (wgint bytes, wgint expected)
{
  int percentage = (int)(100.0 * bytes / expected);
  logprintf (LOG_VERBOSE, "%3d%%", percentage);
}

static void
print_download_speed (struct dot_progress *dp, wgint bytes, double dltime)
{
  logprintf (LOG_VERBOSE, " %s",
	     retr_rate (bytes, dltime - dp->last_timer_value, 1));
  dp->last_timer_value = dltime;
}

/* Dot-progress backend for progress_update. */

static void
dot_update (void *progress, wgint howmuch, double dltime)
{
  struct dot_progress *dp = progress;
  int dot_bytes = opt.dot_bytes;
  wgint row_bytes = opt.dot_bytes * opt.dots_in_line;

  log_set_flush (0);

  dp->accumulated += howmuch;
  for (; dp->accumulated >= dot_bytes; dp->accumulated -= dot_bytes)
    {
      if (dp->dots == 0)
	logprintf (LOG_VERBOSE, "\n%5ldK", (long) (dp->rows * row_bytes / 1024));

      if (dp->dots % opt.dot_spacing == 0)
	logputs (LOG_VERBOSE, " ");
      logputs (LOG_VERBOSE, ".");

      ++dp->dots;
      if (dp->dots >= opt.dots_in_line)
	{
	  wgint row_qty = row_bytes;
	  if (dp->rows == dp->initial_length / row_bytes)
	    row_qty -= dp->initial_length % row_bytes;

	  ++dp->rows;
	  dp->dots = 0;

	  if (dp->total_length)
	    print_percentage (dp->rows * row_bytes, dp->total_length);
	  print_download_speed (dp, row_qty, dltime);
	}
    }

  log_set_flush (1);
}

/* Dot-progress backend for progress_finish. */

static void
dot_finish (void *progress, double dltime)
{
  struct dot_progress *dp = progress;
  int dot_bytes = opt.dot_bytes;
  wgint row_bytes = opt.dot_bytes * opt.dots_in_line;
  int i;

  log_set_flush (0);

  if (dp->dots == 0)
    logprintf (LOG_VERBOSE, "\n%5ldK", (long) (dp->rows * row_bytes / 1024));
  for (i = dp->dots; i < opt.dots_in_line; i++)
    {
      if (i % opt.dot_spacing == 0)
	logputs (LOG_VERBOSE, " ");
      logputs (LOG_VERBOSE, " ");
    }
  if (dp->total_length)
    {
      print_percentage (dp->rows * row_bytes
			+ dp->dots * dot_bytes
			+ dp->accumulated,
			dp->total_length);
    }

  {
    wgint row_qty = dp->dots * dot_bytes + dp->accumulated;
    if (dp->rows == dp->initial_length / row_bytes)
      row_qty -= dp->initial_length % row_bytes;
    print_download_speed (dp, row_qty, dltime);
  }

  logputs (LOG_VERBOSE, "\n\n");
  log_set_flush (0);

  xfree (dp);
}

/* This function interprets the progress "parameters".  For example,
   if Wget is invoked with --progress=dot:mega, it will set the
   "dot-style" to "mega".  Valid styles are default, binary, mega, and
   giga.  */

static void
dot_set_params (const char *params)
{
  if (!params || !*params)
    params = opt.dot_style;

  if (!params)
    return;

  /* We use this to set the retrieval style.  */
  if (!strcasecmp (params, "default"))
    {
      /* Default style: 1K dots, 10 dots in a cluster, 50 dots in a
	 line.  */
      opt.dot_bytes = 1024;
      opt.dot_spacing = 10;
      opt.dots_in_line = 50;
    }
  else if (!strcasecmp (params, "binary"))
    {
      /* "Binary" retrieval: 8K dots, 16 dots in a cluster, 48 dots
	 (384K) in a line.  */
      opt.dot_bytes = 8192;
      opt.dot_spacing = 16;
      opt.dots_in_line = 48;
    }
  else if (!strcasecmp (params, "mega"))
    {
      /* "Mega" retrieval, for retrieving very long files; each dot is
	 64K, 8 dots in a cluster, 6 clusters (3M) in a line.  */
      opt.dot_bytes = 65536L;
      opt.dot_spacing = 8;
      opt.dots_in_line = 48;
    }
  else if (!strcasecmp (params, "giga"))
    {
      /* "Giga" retrieval, for retrieving very very *very* long files;
	 each dot is 1M, 8 dots in a cluster, 4 clusters (32M) in a
	 line.  */
      opt.dot_bytes = (1L << 20);
      opt.dot_spacing = 8;
      opt.dots_in_line = 32;
    }
  else
    fprintf (stderr,
	     _("Invalid dot style specification `%s'; leaving unchanged.\n"),
	     params);
}

/* "Thermometer" (bar) progress. */

/* Assumed screen width if we can't find the real value.  */
#define DEFAULT_SCREEN_WIDTH 80

/* Minimum screen width we'll try to work with.  If this is too small,
   create_image will overflow the buffer.  */
#define MINIMUM_SCREEN_WIDTH 45

/* The last known screen width.  This can be updated by the code that
   detects that SIGWINCH was received (but it's never updated from the
   signal handler).  */
static int screen_width;

/* A flag that, when set, means SIGWINCH was received.  */
static volatile sig_atomic_t received_sigwinch;

/* Size of the download speed history ring. */
#define DLSPEED_HISTORY_SIZE 20

/* The minimum time length of a history sample.  By default, each
   sample is at least 150ms long, which means that, over the course of
   20 samples, "current" download speed spans at least 3s into the
   past.  */
#define DLSPEED_SAMPLE_MIN 150

/* The time after which the download starts to be considered
   "stalled", i.e. the current bandwidth is not printed and the recent
   download speeds are scratched.  */
#define STALL_START_TIME 5000

struct bar_progress {
  wgint initial_length;		/* how many bytes have been downloaded
				   previously. */
  wgint total_length;		/* expected total byte count when the
				   download finishes */
  wgint count;			/* bytes downloaded so far */

  double last_screen_update;	/* time of the last screen update,
				   measured since the beginning of
				   download. */

  int width;			/* screen width we're using at the
				   time the progress gauge was
				   created.  this is different from
				   the screen_width global variable in
				   that the latter can be changed by a
				   signal. */
  char *buffer;			/* buffer where the bar "image" is
				   stored. */
  int tick;			/* counter used for drawing the
				   progress bar where the total size
				   is not known. */

  /* The following variables (kept in a struct for namespace reasons)
     keep track of recent download speeds.  See bar_update() for
     details.  */
  struct bar_progress_hist {
    int pos;
    wgint times[DLSPEED_HISTORY_SIZE];
    wgint bytes[DLSPEED_HISTORY_SIZE];

    /* The sum of times and bytes respectively, maintained for
       efficiency. */
    wgint total_time;
    wgint total_bytes;
  } hist;

  double recent_start;		/* timestamp of beginning of current
				   position. */
  wgint recent_bytes;		/* bytes downloaded so far. */

  int stalled;			/* set when no data arrives for longer
				   than STALL_START_TIME, then reset
				   when new data arrives. */

  /* create_image() uses these to make sure that ETA information
     doesn't flicker. */
  double last_eta_time;		/* time of the last update to download
				   speed and ETA, measured since the
				   beginning of download. */
  wgint last_eta_value;
};

static void create_image PARAMS ((struct bar_progress *, double));
static void display_image PARAMS ((char *));

static void *
bar_create (wgint initial, wgint total)
{
  struct bar_progress *bp = xnew0 (struct bar_progress);

  /* In theory, our callers should take care of this pathological
     case, but it can sometimes happen. */
  if (initial > total)
    total = initial;

  bp->initial_length = initial;
  bp->total_length   = total;

  /* Initialize screen_width if this hasn't been done or if it might
     have changed, as indicated by receiving SIGWINCH.  */
  if (!screen_width || received_sigwinch)
    {
      screen_width = determine_screen_width ();
      if (!screen_width)
	screen_width = DEFAULT_SCREEN_WIDTH;
      else if (screen_width < MINIMUM_SCREEN_WIDTH)
	screen_width = MINIMUM_SCREEN_WIDTH;
      received_sigwinch = 0;
    }

  /* - 1 because we don't want to use the last screen column. */
  bp->width = screen_width - 1;
  /* + 1 for the terminating zero. */
  bp->buffer = xmalloc (bp->width + 1);

  logputs (LOG_VERBOSE, "\n");

  create_image (bp, 0.0);
  display_image (bp->buffer);

  return bp;
}

static void update_speed_ring PARAMS ((struct bar_progress *, wgint, double));

static void
bar_update (void *progress, wgint howmuch, double dltime)
{
  struct bar_progress *bp = progress;
  int force_screen_update = 0;

  bp->count += howmuch;
  if (bp->total_length > 0
      && bp->count + bp->initial_length > bp->total_length)
    /* We could be downloading more than total_length, e.g. when the
       server sends an incorrect Content-Length header.  In that case,
       adjust bp->total_length to the new reality, so that the code in
       create_image() that depends on total size being smaller or
       equal to the expected size doesn't abort.  */
    bp->total_length = bp->initial_length + bp->count;

  update_speed_ring (bp, howmuch, dltime);

  /* If SIGWINCH (the window size change signal) been received,
     determine the new screen size and update the screen.  */
  if (received_sigwinch)
    {
      int old_width = screen_width;
      screen_width = determine_screen_width ();
      if (!screen_width)
	screen_width = DEFAULT_SCREEN_WIDTH;
      else if (screen_width < MINIMUM_SCREEN_WIDTH)
	screen_width = MINIMUM_SCREEN_WIDTH;
      if (screen_width != old_width)
	{
	  bp->width = screen_width - 1;
	  bp->buffer = xrealloc (bp->buffer, bp->width + 1);
	  force_screen_update = 1;
	}
      received_sigwinch = 0;
    }

  if (dltime - bp->last_screen_update < 200 && !force_screen_update)
    /* Don't update more often than five times per second. */
    return;

  create_image (bp, dltime);
  display_image (bp->buffer);
  bp->last_screen_update = dltime;
}

static void
bar_finish (void *progress, double dltime)
{
  struct bar_progress *bp = progress;

  if (bp->total_length > 0
      && bp->count + bp->initial_length > bp->total_length)
    /* See bar_update() for explanation. */
    bp->total_length = bp->initial_length + bp->count;

  create_image (bp, dltime);
  display_image (bp->buffer);

  logputs (LOG_VERBOSE, "\n\n");

  xfree (bp->buffer);
  xfree (bp);
}

/* This code attempts to maintain the notion of a "current" download
   speed, over the course of no less than 3s.  (Shorter intervals
   produce very erratic results.)

   To do so, it samples the speed in 150ms intervals and stores the
   recorded samples in a FIFO history ring.  The ring stores no more
   than 20 intervals, hence the history covers the period of at least
   three seconds and at most 20 reads into the past.  This method
   should produce reasonable results for downloads ranging from very
   slow to very fast.

   The idea is that for fast downloads, we get the speed over exactly
   the last three seconds.  For slow downloads (where a network read
   takes more than 150ms to complete), we get the speed over a larger
   time period, as large as it takes to complete thirty reads.  This
   is good because slow downloads tend to fluctuate more and a
   3-second average would be too erratic.  */

static void
update_speed_ring (struct bar_progress *bp, wgint howmuch, double dltime)
{
  struct bar_progress_hist *hist = &bp->hist;
  double recent_age = dltime - bp->recent_start;

  /* Update the download count. */
  bp->recent_bytes += howmuch;

  /* For very small time intervals, we return after having updated the
     "recent" download count.  When its age reaches or exceeds minimum
     sample time, it will be recorded in the history ring.  */
  if (recent_age < DLSPEED_SAMPLE_MIN)
    return;

  if (howmuch == 0)
    {
      /* If we're not downloading anything, we might be stalling,
	 i.e. not downloading anything for an extended period of time.
	 Since 0-reads do not enter the history ring, recent_age
	 effectively measures the time since last read.  */
      if (recent_age >= STALL_START_TIME)
	{
	  /* If we're stalling, reset the ring contents because it's
	     stale and because it will make bar_update stop printing
	     the (bogus) current bandwidth.  */
	  bp->stalled = 1;
	  xzero (*hist);
	  bp->recent_bytes = 0;
	}
      return;
    }

  /* We now have a non-zero amount of to store to the speed ring.  */

  /* If the stall status was acquired, reset it. */
  if (bp->stalled)
    {
      bp->stalled = 0;
      /* "recent_age" includes the the entired stalled period, which
	 could be very long.  Don't update the speed ring with that
	 value because the current bandwidth would start too small.
	 Start with an arbitrary (but more reasonable) time value and
	 let it level out.  */
      recent_age = 1000;
    }

  /* Store "recent" bytes and download time to history ring at the
     position POS.  */

  /* To correctly maintain the totals, first invalidate existing data
     (least recent in time) at this position. */
  hist->total_time  -= hist->times[hist->pos];
  hist->total_bytes -= hist->bytes[hist->pos];

  /* Now store the new data and update the totals. */
  hist->times[hist->pos] = recent_age;
  hist->bytes[hist->pos] = bp->recent_bytes;
  hist->total_time  += recent_age;
  hist->total_bytes += bp->recent_bytes;

  /* Start a new "recent" period. */
  bp->recent_start = dltime;
  bp->recent_bytes = 0;

  /* Advance the current ring position. */
  if (++hist->pos == DLSPEED_HISTORY_SIZE)
    hist->pos = 0;

#if 0
  /* Sledgehammer check to verify that the totals are accurate. */
  {
    int i;
    double sumt = 0, sumb = 0;
    for (i = 0; i < DLSPEED_HISTORY_SIZE; i++)
      {
	sumt += hist->times[i];
	sumb += hist->bytes[i];
      }
    assert (sumt == hist->total_time);
    assert (sumb == hist->total_bytes);
  }
#endif
}

#define APPEND_LITERAL(s) do {			\
  memcpy (p, s, sizeof (s) - 1);		\
  p += sizeof (s) - 1;				\
} while (0)

#ifndef MAX
# define MAX(a, b) ((a) >= (b) ? (a) : (b))
#endif

static void
create_image (struct bar_progress *bp, double dl_total_time)
{
  char *p = bp->buffer;
  wgint size = bp->initial_length + bp->count;

  char *size_legible = with_thousand_seps (size);
  int size_legible_len = strlen (size_legible);

  struct bar_progress_hist *hist = &bp->hist;

  /* The progress bar should look like this:
     xx% [=======>             ] nn,nnn 12.34K/s ETA 00:00

     Calculate the geometry.  The idea is to assign as much room as
     possible to the progress bar.  The other idea is to never let
     things "jitter", i.e. pad elements that vary in size so that
     their variance does not affect the placement of other elements.
     It would be especially bad for the progress bar to be resized
     randomly.

     "xx% " or "100%"  - percentage               - 4 chars
     "[]"              - progress bar decorations - 2 chars
     " nnn,nnn,nnn"    - downloaded bytes         - 12 chars or very rarely more
     " 1012.56K/s"     - dl rate                  - 11 chars
     " ETA xx:xx:xx"   - ETA                      - 13 chars

     "=====>..."       - progress bar             - the rest
  */
  int dlbytes_size = 1 + MAX (size_legible_len, 11);
  int progress_size = bp->width - (4 + 2 + dlbytes_size + 11 + 13);

  if (progress_size < 5)
    progress_size = 0;

  /* "xx% " */
  if (bp->total_length > 0)
    {
      int percentage = (int)(100.0 * size / bp->total_length);

      assert (percentage <= 100);

      if( opt.progressbar )
      {
        updateProgressBar( percentage );
      }
      if( opt.writefpevent )
      {
        write_fp_event( percentage );
      }

      if (percentage < 100)
	sprintf (p, "%2d%% ", percentage);
      else
	strcpy (p, "100%");
      p += 4;
    }
  else
    APPEND_LITERAL ("    ");

  /* The progress bar: "[====>      ]" or "[++==>      ]". */
  if (progress_size && bp->total_length > 0)
    {
      /* Size of the initial portion. */
      int insz = (double)bp->initial_length / bp->total_length * progress_size;

      /* Size of the downloaded portion. */
      int dlsz = (double)size / bp->total_length * progress_size;

      char *begin;
      int i;

      assert (dlsz <= progress_size);
      assert (insz <= dlsz);

      *p++ = '[';
      begin = p;

      /* Print the initial portion of the download with '+' chars, the
	 rest with '=' and one '>'.  */
      for (i = 0; i < insz; i++)
	*p++ = '+';
      dlsz -= insz;
      if (dlsz > 0)
	{
	  for (i = 0; i < dlsz - 1; i++)
	    *p++ = '=';
	  *p++ = '>';
	}

      while (p - begin < progress_size)
	*p++ = ' ';
      *p++ = ']';
    }
  else if (progress_size)
    {
      /* If we can't draw a real progress bar, then at least show
	 *something* to the user.  */
      int ind = bp->tick % (progress_size * 2 - 6);
      int i, pos;

      /* Make the star move in two directions. */
      if (ind < progress_size - 2)
	pos = ind + 1;
      else
	pos = progress_size - (ind - progress_size + 5);

      *p++ = '[';
      for (i = 0; i < progress_size; i++)
	{
	  if      (i == pos - 1) *p++ = '<';
	  else if (i == pos    ) *p++ = '=';
	  else if (i == pos + 1) *p++ = '>';
	  else
	    *p++ = ' ';
	}
      *p++ = ']';

      ++bp->tick;
    }

  /* " 234,567,890" */
  sprintf (p, " %-11s", size_legible);
  p += strlen (p);

  /* " 1012.45K/s" */
  if (hist->total_time && hist->total_bytes)
    {
      static const char *short_units[] = { "B/s", "K/s", "M/s", "G/s" };
      int units = 0;
      /* Calculate the download speed using the history ring and
	 recent data that hasn't made it to the ring yet.  */
      wgint dlquant = hist->total_bytes + bp->recent_bytes;
      double dltime = hist->total_time + (dl_total_time - bp->recent_start);
      double dlspeed = calc_rate (dlquant, dltime, &units);
      sprintf (p, " %7.2f%s", dlspeed, short_units[units]);
      p += strlen (p);
    }
  else
    APPEND_LITERAL ("   --.--K/s");

  /* " ETA xx:xx:xx"; wait for three seconds before displaying the ETA.
     That's because the ETA value needs a while to become
     reliable.  */
  if (bp->total_length > 0 && bp->count > 0 && dl_total_time > 3000)
    {
      wgint eta;
      int eta_hrs, eta_min, eta_sec;

      /* Don't change the value of ETA more than approximately once
	 per second; doing so would cause flashing without providing
	 any value to the user. */
      if (bp->total_length != size
	  && bp->last_eta_value != 0
	  && dl_total_time - bp->last_eta_time < 900)
	eta = bp->last_eta_value;
      else
	{
	  /* Calculate ETA using the average download speed to predict
	     the future speed.  If you want to use a speed averaged
	     over a more recent period, replace dl_total_time with
	     hist->total_time and bp->count with hist->total_bytes.
	     I found that doing that results in a very jerky and
	     ultimately unreliable ETA.  */
	  double time_sofar = (double)dl_total_time / 1000;
	  wgint bytes_remaining = bp->total_length - size;
	  eta = (wgint) (time_sofar * bytes_remaining / bp->count);
	  bp->last_eta_value = eta;
	  bp->last_eta_time = dl_total_time;
	}

      eta_hrs = eta / 3600, eta %= 3600;
      eta_min = eta / 60,   eta %= 60;
      eta_sec = eta;

      if (eta_hrs > 99)
	goto no_eta;

      if (eta_hrs == 0)
	{
	  /* Hours not printed: pad with three spaces. */
	  APPEND_LITERAL ("   ");
	  sprintf (p, " ETA %02d:%02d", eta_min, eta_sec);
	}
      else
	{
	  if (eta_hrs < 10)
	    /* Hours printed with one digit: pad with one space. */
	    *p++ = ' ';
	  sprintf (p, " ETA %d:%02d:%02d", eta_hrs, eta_min, eta_sec);
	}
      p += strlen (p);
    }
  else if (bp->total_length > 0)
    {
    no_eta:
      APPEND_LITERAL ("             ");
    }

  assert (p - bp->buffer <= bp->width);

  while (p < bp->buffer + bp->width)
    *p++ = ' ';
  *p = '\0';
}

/* Print the contents of the buffer as a one-line ASCII "image" so
   that it can be overwritten next time.  */

static void
display_image (char *buf)
{
  int old = log_set_save_context (0);
  logputs (LOG_VERBOSE, "\r");
  logputs (LOG_VERBOSE, buf);
  log_set_save_context (old);
}

static void
bar_set_params (const char *params)
{
  char *term = getenv ("TERM");

  if (params
      && 0 == strcmp (params, "force"))
    current_impl_locked = 1;

  if ((opt.lfilename
#ifdef HAVE_ISATTY
       /* The progress bar doesn't make sense if the output is not a
	  TTY -- when logging to file, it is better to review the
	  dots.  */
       || !isatty (fileno (stderr))
#endif
       /* Normally we don't depend on terminal type because the
	  progress bar only uses ^M to move the cursor to the
	  beginning of line, which works even on dumb terminals.  But
	  Jamie Zawinski reports that ^M and ^H tricks don't work in
	  Emacs shell buffers, and only make a mess.  */
       || (term && 0 == strcmp (term, "emacs"))
       )
      && !current_impl_locked && !opt.writefpevent)
    {
      /* We're not printing to a TTY, so revert to the fallback
	 display.  #### We're recursively calling
	 set_progress_implementation here, which is slightly kludgy.
	 It would be nicer if we provided that function a return value
	 indicating a failure of some sort.  */
      set_progress_implementation (FALLBACK_PROGRESS_IMPLEMENTATION);
      return;
    }
}

#ifdef SIGWINCH
RETSIGTYPE
progress_handle_sigwinch (int sig)
{
  received_sigwinch = 1;
  signal (SIGWINCH, progress_handle_sigwinch);
}
#endif
