#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#include "LogMsg.h"
#include "JPGApi.h"
#include "performance.h"
#include "lcd.h"
#include "post.h"


#define JPEG_INPUT_FILE		"./TestVectors/test_420_1600_1200.jpg"

#define LCD_WIDTH		800
#define LCD_HEIGHT		600

#ifdef RGB24BPP
	#define LCD_BPP			24
#else
	#define LCD_BPP			16
#endif

static int fb_init(int win_num, int bpp, int x, int y, int width, int height, unsigned int *addr);


/*
 *******************************************************************************
Name            : TestDecoder
Description     : To test Decoder
Parameter       : imageType - JPG_YCBYCR or JPG_RGB16
Return Value    : void
 *******************************************************************************
 */
int Test_Jpeg_Display(int argc, char **argv)
{
	int 	fb_fd;
	int 	pp_fd;
	int		in_fd;
	int		buf_size;
	int		handle;
	long 	streamSize;
	char 	*InBuf = NULL;
	char 	*OutBuf = NULL;
	char	*pp_in_buf;
	char	*pp_out_buf;
	char	*in_addr;
	char 	*pp_addr;
	UINT32 	fileSize;
	INT32 	width, height, samplemode;
	unsigned int	fb_addr;
	pp_params 		pp_param;
	JPEG_ERRORTYPE 	ret;
	struct stat		s;
	struct s3c_fb_dma_info	fb_info;

#ifdef FPS
	struct timeval start;
	struct timeval stop;
	unsigned int	time = 0;
#endif


	/* LCD frame buffer initialization */
	fb_fd = fb_init(0, LCD_BPP, 0, 0, LCD_WIDTH, LCD_HEIGHT, &fb_addr);
	if (fb_fd < 0) {
		printf("frame buffer open error\n");
		return -1;
	}

	/* Post processor open */
	pp_fd = open(PP_DEV_NAME, O_RDWR|O_NDELAY);
	if (pp_fd < 0) {
		printf("post processor open error\n");
		return -1;
	}

	/* Get post processor's input buffer address */
	buf_size = ioctl(pp_fd, PPROC_GET_BUF_SIZE);
	pp_addr = (char *)mmap(0, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, pp_fd, 0);
	if(pp_addr == NULL) {
		printf("Post processor mmap failed\n");
		return -1;
	}
	pp_in_buf = pp_addr;
	pp_out_buf = pp_in_buf + ioctl(pp_fd, PPROC_GET_INBUF_SIZE);

	/* Input file(JPEG file) open */
	char *input_file = argc > 2 ? argv[2] : JPEG_INPUT_FILE;
	in_fd = open(input_file, O_RDONLY);
	if (in_fd < 0) {
		printf("Input file open error: %s\n", input_file);
		return -1;
	}

	fstat(in_fd, &s);
	fileSize = s.st_size;

	/* mapping input file to memory */
	in_addr = (char *)mmap(0, fileSize, PROT_READ, MAP_SHARED, in_fd, 0);
	if (in_addr == NULL) {
		printf("Input file memory mapping failed\n");
		return -1;
	}


#ifdef FPS
	gettimeofday(&start, NULL);
#endif

	/* JPEG driver initialization */
	handle = SsbSipJPEGDecodeInit();
	if(handle < 0)
		return -1;

#ifdef FPS
	gettimeofday(&stop, NULL);
	time += measureTime(&start, &stop);
#endif


	/* Get jpeg's input buffer address */
	InBuf = SsbSipJPEGGetDecodeInBuf(handle, fileSize);
	if(InBuf == NULL){
		printf("Input buffer is NULL\n");
		return -1;
	}


	/* Put JPEG frame to Input buffer */
	memcpy(InBuf, in_addr, fileSize);
	close(in_fd);


#ifdef FPS
	gettimeofday(&start, NULL);
#endif

	/* Decode JPEG frame */
	ret = SsbSipJPEGDecodeExe(handle);
	if (ret != JPEG_OK) {
		printf("Decoding failed\n");
		return -1;
	}

#ifdef FPS
	gettimeofday(&stop, NULL);
	time += measureTime(&start, &stop);
	printf("[JPEG Decoding Performance] Elapsed time : %u\n", time);
	time = 0;
#endif


	/* Get Output buffer address */
	OutBuf = SsbSipJPEGGetDecodeOutBuf(handle, &streamSize);
	if(OutBuf == NULL){
		printf("Output buffer is NULL\n");
		return -1;
	}


	/* Get decode config. */
	SsbSipJPEGGetConfig(JPEG_GET_DECODE_WIDTH, &width);
	SsbSipJPEGGetConfig(JPEG_GET_DECODE_HEIGHT, &height);
	SsbSipJPEGGetConfig(JPEG_GET_SAMPING_MODE, &samplemode);


	/* Set post processor */
	pp_param.SrcFullWidth	= width;
	pp_param.SrcFullHeight	= height;
	pp_param.SrcStartX		= 0;
	pp_param.SrcStartY		= 0;
	pp_param.SrcWidth		= pp_param.SrcFullWidth;
	pp_param.SrcHeight		= pp_param.SrcFullHeight;
	pp_param.SrcCSpace		= YCBYCR;
	pp_param.DstStartX		= 0;
	pp_param.DstStartY		= 0;
	pp_param.DstFullWidth	= LCD_WIDTH;
	pp_param.DstFullHeight	= LCD_HEIGHT;
	pp_param.DstWidth		= pp_param.DstFullWidth;
	pp_param.DstHeight		= pp_param.DstFullHeight;
	pp_param.DstCSpace		= RGB16;
#ifdef RGB24BPP
	pp_param.DstCSpace		= RGB24;
#endif
	pp_param.OutPath		= 0;
	pp_param.Mode			= 0;


	/* copy decoded frame to post processor's input buffer */
	memcpy(pp_in_buf, OutBuf, width*height*2);
	ioctl(fb_fd, GET_FB_INFO, &fb_info);
	pp_param.SrcFrmSt = ioctl(pp_fd, PPROC_GET_PHY_INBUF_ADDR);
	pp_param.DstFrmSt = pp_param.SrcFrmSt + ioctl(pp_fd, PPROC_GET_INBUF_SIZE);

	ioctl(pp_fd, PPROC_SET_PARAMS, &pp_param);
	ioctl(pp_fd, PPROC_START);

	memcpy(fb_addr, pp_out_buf, LCD_WIDTH * LCD_HEIGHT * 2);

	//////////////////////////////////////////////////////////////
	// 9. finalize handle                                      //
	//////////////////////////////////////////////////////////////
	SsbSipJPEGDecodeDeInit(handle);
	close(pp_fd);
	close(fb_fd);

	return 0;
}


static int fb_init(int win_num, int bpp, int x, int y, int width, int height, unsigned int *addr)
{
	int 			dev_fp = -1;
	int 			fb_size;
	s3c_win_info_t	fb_info_to_driver;


	switch(win_num)
	{
		case 0:
			dev_fp = open(FB_DEV_NAME, O_RDWR);
			break;
		case 1:
			dev_fp = open(FB_DEV_NAME1, O_RDWR);
			break;
		case 2:
			dev_fp = open(FB_DEV_NAME2, O_RDWR);
			break;
		case 3:
			dev_fp = open(FB_DEV_NAME3, O_RDWR);
			break;
		case 4:
			dev_fp = open(FB_DEV_NAME4, O_RDWR);
			break;
		default:
			printf("Window number is wrong\n");
			return -1;
	}
	if (dev_fp < 0) {
		perror(FB_DEV_NAME);
		return -1;
	}

	switch(bpp)
	{
		case 16:
			fb_size = width * height * 2;
			break;
		case 24:
			fb_size = width * height * 4;
			break;
		default:
			printf("16 and 24 bpp support");
			return -1;
	}

	if ((*addr = (unsigned int) mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fp, 0)) < 0) {
		printf("mmap() error in fb_init()");
		return -1;
	}

	fb_info_to_driver.Bpp 		= bpp;
	fb_info_to_driver.LeftTop_x	= x;
	fb_info_to_driver.LeftTop_y	= y;
	fb_info_to_driver.Width 	= width;
	fb_info_to_driver.Height 	= height;

	if (ioctl(dev_fp, SET_OSD_INFO, &fb_info_to_driver)) {
		printf("Some problem with the ioctl SET_VS_INFO!!!\n");
		return -1;
	}

	if (ioctl(dev_fp, SET_OSD_START)) {
		printf("Some problem with the ioctl START_OSD!!!\n");
		return -1;
	}

	return dev_fp;
}

