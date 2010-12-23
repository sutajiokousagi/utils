/*
    Copyright (C) 2004 Samsung Electronics

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#include <linux/videodev2.h>
#include "videodev2_s3c.h"

/*------------ FIXED configuration ----------------------*/
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define YUV420			420
#define YUV422			422

/*------------ CIS configuration ----------------------*/
#define SAMSUNG_UXGA_S5K3BA

/*------------ PREVIEW configuration ----------------------*/
int g_lcd_width = 800;
int g_lcd_height = 480;
#define LCD_WIDTH 		g_lcd_width			/* use 240 if 2442 */
#define LCD_HEIGHT 		g_lcd_height		/* use 320 if 2442 */
#define LCD_BPP			16			/* use 32 for 24/32 bit lcd */
#define SRC_BPP			16			/* use 32 for 24/32 bit src */
#define V4L2_PREVIEW_PIX_FMT	V4L2_PIX_FMT_RGB565	/* use V4L2_PIX_FMT_RGB24 for 24 bit src */

int g_frame_buffer_size = 0;

void calc_frame_buffer()
{
	g_frame_buffer_size = g_lcd_width * g_lcd_height * SRC_BPP / 8;
}
#define FRAME_BUFFER_SIZE	g_frame_buffer_size

/*------------ CODEC configuration ----------------------*/
#define CODEC_WIDTH		640
#define CODEC_HEIGHT	480
#define YUV_FMT			YUV420

#if (YUV_FMT == YUV420)
#define V4L2_CODEC_PIX_FMT	V4L2_PIX_FMT_YUV420
#define YUV_PCT_SIZE		((CODEC_WIDTH * CODEC_HEIGHT) + ((CODEC_WIDTH * CODEC_HEIGHT) / 2))
#elif (YUV_FMT == YUV422)
#define V4L2_CODEC_PIX_FMT	V4L2_PIX_FMT_YUYV
#define YUV_PCT_SIZE		(CODEC_WIDTH * CODEC_HEIGHT * 2)
#else
#error you must define YUV_FMT (420/422) here.
#endif

/*------------ NODE configuration ----------------------*/
//#define PREVIEW_NODE 		"/dev/video/preview"
//#define CODEC_NODE		"/dev/video/codec"
//#define FB_NODE   		"/dev/fb1"		/* Use fb1 if 2.6.16 or 2443 */
#define CODEC_NODE			"/dev/video0"
#define PREVIEW_NODE 		"/dev/video1"
#define FB_NODE   		"/dev/fb0"		/* Use fb1 if 2.6.16 or 2443 */

/*------------ OPERATION MODE configuration ----------------------*/
#define MMAP_ON_PREVIEW
#define MMAP_ON_CODEC
#undef	PREVIEW_USE_POLL
#define	CODEC_USE_POLL
#define FB_SET_OSD_INFO		/* Turn off if 2442 */

#define PP_NUM			4
#undef USE_LAST_IRQ		/* sync with driver */

/*------------ GLOBAL configuration ----------------------*/
static int cam_p_fp = -1;	/* preview file pointer */
static int cam_c_fp = -1;	/* codec file pointer */

static unsigned char *g_yuv;	/* yuv data for save */

static char *fb_addr = NULL;	/* framebuffer address */
static int fb_fp = -1;		/* framebuffer file pointer */

static pthread_t pth = 0;	/* pthread id */
static int key_flag = 0;	/* key flag */
static int key = 0;		/* key value */

static int bright_level = 0;	/* bright level */

struct v4l2_control ctrl;	/* v4l2 control structure */
static int frame_count = 0;	/* for multi-frame capture test */

/*------------ V4L2 STREAMING I/O configuration ----------------------*/
struct buffer {
	void * start;
	size_t length;
};

struct buffer buffers_p[4];
struct buffer buffers_c[4];

/*------------ WINDOW(OSD) configuration ----------------------*/
typedef struct {
        int Bpp;
        int LeftTop_x;
        int LeftTop_y;
        int Width;
        int Height;
}  s3c_win_info_t;

static s3c_win_info_t fb_info_to_driver;

#define GET_DISPLAY_BRIGHTNESS		_IOR('F', 1, u_int)
#define SET_DISPLAY_BRIGHTNESS		_IOW('F', 2, u_int)

#define SET_OSD_START			_IO('F', 201)
#define SET_OSD_STOP			_IO('F', 202)
#define SET_OSD_ALPHA_UP		_IO('F', 203)
#define SET_OSD_ALPHA_DOWN		_IO('F', 204)
#define SET_OSD_MOVE_LEFT		_IO('F', 205)
#define SET_OSD_MOVE_RIGHT		_IO('F', 206)
#define SET_OSD_MOVE_UP			_IO('F', 207)
#define SET_OSD_MOVE_DOWN		_IO('F', 208)
#define SET_OSD_INFO			_IOW('F', 209, s3c_win_info_t)

/*------------------------ FUNCTIONS ------------------------*/
static void exit_from_app()
{
   	/* Stop previewing camera */
	enum v4l2_buf_type type;
	int start = 0;

	int ret = ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);

	if (ret < 0) {
    		printf("[VIDIOC_OVERLAY] failed\n");
    		exit(1);
  	}

	if (g_yuv)
		free(g_yuv);

	if (cam_p_fp)
		close(cam_p_fp);

	if (cam_c_fp)
		close(cam_c_fp);

	if (fb_fp)
		munmap(fb_addr, FRAME_BUFFER_SIZE);

	if (fb_fp)
		close(fb_fp);

	exit(0);
}

static inline int read_data(int fp, unsigned char *buf, int width, int height, int bpp, int f_index)
{
	int ret = 0;

#if defined(MMAP_ON_PREVIEW)
	memcpy(buf, buffers_p[f_index].start, FRAME_BUFFER_SIZE);
	ret = 1;
#else
	if (bpp == 16) {
		if ((ret = read(fp, buf, width * height * 2)) != width * height * 2)
			ret = 1;
	} else {
		if ((ret = read(fp, buf, width * height * 4)) != width * height * 4)
			ret = 1;
	}
#endif

	return ret;
}

static inline void save_yuv(int width, int height, int fmt, int f_index)
{
	FILE *yuv_fp = NULL;
	char file_name[100];

	sprintf(file_name, "YUV%d_%dx%d_%d.yuv", fmt, width, height, frame_count);
	printf("Filename: %s\n", file_name);

#if defined(MMAP_ON_CODEC)
	memcpy(g_yuv, buffers_c[f_index].start, YUV_PCT_SIZE);
#else
	if (read(cam_c_fp, g_yuv, YUV_PCT_SIZE) < 0)
		perror("read() error\n");
#endif

	fflush(stdout);

	/* file create/open, note to "wb" */
	yuv_fp = fopen(&file_name[0], "wb");

	if (!yuv_fp)
		perror(&file_name[0]);

	printf("Addr. of g_yuv: 0x%08x\n", g_yuv);
	fwrite(g_yuv, 1, YUV_PCT_SIZE, yuv_fp);
	fclose(yuv_fp);
}

void fmalloc(int size)
{
	g_yuv = (unsigned char *) malloc(size);

	if (!g_yuv) {
		printf("Memory allcation failed\n");
		exit(1);
	}
}

static void sig_del(int signo, int width, int height, int bpp)
{
	exit_from_app();
}

int signal_ctrl_c(void)
{
	if (signal(SIGINT, sig_del) == SIG_ERR)
		printf("Signal error\n");
}

static inline int cam_p_init(const char *preview_node)
{
	int dev_fp = -1;

	dev_fp = open(preview_node, O_RDWR);

	if (dev_fp < 0) {
		perror(preview_node);
		return -1;
	}

	return dev_fp;
}

static inline int cam_c_init(const char *codec_node)
{
	int dev_fp = -1;

	dev_fp = open(codec_node, O_RDWR);

	if (dev_fp < 0) {
		perror(codec_node);
		return -1;
	}

	return dev_fp;
}

static int osd_init(int fd, int bpp, int width, int height)
{
	fb_info_to_driver.Bpp = bpp;
	fb_info_to_driver.LeftTop_x = 0;
	fb_info_to_driver.LeftTop_y = 0;
	fb_info_to_driver.Width = width;
	fb_info_to_driver.Height = height;

	if(ioctl(fd, SET_OSD_INFO, &fb_info_to_driver)){
                printf("Some problem with SET_VS_INFO\n");
                return -1;
        }

  	if(ioctl(fd, SET_OSD_START)){
           	printf("Some problem with SET_OSD_START\n");
		perror("ioctl()");
		return -1;
        }
}

static inline int fb_init(int bpp, int width, int height)
{
	int dev_fp = -1;

	dev_fp = open(FB_NODE, O_RDWR);

	if (dev_fp < 0)
		return -1;

	printf("fb: %s\n", FB_NODE);

	if ((fb_addr = (char *) mmap(0, FRAME_BUFFER_SIZE,
				PROT_READ | PROT_WRITE, MAP_SHARED, dev_fp, 0)) < 0) {
		perror("mmap() error in fb_init()");
		return -1;
	}

#if defined(FB_SET_OSD_INFO)
	osd_init(dev_fp, SRC_BPP, LCD_WIDTH, LCD_HEIGHT);
#endif
	printf("Addr. of fb_addr = 0x%8X  \n", fb_addr);

	return dev_fp;
}

static inline void draw(char *dest, unsigned char *src, int width, int height, int bpp)
{
	int x, y, file_size;
	unsigned long *rgb32;
	unsigned long *rgb32_dst;
	unsigned short *rgb16;

	int end_y = height;
	int end_x = MIN(LCD_WIDTH, width);

	if (bpp == 16) {
		if (LCD_BPP == 16) {
			for (y = 0; y < end_y; y++)
				memcpy(dest + y * LCD_WIDTH * 2, src + y * width * 2, end_x * 2);
		} else {
			for (y = 0; y < end_y; y++) {
				rgb16 = (unsigned short *) (src + (y * width * 2));
				rgb32 = (unsigned long *) (dest + (y * LCD_WIDTH * 2));

				// TO DO : 16 bit RGB data -> 24 bit RGB data
				for (x = 0; x < end_x; x++) {
					*rgb32 = ((*rgb16) & 0xF800) << 16 | ((*rgb16) & 0x07E0) << 8 |
					    (*rgb16) & 0x001F;
					rgb32++;
					rgb16++;
				}
			}
		}
	} else if (bpp == 24 || bpp == 32) {
		if (LCD_BPP == 16) {
			for (y = 0; y < end_y; y++) {
				rgb32 = (unsigned long *) (src + (y * width * 4));
				rgb16 = (unsigned short *) (dest + (y * LCD_WIDTH * 2));

				// 24 bit RGB data -> 16 bit RGB data
				for (x = 0; x < end_x; x++) {
					*rgb16 = (*rgb32 >> 8) & 0xF800 | (*rgb32 >> 5) & 0x07E0 |
					    (*rgb32 >> 3) & 0x001F;
					rgb32++;
					rgb16++;
				}
			}
		} else {
			for (y = 0; y < end_y; y++)
				memcpy(dest + y * LCD_WIDTH * 4, src + y * width * 4, end_x * 4);
		}
	}
}

static void get_key(void)
{
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	int ret;

	while (1) {
		if (key_flag == 0) {
			key = getchar();

			if (key == 'p') {
				switch (getchar()) {
				case '0': /* no cropping */
					printf("[Cropping] No cropping, ");
					crop.c.left = 0;
					crop.c.top = 0;
					crop.c.width = 640;
					crop.c.height = 480;

					ret = ioctl(cam_p_fp, VIDIOC_S_CROP, &crop);
					if (ret < 0) {
			    			fprintf(stderr, "[VIDIOC_S_CROP] VIDIOC_S_CROP failed\n");
			    			exit_from_app();
				  	}
					break;

				case '1': /* 352 x 272 */
					printf("[Cropping] 352 x 272 cropping, ");
					crop.c.left = 144;
					crop.c.top = 104;
					crop.c.width = 352;
					crop.c.height = 272;

					ret = ioctl(cam_p_fp, VIDIOC_S_CROP, &crop);
					if (ret < 0) {
			    			fprintf(stderr, "[VIDIOC_S_CROP] VIDIOC_S_CROP failed\n");
			    			exit_from_app();
				  	}
					break;

				case '2': /* 128 x 96 */
					printf("[Cropping] 128 x 96 cropping, ");
					crop.c.left = 256;
					crop.c.top = 192;
					crop.c.width = 128;
					crop.c.height = 96;

					ret = ioctl(cam_p_fp, VIDIOC_S_CROP, &crop);
					if (ret < 0) {
			    			fprintf(stderr, "[VIDIOC_S_CROP] VIDIOC_S_CROP failed\n");
			    			exit_from_app();
				  	}
					break;

				case '9': /* default cropping */
					printf("[Cropping] Default cropping, ");
					cropcap.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
					ret = ioctl(cam_p_fp, VIDIOC_CROPCAP, &cropcap);
					if (ret < 0) {
						fprintf(stderr, "[VIDIOC_CROPCAP] failled\n");
				    		exit_from_app();
				  	}

					printf("[VIDIOC_CROPCAP] bounds:  offset = (%d, %d), size = %dx%d\n", \
					cropcap.bounds.left, cropcap.bounds.top, cropcap.bounds.width, cropcap.bounds.height);
					printf("[VIDIOC_CROPCAP] defrect: offset = (%d, %d), size = %dx%d\n", \
					cropcap.defrect.left, cropcap.defrect.top, cropcap.defrect.width, cropcap.defrect.height);

				  	crop.c.left = cropcap.defrect.left;
					crop.c.top = cropcap.defrect.top;
					crop.c.width = cropcap.defrect.width;
					crop.c.height = cropcap.defrect.height;

					ret = ioctl(cam_p_fp, VIDIOC_S_CROP, &crop);
					if (ret < 0) {
			    			fprintf(stderr, "[VIDIOC_S_CROP] VIDIOC_S_CROP failed\n");
			    			exit_from_app();
				  	}
					break;

				default:
					break;
				}
			}

			if (key == 'w') {
				switch (getchar()) {
				case '0':
				default:
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 0;
					printf("[White balance] Auto mode selected\n");
					break;
				case '1':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 1;
					printf("[White balance] Indoor-3100 mode selected\n");
					break;
				case '2':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 2;
					printf("[White balance] Outdoor-5100 mode selected\n");
					break;
				case '3':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 3;
					printf("[White balance] Indoor-2000 mode selected\n");
					break;
				case '4':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 4;
					printf("[White balance] Halt mode selected\n");
					break;
				case '5':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 5;
					printf("[White balance] Cloudy mode selected\n");
					break;
				case '6':
					ctrl.id = V4L2_CID_AUTO_WHITE_BALANCE;
					ctrl.value = 6;
					printf("[White balance] Sunny mode selected\n");
					break;
				}
			}

			if (key == 'e') {
				switch (getchar()) {
				case '0':
				default:
					ctrl.id = V4L2_CID_ORIGINAL;
					ctrl.value = 0;
					printf("[Image effect] Bypass mode selected\n");
					break;
				case '1':
					ctrl.id = V4L2_CID_ARBITRARY;
					ctrl.value = 1;
					printf("[Image effect] Arbitrary mode selected\n");
					break;
				case '2':
					ctrl.id = V4L2_CID_NEGATIVE;
					ctrl.value = 2;
					printf("[Image effect] Negative mode selected\n");
					break;
				case '3':
					ctrl.id = V4L2_CID_ART_FREEZE;
					ctrl.value = 3;
					printf("[Image effect] Art Freeze mode selected\n");
					break;
				case '4':
					ctrl.id = V4L2_CID_EMBOSSING;
					ctrl.value = 4;
					printf("[Image effect] Embossing mode selected\n");
					break;
				case '5':
					ctrl.id = V4L2_CID_SILHOUETTE;
					ctrl.value = 5;
					printf("[Image effect] Silhouette mode selected\n");
					break;
				}
			}

			if (key == 'r') {
				switch (getchar()) {
				case '0':
				default:
					ctrl.id = V4L2_CID_ROTATE_BYPASS;
					ctrl.value = 0;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_WIDTH, LCD_HEIGHT);
#endif
					printf("[Image rotate] Bypass mode selected\n");
					break;
				case '1':
					ctrl.id = V4L2_CID_ROTATE_90;
					ctrl.value = 1;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_HEIGHT, LCD_WIDTH);
#endif
					printf("[Image rotate] Rotate 90 mode selected\n");
					break;
				case '2':
					ctrl.id = V4L2_CID_HFLIP;
					ctrl.value = 2;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_WIDTH, LCD_HEIGHT);
#endif
					printf("[Image rotate] X-ais filp mode selected\n");
					break;
				case '3':
					ctrl.id = V4L2_CID_VFLIP;
					ctrl.value = 3;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_WIDTH, LCD_HEIGHT);
#endif
					printf("[Image rotate] Y-ais filp mode selected\n");
					break;
				case '4':
					ctrl.id = V4L2_CID_ROTATE_180;
					ctrl.value = 4;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_WIDTH, LCD_HEIGHT);
#endif
					printf("[Image rotate] Rotate 180 mode selected\n");
					break;
				case '5':
					ctrl.id = V4L2_CID_ROTATE_270;
					ctrl.value = 5;
#if defined(FB_SET_OSD_INFO)
					osd_init(fb_fp, SRC_BPP, LCD_HEIGHT, LCD_WIDTH);
#endif
					printf("[Image rotate] Rotate 270 mode selected\n");
					break;
				}
			}

			if (key == 'b') {
				switch (getchar()) {
				case '0':
				default:
					bright_level = 0;
					printf("[Brightness] level-0\n");
					break;
				case '1':
					bright_level = 1;
					printf("[Brightness] level-1\n");
					break;
				case '2':
					bright_level = 2;
					printf("[Brightness] level-2\n");
					break;
				case '3':
					bright_level = 3;
					printf("[Brightness] level-3\n");
					break;
				case '4':
					bright_level = 4;
					printf("[Brightness] level-4\n");
					break;
				case '5':
					bright_level = 5;
					printf("[Brightness] level-5\n");
					break;
				case '6':
					bright_level = 6;
					printf("[Brightness] level-6\n");
					break;
				case '7':
					bright_level = 7;
					printf("[Brightness] level-7\n");
					break;
				case '8':
					bright_level = 8;
					printf("[Brightness] level-8\n");
					break;
				case '9':
					bright_level = 9;
					printf("[Brightness] level-9\n");
					break;
				}
			}

			key_flag = 1;
		}
	}
}

int main(int argc, char *argv[])
{
#if defined(PREVIEW_USE_POLL)
	struct pollfd events_p[1];
#endif

#if defined(CODEC_USE_POLL)
	struct pollfd events_c[1];
#endif

	struct v4l2_capability cap;
  	struct v4l2_input chan;
	struct v4l2_framebuffer preview;
	struct v4l2_pix_format preview_fmt;
	struct v4l2_format codec_fmt;
	struct v4l2_buffer preview_v4l2_buf;
	struct v4l2_buffer codec_v4l2_buf;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_streamparm stream_param;
	enum v4l2_buf_type type;

	unsigned char rgb_for_preview[640 * 480 * 4];	// MAX
	int ret, retval, start, i, k_id, found = 0;
	char *cmd;
	char file_name[100];
	int yuv_cnt = 0;
	int nArg;
	int showHelp = 0;

	char *envx = getenv( "VIDEO_X_RES" );
	char *envy = getenv( "VIDEO_Y_RES" );
	if (envx != NULL && envy != NULL)
	{
		g_lcd_width = atoi( envx );
		g_lcd_height = atoi( envy );
	}

	cmd = argv[0];
	calc_frame_buffer();

	// Parse for --width= --height= --help
	for (nArg = 1; nArg < argc; nArg++)
	{
		if (strncmp( argv[nArg], "--", 2 ))
		{
			printf( "Unrecognized argument %s\n", argv[nArg] );
			showHelp++;
			continue;
		}
		if (!strcmp( argv[nArg], "--help" ))
		{
			showHelp++;
			continue;
		}
		if (!strncmp( argv[nArg], "--width=", 8 ))
		{
			g_lcd_width = atoi( &argv[nArg][8] );
			if (g_lcd_width <= 0)
			{
				printf( "Invalid width %d\n", g_lcd_width );
				exit_from_app();
			}
			calc_frame_buffer();
			continue;
		}
		if (!strncmp( argv[nArg], "--height=", 9 ))
		{
			g_lcd_height = atoi( &argv[nArg][9] );
			if (g_lcd_height <= 0)
			{
				printf( "Invalid height %d\n", g_lcd_height );
				exit_from_app();
			}
			calc_frame_buffer();
			continue;
		}
		printf( "Unrecognized option %s\n", argv[nArg] );
		showHelp++;
	}

	if (showHelp)
	{
		printf( "Syntax:\n  %s --help\n\tDisplay this message\n  %s [--width=x] [--height=y]\n\tShow camera preview, x to exit\n",
			argv[0], argv[0] );
		exit_from_app();
	}

	printf( "Initializing frame buffer at %d x %d\n", LCD_WIDTH, LCD_HEIGHT );

	if ((cam_p_fp = cam_p_init(PREVIEW_NODE)) < 0) {
		printf("cam_p_init() failed\n");
		exit_from_app();
	}

	if ((cam_c_fp = cam_c_init(CODEC_NODE)) < 0) {
		printf("cam_c_init() failed\n");
		exit_from_app();
	}

#if defined(PREVIEW_USE_POLL)
	memset(events_p, 0, sizeof(events_p));
	events_p[0].fd = cam_p_fp;
	events_p[0].events = POLLIN | POLLERR;
#endif

#if defined(CODEC_USE_POLL)
	memset(events_c, 0, sizeof(events_c));
	events_c[0].fd = cam_c_fp;
	events_c[0].events = POLLIN | POLLERR;
#endif

	if ((fb_fp = fb_init((SRC_BPP / 8), LCD_WIDTH, LCD_HEIGHT)) < 0) {
		printf("fb_init() failed\n");
		exit_from_app();
	}

	fflush(stdout);
	signal_ctrl_c();

 	 /* Get capability */
  	ret = ioctl(cam_p_fp , VIDIOC_QUERYCAP, &cap);

  	if (ret < 0) {
    		printf("[VIDIOC_QUERYCAP] failled\n");
    		exit_from_app();
 	 }

	printf("Name of the interface is %s\n", cap.driver);

  	/* Check the type - preview(OVERLAY) */
  	if (!(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY)) {
    		printf("Can not capture (V4L2_CAP_VIDEO_OVERLAY is false)\n");
    		exit_from_app();
 	 }

	chan.index = 0;
	found = 0;

	while((ioctl(cam_p_fp, VIDIOC_ENUMINPUT, &chan)) == 0) {

		printf("[VIDIOC_ENUMINPUT] Name of channel [%d] is %s\n", chan.index, chan.name);

		/* Test channel.type */
    		if (chan.type & V4L2_INPUT_TYPE_CAMERA) {
      			printf("[VIDIOC_ENUMINPUT] Input channel: Camera (V4L2_INPUT_TYPE_CAMERA)\n");
			found = 1;
			break;
    		}

		chan.index++;
	}

	if(!found)
		exit_from_app();

	/*  Settings for input channel 0 which is channel of webcam */
  	chan.type = V4L2_INPUT_TYPE_CAMERA;

	printf( "Setting up input channel...\n" );
	fflush( stdout );
  	ret = ioctl(cam_p_fp, VIDIOC_S_INPUT, &chan);

  	if (ret < 0) {
    		printf("[VIDIOC_S_INPUT] failed\n");
    		exit_from_app();
  	}

	preview_fmt.width = LCD_WIDTH;
	preview_fmt.height = LCD_HEIGHT;
	preview_fmt.pixelformat = V4L2_PREVIEW_PIX_FMT;

	preview.capability = 0;
	preview.flags = 0;
	preview.fmt = preview_fmt;

	/* Set up for preview */
	printf( "Setting up for preview...\n" );
	fflush( stdout );
	ret = ioctl(cam_p_fp, VIDIOC_S_FBUF, &preview);
	if (ret< 0) {
    		printf("[VIDIOC_S_BUF] failed\n");
    		exit_from_app();
  	}

#if defined(MMAP_ON_PREVIEW)
	printf( "Setting up preview mmap for %d regions\n", PP_NUM );
	fflush( stdout );

	for(i = 0; i < PP_NUM; i++) {
		preview_v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		preview_v4l2_buf.memory = V4L2_MEMORY_MMAP;
		preview_v4l2_buf.index = i;

  		ret = ioctl(cam_p_fp , VIDIOC_QUERYBUF, &preview_v4l2_buf);
		if(ret == -1) {
			printf("[VIDIOC_QUERYBUF] failed\n");
			exit_from_app();
		}

		if ((buffers_p[i].start = (char *) mmap(0, FRAME_BUFFER_SIZE,
                                PROT_READ | PROT_WRITE, MAP_SHARED, cam_p_fp, preview_v4l2_buf.m.offset)) < 0) {
                	perror("mmap() on cam_p_fp\n");
                	return -1;
        	}
		printf("Virtual addr of cam_p_addr(Width(%d), Height(%d)) = 0x%x\n", LCD_WIDTH, LCD_HEIGHT, buffers_p[i].start);
	}
#endif

	codec_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	codec_fmt.fmt.pix.width = CODEC_WIDTH;
	codec_fmt.fmt.pix.height = CODEC_HEIGHT;
	codec_fmt.fmt.pix.pixelformat= V4L2_CODEC_PIX_FMT;

	printf( "Setting video format\n" );
	fflush( stdout );

  	ret = ioctl(cam_c_fp , VIDIOC_S_FMT, &codec_fmt);

  	if (ret < 0) {
    		printf("[VIDIOC_S_FMT] failled\n");
    		exit_from_app();
 	 }

#if defined(MMAP_ON_CODEC)
	printf( "Setting up codec mmap\n" );
	fflush( stdout );

	for(i = 0; i < PP_NUM; i++) {
		codec_v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		codec_v4l2_buf.memory = V4L2_MEMORY_MMAP;
		codec_v4l2_buf.index = i;

  		ret = ioctl(cam_c_fp , VIDIOC_QUERYBUF, &codec_v4l2_buf);

		if(ret == -1) {
			printf("[VIDIOC_QUERYBUF] failed\n");
			exit_from_app();
		}

		if ((buffers_c[i].start = (char *) mmap(0, YUV_PCT_SIZE,
                                PROT_READ | PROT_WRITE, MAP_SHARED, cam_c_fp, codec_v4l2_buf.m.offset)) < 0) {
                	perror("mmap() on cam_c_fp");
                	return -1;
        	}

		printf("virtual addr of cam_c_addr(Width(%d), Height(%d)) = 0x%x\n", CODEC_WIDTH, CODEC_HEIGHT, buffers_c[i].start);
	}
#endif
	fmalloc(YUV_PCT_SIZE);

	/* Preview start */
	start = 1;
	printf( "Starting preview overlay\n" );
	fflush( stdout );

	ret = ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);

	if (ret < 0) {
    		printf("[VIDIOC_OVERLAY] failed\n");
    		exit_from_app();
  	}

	/* Codec set */
	/* Get capability */
	printf( "Getting codec capability\n" );
	fflush( stdout );
  	ret = ioctl(cam_c_fp , VIDIOC_QUERYCAP, &cap);

  	if (ret < 0) {
    		printf("[VIDIOC_QUERYCAP] failed\n");
    		exit_from_app();
	}

	printf("Name of the interface is %s\n", cap.driver);

 	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
   		printf("Can not capture (V4L2_CAP_VIDEO_CAPTURE is false)\n");
  		exit_from_app();
 	 }

	k_id = pthread_create(&pth, 0, (void *) &get_key, 0);

	if (k_id != 0) {
		printf("pthread creation failed\n");
		exit_from_app();
	}

	while (1) {
#if defined(PREVIEW_USE_POLL)
		retval = poll((struct pollfd *) &events_p, 1, 500);

		if(retval < 0) {
			perror("poll error\n");
			exit_from_app();
		}

		if(retval == 0) {
			printf("No data in 500 ms..\n");
			continue;
		}
#endif
  		ret = ioctl(cam_p_fp , VIDIOC_DQBUF, &preview_v4l2_buf);

#if defined(MMAP_ON_PREVIEW)
		if (!read_data(cam_p_fp, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP, preview_v4l2_buf.index)) {
			printf("read_data() failed\n");
			break;
		}
#else
		if (!read_data(cam_p_fp, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP, 0)) {
			printf("read_data() failed\n");
			break;
		}
#endif
		draw(fb_addr, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP);
		usleep(50000);

		while (key_flag) {
			key_flag = 0;

			switch (key) {
			case 'z': /* Zoom-in */
				ctrl.id = V4L2_CID_ZOOMIN;
				ctrl.value = 0;
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] V4L2_CID_ZOOMIN failed\n");
		    			exit_from_app();
			  	}
				break;

			case 'a': /* Zoom-out */
				ctrl.id = V4L2_CID_ZOOMOUT;
				ctrl.value = 0;
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] V4L2_CID_ZOOMOUT failed\n");
		    			exit_from_app();
			  	}
				break;

			case 'p': /* crop */
				crop.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
				ret = ioctl(cam_p_fp, VIDIOC_G_CROP, &crop);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_G_CROP] VIDIOC_G_CROP failed\n");
		    			exit_from_app();
			  	}

				printf("offset = (%d, %d), size = %dx%d\n", crop.c.left, crop.c.top, crop.c.width, crop.c.height);
				break;

			case 'm': /* Mirror */
				ctrl.id = V4L2_CID_ROTATE_180;
				ctrl.value = 4;
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] V4L2_CID_ROTATE_180 failed\n");
		    			exit_from_app();
			  	}
				break;

			case 'r': /* Rotate */
				start = 0;
				ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);

				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] ROTATE failed\n");
		    			exit_from_app();
			  	}

				memset(fb_addr, 0, LCD_WIDTH * LCD_WIDTH * SRC_BPP);

				start = 1;
				ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);
				break;

			case 'w': /* White Balance */
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] V4L2_CID_AUTO_WHITE_BALANCE failed\n");
		    			exit_from_app();
			  	}
				break;

			case 'e': /* Image Effect */
				ret = ioctl(cam_p_fp, VIDIOC_S_CTRL, &ctrl);
				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] Image Effect failed\n");
		    			exit_from_app();
			  	}
				break;

			case 'c': /* Image capture */
				stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				stream_param.parm.capture.capturemode = V4L2_MODE_HIGHQUALITY;
				ret = ioctl(cam_c_fp, VIDIOC_S_PARM, &stream_param);
				if (ret < 0) {
			   		printf("[VIDIOC_S_PARM] failed\n");
			   		exit_from_app();
			 	}

				/* Codec start */
				start = 1;
				ret = ioctl(cam_c_fp, VIDIOC_STREAMON, &start);

				if (ret < 0) {
			   		printf("[VIDIOC_STREAMON] failed\n");
			   		exit_from_app();
			 	}

#if defined(CODEC_USE_POLL)
				retval = poll((struct pollfd *) &events_c, 1, 5000);

				if(retval < 0) {
					perror("poll error\n");
					exit_from_app();
				}

				if(retval == 0) {
					printf("No data in 5 secs..\n");
					return;
				}
#endif
				/* Codec stop */
				start = 0;
				ret = ioctl(cam_c_fp, VIDIOC_STREAMOFF, &start);

				if (ret < 0) {
			    		printf("[VIDIOC_STREAMOFF] failed\n");
					exit_from_app();;
			 	}

				stream_param.parm.capture.capturemode = 0;
				ret = ioctl(cam_p_fp, VIDIOC_S_PARM, &stream_param);

				if (ret < 0) {
			   		printf("[VIDIOC_S_PARM] failed\n");
			   		exit_from_app();;
			 	}

				start = 1;
				ret = ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);

				if (ret < 0) {
						printf("[VIDIOC_OVERLAY] failed\n");
						exit_from_app();
				}

				ret = ioctl(cam_c_fp , VIDIOC_DQBUF, &codec_v4l2_buf);
#if defined(MMAP_ON_CODEC)
				save_yuv(CODEC_WIDTH, CODEC_HEIGHT, YUV_FMT, codec_v4l2_buf.index);
#else
				save_yuv(CODEC_WIDTH, CODEC_HEIGHT, YUV_FMT, 0);
#endif
			    	printf("save_yuv\n");
				break;

			case 'f': /* Multi-frame image capture */
				stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				stream_param.parm.capture.capturemode = V4L2_MODE_HIGHQUALITY;
				ret = ioctl(cam_c_fp, VIDIOC_S_PARM, &stream_param);

				if (ret < 0) {
			   		printf("[VIDIOC_S_PARM] failed\n");
			   		exit_from_app();;
			 	}

#if !defined(USE_LAST_IRQ)
				/* Codec start */
				start = 1;
				ret = ioctl(cam_c_fp, VIDIOC_STREAMON, &start);

				if (ret < 0) {
					printf("[VIDIOC_STREAMON] failed\n");
					exit_from_app();;
				}

				while (frame_count < 10) {
#if defined(CODEC_USE_POLL)
					retval = poll((struct pollfd *) &events_c, 1, 5000);

					if(retval < 0) {
						perror("poll error\n");
						exit_from_app();;
					}

					if(retval == 0) {
						printf("No data in 5 secs..\n");
						return;
					}
#endif
					ret = ioctl(cam_c_fp , VIDIOC_DQBUF, &codec_v4l2_buf);
					save_yuv(CODEC_WIDTH, CODEC_HEIGHT, YUV_FMT, codec_v4l2_buf.index);
					printf("save_yuv\n");
					frame_count++;
				}

				frame_count = 0;

				/* Codec stop */
				start = 0;
				ret = ioctl(cam_c_fp, VIDIOC_STREAMOFF, &start);
				if (ret < 0) {
			    		printf("[VIDIOC_STREAMOFF] failed\n");
					exit_from_app();;
			 	}

				stream_param.parm.capture.capturemode = 0;
				ret = ioctl(cam_p_fp, VIDIOC_S_PARM, &stream_param);
				if (ret < 0) {
			   		printf("[VIDIOC_S_PARM] failed\n");
			   		exit_from_app();;
			 	}

				start = 1;
				ret = ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);

				if (ret < 0) {
						printf("[VIDIOC_OVERLAY] failed\n");
						exit_from_app();
				}

				break;
#else
				while (frame_count < 10) {
					/* Codec start */
					start = 1;
					ret = ioctl(cam_c_fp, VIDIOC_STREAMON, &start);

					if (ret < 0) {
						printf("[VIDIOC_STREAMON] failed\n");
						exit_from_app();;
					}


#if defined(CODEC_USE_POLL)
					retval = poll((struct pollfd *) &events_c, 1, 5000);

					if(retval < 0) {
						perror("poll error\n");
						exit_from_app();;
					}

					if(retval == 0) {
						printf("No data in 5 secs..\n");
						return;
					}
#endif

					ret = ioctl(cam_c_fp , VIDIOC_DQBUF, &codec_v4l2_buf);
					save_yuv(CODEC_WIDTH, CODEC_HEIGHT, YUV_FMT, codec_v4l2_buf.index);
					printf("save_yuv\n");
					frame_count++;

					/* Codec stop */
					start = 0;
					ret = ioctl(cam_c_fp, VIDIOC_STREAMOFF, &start);
					if (ret < 0) {
							printf("[VIDIOC_STREAMOFF] failed\n");
						exit_from_app();;
					}
				}

				frame_count = 0;

				stream_param.parm.capture.capturemode = 0;
				ret = ioctl(cam_p_fp, VIDIOC_S_PARM, &stream_param);

				if (ret < 0) {
					printf("[VIDIOC_S_PARM] failed\n");
					exit_from_app();;
				}

				start = 1;
				ret = ioctl(cam_p_fp, VIDIOC_OVERLAY, &start);

				if (ret < 0) {
					printf("[VIDIOC_OVERLAY] failed\n");
					exit_from_app();
				}

				break;
#endif

			case 'b': /* Brightness */
				ret = ioctl(fb_fp, SET_DISPLAY_BRIGHTNESS, &bright_level);

				if (ret < 0) {
		    			fprintf(stderr, "[VIDIOC_S_CTRL] Brightness failed\n");
		    			exit_from_app();;
			  	}

				break;

			case 'x': /* Stop Application */
				printf("Stop application\n");
				exit_from_app();

			/*** OSD ****************************/
			case '+':
				ioctl(fb_fp, SET_OSD_ALPHA_UP);
				break;
			case '-':
				ioctl(fb_fp, SET_OSD_ALPHA_DOWN);
				break;
			case '4':
				ioctl(fb_fp, SET_OSD_MOVE_LEFT);
				break;
			case '6':
				ioctl(fb_fp, SET_OSD_MOVE_RIGHT);
				break;
			case '8':
				ioctl(fb_fp, SET_OSD_MOVE_UP);
				break;
			case '2':
				ioctl(fb_fp, SET_OSD_MOVE_DOWN);
				break;
			}

#if defined(PREVIEW_USE_POLL)
			retval = poll((struct pollfd *) &events_p, 1, 500);

			if(retval < 0) {
				perror("poll error : ");
				exit_from_app();;
			}

			if(retval == 0) {
				printf("No data in 500 ms..\n");
				continue;
			}
#endif
	  		ret = ioctl(cam_p_fp , VIDIOC_DQBUF, &preview_v4l2_buf);

#if defined(MMAP_ON_PREVIEW)
			if (!read_data(cam_p_fp, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP, preview_v4l2_buf.index)) {
				printf("read_data() failed\n");
				break;
			}
#else
			if (!read_data(cam_p_fp, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP, 0)) {
				printf("read_data() failed\n");
				break;
			}
#endif
			draw(fb_addr, &rgb_for_preview[0], LCD_WIDTH, LCD_HEIGHT, SRC_BPP);
		}// while (key_flag)
	}// while (1)

	exit_from_app();
	return 0;
}

