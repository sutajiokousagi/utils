/* $Id$
  chumby_fbctl.cpp - simple utility to allow fb blank / unblank and other
  VESA IOCTL operations

 */


#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/fb.h> // Defines FBIOGET_VSCREENINFO, fb_var_screeninfo, etc.
#include <string.h>
#include <unistd.h>

#define VER_STR "0.16"

#define WRITE_REG 1
#define READ_REG 2
//#define O_RDWR  2
#define DEBUG 1

/* define some IOCTL values */


#define FB_VMODE_YUV422PACKED               0x0
#define FB_VMODE_RGB565			0x100


#define FBIOPUT_VIDEO_ALPHABLEND	0xEB  /* video layer controls alpha blend */
#define FBIOPUT_GLOBAL_ALPHABLEND	0xE1
#define FBIOPUT_GRAPHIC_ALPHABLEND	0xE2

#define FB_IOC_MAGIC                        'm'
#define FB_IOCTL_CONFIG_CURSOR              _IO(FB_IOC_MAGIC, 0)
#define FB_IOCTL_DUMP_REGS                  _IO(FB_IOC_MAGIC, 1)
#define FB_IOCTL_CLEAR_IRQ                  _IO(FB_IOC_MAGIC, 2)

/*
 * There are many video mode supported.
 */
#define FB_IOCTL_SET_VIDEO_MODE             _IO(FB_IOC_MAGIC, 3)
#define FB_IOCTL_GET_VIDEO_MODE             _IO(FB_IOC_MAGIC, 4)

/* Request a new video buffer from driver. User program needs to free
 * this memory.
 */
#define FB_IOCTL_CREATE_VID_BUFFER          _IO(FB_IOC_MAGIC, 5)

/* Configure viewport in driver. */
#define FB_IOCTL_SET_VIEWPORT_INFO          _IO(FB_IOC_MAGIC, 6)
#define FB_IOCTL_GET_VIEWPORT_INFO          _IO(FB_IOC_MAGIC, 7)

/* Flip the video buffer from user mode. Vide buffer can be separated into:
 * a. Current-used buffer - user program put any data into it. It will be
 *    displayed immediately.
 * b. Requested from driver but not current-used - user programe can put any
 *    data into it. It will be displayed after calling
 *    FB_IOCTL_FLIP_VID_BUFFER.
 *    User program should free this memory when they don't use it any more.
 * c. User program alloated - user program can allocated a contiguos DMA
 *    buffer to store its video data. And flip it to driver. Notices that
 *    this momory should be free by user programs. Driver won't take care of
 *    this.
 */
#define FB_IOCTL_FLIP_VID_BUFFER            _IO(FB_IOC_MAGIC, 8)

/* Get the current buffer information. User program could use it to display
 * anything directly. If developer wants to allocate multiple video layers,
 * try to use FB_IOCTL_CREATE_VID_BUFFER  to request a brand new video
 * buffer.
 */
#define FB_IOCTL_GET_BUFF_ADDR              _IO(FB_IOC_MAGIC, 9)

/* Get/Set offset position of screen */
#define FB_IOCTL_SET_VID_OFFSET             _IO(FB_IOC_MAGIC, 10)
#define FB_IOCTL_GET_VID_OFFSET             _IO(FB_IOC_MAGIC, 11)

/* Turn on the memory toggle function to improve the frame rate while playing
 * movie.
 */
#define FB_IOCTL_SET_MEMORY_TOGGLE          _IO(FB_IOC_MAGIC, 12)

#define FB_IOCTL_SET_COLORKEYnALPHA         _IO(FB_IOC_MAGIC, 13)
#define FB_IOCTL_GET_COLORKEYnALPHA         _IO(FB_IOC_MAGIC, 14)
#define FB_IOCTL_SWITCH_GRA_OVLY            _IO(FB_IOC_MAGIC, 15)
#define FB_IOCTL_SWITCH_VID_OVLY            _IO(FB_IOC_MAGIC, 16)

/* For VPro integration */
#define FB_IOCTL_GET_FREELIST               _IO(FB_IOC_MAGIC, 17)

/* Wait for vsync happen. */
#define FB_IOCTL_WAIT_VSYNC                 _IO(FB_IOC_MAGIC, 18)

struct _sViewPortInfo {
        unsigned short srcWidth;        /* video source size */
        unsigned short srcHeight;
        unsigned short zoomXSize;       /* size after zooming */
        unsigned short zoomYSize;
        unsigned short ycPitch;
        unsigned short uvPitch;
};

struct _sViewPortOffset {
        unsigned short xOffset;         /* position on screen */
        unsigned short yOffset;
};

struct pxa910_fb_chroma {
        u_char     mode;
        u_char     y_alpha;
        u_char     y;
        u_char     y1;
        u_char     y2;
        u_char     u_alpha;
        u_char     u;
        u_char     u1;
        u_char     u2;
        u_char     v_alpha;
        u_char     v;
        u_char     v1;
        u_char     v2;
};

int def_env( int defaultval, const char *env )
{
	const char *env_value = getenv( env );
	if (env_value == NULL)
	{
		printf( "Error: could not get %s from environment, defaulting to %d\n", defaultval );
		return defaultval;
	}
	return atoi( env_value );
}

int main(int argc, char *argv[])
{
        struct fb_var_screeninfo fbdata;
        struct pxa910_fb_chroma chroma;
        int fd_graphic, fd_video;
        unsigned int temp;
        unsigned int colorspace;
        bool setGlobalAlpha = false;
        bool setChroma = false;
        bool yuv422p = false;
        int graphic_enable = 1;
        int video_enable = 1;
        int video_fbnum = 0;
	int blank_op = -1;
	int fbactive_op = -1;
	int alpha_op = -1;
	int mode_op = -1;

		printf( "chumby_fbctl v" VER_STR "\n" );

		// Check for args
		int n;
		for (n = 1; n < argc; n++)
		{
			if (argv[n][0] == '-')
				switch (argv[n][1])
				{
					case 'b':
						n++;
						blank_op = atoi( argv[n] );
						break;
					case 'f':
						n++;
						fbactive_op = atoi( argv[n] );
						break;
					case 'a':
						n++;
						alpha_op = atoi( argv[n] );
						break;
					case 'm':
						n++;
						mode_op = atoi( argv[n] );
						break;

					case 'h':
					default:
						if (argv[n][1] != 'h')
						{
							printf( "%s - warning - ignoring unrecognized option '%c'\n", argv[0], argv[n][1] );
						}
						printf( "\
-h display this message and exit\n\
-b <n> blank (n=1) or unblank (n=0)\n\
-f <n> make fb<b> visible\n\
-m <n> set mode <n> (normal=0)\n\
-a <n> set alpha for fb1 to <n>\n\
" );
						return -1;
						break;
				}
			else
			{
				printf( "%s - warning - ignoring unrecognized option %s\n", argv[0], argv[n] );
			}
		}

		char video_fbname[128];
		sprintf( video_fbname, "/dev/fb%d", video_fbnum );
        fd_video = open(video_fbname , O_RDWR);
        if( fd_video < 0) {
                printf( "Failed to open video (%s) errno=%d (%s)\n", video_fbname, errno, strerror(errno) );
                exit(1);
        }
        printf( "%s handle = %d\n", video_fbname, fd_video );

	if (blank_op >= 0)
	{

		// Just try to unblank and exit
		if (ioctl( fd_video, FBIOBLANK, blank_op ) != 0)
		{
			printf( "unblank failed, errno=%d (%s)\n", errno, strerror(errno) );
		}

		close( fd_video );
		return 0;

	}

	if (fbactive_op >= 0 || alpha_op >= 0)
	{
		if (alpha_op < 0)
		{
			alpha_op = (fbactive_op == 0) ? 0 : 255;
		}

		if (ioctl(fd_video, FBIOPUT_GRAPHIC_ALPHABLEND, (255-alpha_op)) != 0)
		{
			printf( "Failed to set GRAPHIC_ALPHABLEND, errno=%d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}
		printf( "Set global alpha to %d\n", alpha_op );
	}

	if (mode_op >= 0)
	{
		// Try to reset a sane display mode
		sprintf( video_fbname, "/dev/fb%d", (video_fbnum + 1) % 2 );
        int fd_graphic = open(video_fbname , O_RDWR);
        if( fd_graphic < 0) {
                printf( "Failed to open graphic (%s) errno=%d (%s)\n", video_fbname, errno, strerror(errno) );
                exit(1);
        }

		struct fb_var_screeninfo fbdata;

		int fb_mode = -1;
		if (ioctl( fd_graphic, FB_IOCTL_GET_VIDEO_MODE, &fb_mode ) != 0)
		{
			printf( "FB_IOCTL_GET_VIDEO_MODE failed errno = %d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}
		printf( "Current video mode = %x\n", fb_mode );
		fb_mode = FB_VMODE_RGB565;
		if (ioctl( fd_graphic, FB_IOCTL_SET_VIDEO_MODE, &fb_mode ) != 0)
		{
			printf( "FB_IOCTL_SET_VIDEO_MODE failed errno = %d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}

		int g_xres = def_env( 800, "VIDEO_X_RES");
		int g_yres = def_env( 600, "VIDEO_Y_RES");

		printf( "Using default values %dx%d\n", g_xres, g_yres );

		struct _sViewPortOffset gViewPortOffset ;
		// = {
        //.xOffset = 0,   /* position on screen */
        //.yOffset = 0
		//};
		if (ioctl( fd_graphic, FB_IOCTL_GET_VID_OFFSET, &gViewPortOffset ) != 0)
		{
			printf( "FB_IOCTL_GET_VID_OFFSET failed errno=%d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}
		printf( "current offset={%d,%d}\n", gViewPortOffset.xOffset, gViewPortOffset.yOffset );
		gViewPortOffset.xOffset = 0;
		gViewPortOffset.yOffset = 0;
		if (ioctl( fd_graphic, FB_IOCTL_SET_VID_OFFSET, &gViewPortOffset ) != 0)
		{
			printf( "FB_IOCTL_SET_VID_OFFSET failed errno=%d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}

		struct _sViewPortInfo gViewPortInfo;
		// = {
        //.srcWidth = DEFAULT_WIDTH,      /* video source size */
        //.srcHeight = DEFAULT_HEIGHT,
        //.zoomXSize = DEFAULT_WIDTH,     /* size after zooming */
        //.zoomYSize = DEFAULT_HEIGHT,
		//};
		if (ioctl( fd_graphic, FB_IOCTL_GET_VIEWPORT_INFO, &gViewPortInfo ) != 0)
		{
			printf( "FB_IOCTL_GET_VIEWPORT_INFO failed errno=%d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}
		printf( "Current src={%d,%d}, zoom={%d,%d} ycPitch=%d uvPitch=%d\n",
			gViewPortInfo.srcWidth, gViewPortInfo.srcHeight, gViewPortInfo.zoomXSize, gViewPortInfo.zoomYSize,
			gViewPortInfo.ycPitch, gViewPortInfo.uvPitch );
		gViewPortInfo.srcWidth = g_xres;
		gViewPortInfo.srcHeight = g_yres;
		gViewPortInfo.zoomXSize = g_xres;
		gViewPortInfo.zoomYSize = g_yres;
		if (ioctl( fd_graphic, FB_IOCTL_SET_VIEWPORT_INFO, &gViewPortInfo ) != 0)
		{
			printf( "FB_IOCTL_SET_VIEWPORT_INFO failed errno=%d (%s)\n", errno, strerror(errno) );
			exit( 1 );
		}


		if (ioctl(fd_graphic, FBIOGET_VSCREENINFO, &fbdata)!=0) {
			printf("FBIOGET_VSCREENINFO ioctl error res = %d.\n", errno);
            exit(1);
		}

		printf("current x and y res is: %d %d \n", fbdata.xres, fbdata.yres);
		printf("current bits per pixel: %d \n", fbdata.bits_per_pixel);
		printf("current width and height defined as : %d %d \n", fbdata.width, fbdata.height);

		// To reset colospace, we need to specify the individual colors...
		fbdata.xres = g_xres; //800; //g_lcd_width;
		fbdata.yres = g_yres; //600; //g_lcd_height;
		fbdata.width = g_xres; //800; //g_lcd_width;
		fbdata.height = g_yres; //600; //g_lcd_height;

		if (ioctl(fd_graphic, FBIOPUT_VSCREENINFO, &fbdata)!=0) {
				printf("FBIOPUT_VSCREENINFO ioctl error res = %d.\n", errno);
				exit(1);
		}


        close( fd_graphic );
	}

		printf( "Exiting...\n" );

		close( fd_video );

		return 0;
}

