// $Id$

#define _FBTEXT_H_OWNER
#include "fbtext.h"

static FILE *fb_file;
unsigned short *fb;

static int cur_x = 0,cur_y = 0, start_x = 0, start_y = 0;
static unsigned short *fb_background;
static unsigned short fb_color = 0; // font color

void fbtext_gotoxc( short x ) { cur_x = start_x = x * FONT_WIDTH; }
void fbtext_gotoyc( short y ) { cur_y = start_y = y * FONT_HEIGHT; }
void fbtext_gotoxyc( short x, short y ) { fbtext_gotoxc( x ); fbtext_gotoyc( y ); }

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
		//memset(fb,0,g_width*g_height*BYTES_PER_PIXEL);
		memcpy(fb,fb_background,g_width*g_height*BYTES_PER_PIXEL);
		cur_x = start_x;
		cur_y = start_y;
	}
}

void fbtext_scroll(void)
{
	short unsigned int *dst = fb;
	short unsigned int *src = &fb[FONT_HEIGHT*g_width];
	int offset;
	memmove(dst,src,ROWS*PIXELS_PER_ROW*BYTES_PER_PIXEL);
	offset = (ROWS*PIXELS_PER_ROW);
	memset(&fb[offset],0,((g_width*g_height)-offset)*BYTES_PER_PIXEL);
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
	char *PIXEL_SHIFT;
#endif
	if (!fb_file)
    {
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
			//	fb_fix.smem_start, fb, cur_x, cur_y);
		}
#else
		fb = (unsigned short *)mmap(0,
			g_width*g_height*BYTES_PER_PIXEL,
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
		fb_background = (unsigned short *)malloc(g_width*g_height*BYTES_PER_PIXEL);
		memcpy(fb_background,fb,g_width*g_height*BYTES_PER_PIXEL);
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
				int y;
				for (y=0;y<FONT_HEIGHT;y++)
                {
					unsigned long offset = (cur_y+y)*g_width+cur_x;
					if (offset+FONT_WIDTH<g_width*g_height)
                    {
						unsigned short *line = &fb[offset];
						unsigned short *bg = &fb_background[offset];
						int x;
						unsigned short b = *cp++;
						//if (y == 0) fprintf( stderr, "Sending out %c at %d,%d offset %lu\n", c, cur_x, cur_y, offset );
						if (FONT_WIDTH>8)
                        {
							b = (b<<8)+*cp++;
                        }
						for (x=0;x<FONT_WIDTH;x++)
                        {
                            // hacked to skip first column
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
					}
					else
					{
						static int spew = 0;
						if (spew++ == 0)
						{
							fprintf( stderr, "Ignoring output for %c - offset %lu would exceed fb size%lu\n",
								c, offset, (unsigned long)g_width * g_height );
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
}

//
// put text on the display
//
void fbtext_puts(char *s)
{
	char outchar;
	//fprintf( stderr, "sending %s @(%d,%d)\n", s, cur_x, cur_y );
	while (*s)
	{

		outchar = *s;
		//check for escaped chars from command-line
		if (*s == '\\')
		{
			if (*(s+1) == 'n')
			{
				outchar = '\n';
				s += 2;
			}
			else if (*(s+1) == 't')
			{
				outchar = '\t';
				s += 2;
			}
			else if (*(s+1) == 'r')
			{
				outchar = '\r';
				s += 2;
			}
			else
			{
				s++;
			}
		}
		else
		{
			s++;
		}

		fbtext_putc(outchar);
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
	unsigned short color16 = ((r&0xf8)<<8) + ((g&0xfc)<<3) + ((b&0xf8)>>3);
	// Sanity check the values
	if (top>=bottom || left>=right)
	{
		return;
	}
	assure_fb();
	if (right>g_width)
    {
        right = g_width;
    }
	if (bottom>g_height)
    {
        bottom = g_height;
    }
	for (y=top;y<bottom;y++)
    {
		unsigned short *line = &fb[y*g_width+left];
		unsigned width = right-left;
		while (width--)
			*line++ = color16;
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
    {
        right = g_width;
    }
	if (bottom>g_height)
    {
        bottom = g_height;
    }
	for (y=top;y<bottom;y++)
    {
		unsigned long offset = y*g_width+left;
		unsigned short *line = &fb[offset];
		unsigned short *bg = &fb_background[offset];
		unsigned width = right-left;
		while (width--)
        {
			*line++ = *bg++;
        }
	}
}

