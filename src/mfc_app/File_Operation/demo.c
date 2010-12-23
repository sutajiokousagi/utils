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
#include <linux/vt.h>
#include <linux/fb.h>
#include "mfc.h"
#include "MfcDrvParams.h"
#include "post.h"
#include "lcd.h"
#include "performance.h"
#include "FrameExtractor.h"
#include "MPEG4Frames.h"


static unsigned char delimiter_mpeg4[] = {0x00, 0x00, 0x01 };

int Demo(int argc, char **argv)
{
	int			mfc_fd, mfc_fd2, pp_fd, fb_fd, fb_fd2, in_fd, in_fd2;
	char		*addr, *addr2, *in_addr, *in_addr2, *fb_addr, *fb_addr2;
	int			file_size, file_size2, fb_size, fb_size2;
	char		*file_pos_cur, *file_pos_end;
	char		*file_pos_cur2, *file_pos_end2;
	int			last_unit_size;

	pp_params		pp_param, pp_param2;
	s3c_win_info_t	osd_info_to_driver;

	struct stat					s;
	struct fb_fix_screeninfo	lcd_info, lcd_info2;		
	
	// arguments of MFC ioctl
	MFC_DEC_INIT_ARG			dec_init;
	MFC_DEC_EXE_ARG				dec_exe;
	MFC_GET_BUF_ADDR_ARG		get_buf_addr;

	MFC_DEC_INIT_ARG			dec_init2;
	MFC_DEC_EXE_ARG				dec_exe2;
	MFC_GET_BUF_ADDR_ARG		get_buf_addr2;

	FRAMEX_CTX		*pFrameExCtx2;	// frame extractor context
	FRAMEX_STRM_PTR file_strm2;
	char			*pStrmBuf;
	int				nStrmSize;
	int 			r;

#ifdef FPS
	struct timeval	start, stop;
	unsigned int	time = 0;
	int				frame_cnt = 0;
#endif
	
	if (argc != 3) {
		printf("Usage : mfc <H.264 filename> <MPEG4 filename>\n");
		return -1;
	}

	// in/out file open
	in_fd = open(argv[1], O_RDONLY);
	if(in_fd < 0) {
		printf("input file open error\n");
		return -1;
	}

	in_fd2 = open(argv[2], O_RDONLY);
	if(in_fd2 < 0) {
		printf("input file2 open error\n");
		return -1;
	}

	// get input file size
	fstat(in_fd, &s);
	file_size = s.st_size;
	fstat(in_fd2, &s);
	file_size2 = s.st_size;
	
	// mapping input file to memory
	in_addr = (char *)mmap(0, file_size, PROT_READ, MAP_SHARED, in_fd, 0);
	if(in_addr == NULL) {
		printf("input file memory mapping failed\n");
		return -1;
	}

	// mapping input file to memory
	in_addr2 = (char *)mmap(0, file_size2, PROT_READ, MAP_SHARED, in_fd2, 0);
	if(in_addr2 == NULL) {
		printf("input file2 memory mapping failed\n");
		return -1;
	}

	file_pos_cur = in_addr;
	file_pos_end = in_addr + file_size;

	file_pos_cur2 = in_addr2;
	file_pos_end2 = in_addr2 + file_size2;

	pStrmBuf = (char *)malloc(VIDEO_BUFFER_SIZE);

	pFrameExCtx2 = FrameExtractorInit(FRAMEX_IN_TYPE_MEM, delimiter_mpeg4, sizeof(delimiter_mpeg4), 1);   

	file_strm2.p_start = file_strm2.p_cur = (unsigned char *)in_addr2;
	file_strm2.p_end = (unsigned char *)(in_addr2 + file_size2);
	FrameExtractorFirst(pFrameExCtx2, &file_strm2);

	nStrmSize = ExtractConfigStreamMpeg4(pFrameExCtx2, &file_strm2, (unsigned char *)pStrmBuf, VIDEO_BUFFER_SIZE, NULL);
	if (nStrmSize < 4) {
		printf("Cannot get the config stream for the MPEG4.\n");
		return -1;
	}
		
	// MFC open
	mfc_fd = open(MFC_DEV_NAME, O_RDWR|O_NDELAY);
	if (mfc_fd < 0) {
		printf("MFC open error : %d\n", mfc_fd);
		return -1;
	}

	// mapping shared in/out buffer between App and D/D for MFC
	addr = (char *) mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mfc_fd, 0);
	if (addr == NULL) {
		printf("MFC mmap failed\n");
		return -1;
	}

	// MFC open
	mfc_fd2 = open(MFC_DEV_NAME, O_RDWR|O_NDELAY);
	if (mfc_fd2 < 0) {
		printf("MFC open error : %d\n", mfc_fd2);
		return -1;
	}

	// mapping shared in/out buffer between App and D/D for MFC
	addr2 = (char *) mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mfc_fd2, 0);
	if (addr2 == NULL) {
		printf("MFC mmap failed\n");
		return -1;
	}

	// Post processor open
	pp_fd = open(PP_DEV_NAME, O_RDWR|O_NDELAY);
	if(pp_fd < 0)
	{
		printf("Post processor open error\n");
		return -1;
	}

	// LCD frame buffer open
	fb_fd = open(FB_DEV_NAME, O_RDWR|O_NDELAY);
	if(fb_fd < 0)
	{
		printf("LCD frame buffer open error\n");
		return -1;
	}

	// LCD frame buffer open
	fb_fd2 = open(FB_DEV_NAME1, O_RDWR|O_NDELAY);
	if(fb_fd2 < 0)
	{
		printf("LCD frame buffer open error\n");
		return -1;
	}

	// get input buffer address in ring buffer mode
	// When below ioctl function is called for the first time, It returns double buffer size.
	// So, Input buffer will be filled as 2 part unit size
	get_buf_addr.in_usr_data = (int)addr;
	ioctl(mfc_fd, IOCTL_MFC_GET_RING_BUF_ADDR, &get_buf_addr);

	get_buf_addr2.in_usr_data = (int)addr2;
	ioctl(mfc_fd2, IOCTL_MFC_GET_LINE_BUF_ADDR, &get_buf_addr2);
printf("input buffer addr : 0x%X, input buffer2 addr : 0x%X\n", get_buf_addr.out_buf_addr, get_buf_addr2.out_buf_addr);

	// copy input stream to input buffer
	memcpy((char *)get_buf_addr.out_buf_addr, in_addr, get_buf_addr.out_buf_size);	
	file_pos_cur += get_buf_addr.out_buf_size;

	memcpy((char *)get_buf_addr2.out_buf_addr, pStrmBuf, nStrmSize);	
	
	// MFC decoder initialization
	dec_init.in_strmSize = get_buf_addr.out_buf_size;
	ioctl(mfc_fd, IOCTL_MFC_H264_DEC_INIT, &dec_init);
	printf("out_width : %d, out_height : %d\n", dec_init.out_width, dec_init.out_height);

	dec_init2.in_strmSize = nStrmSize;
	ioctl(mfc_fd2, IOCTL_MFC_MPEG4_DEC_INIT, &dec_init2);
	printf("out_width : %d, out_height : %d\n", dec_init2.out_width, dec_init2.out_height);

	// set post processor configuration
	pp_param.SrcFullWidth	= dec_init.out_width;
	pp_param.SrcFullHeight	= dec_init.out_height;
	pp_param.SrcCSpace		= YC420;
	pp_param.DstFullWidth	= 800;		// destination width
	pp_param.DstFullHeight	= 480;		// destination height
	pp_param.DstCSpace		= RGB16;
	pp_param.OutPath		= POST_DMA;
	pp_param.Mode			= ONE_SHOT;

	// set post processor configuration
	pp_param2.SrcFullWidth	= dec_init2.out_width;
	pp_param2.SrcFullHeight	= dec_init2.out_height;
	pp_param2.SrcCSpace		= YC420;
	pp_param2.DstFullWidth	= 400;		// destination width
	pp_param2.DstFullHeight	= 240;		// destination height
	pp_param2.DstCSpace		= RGB16;
	pp_param2.OutPath		= POST_DMA;
	pp_param2.Mode			= ONE_SHOT;
	
	// get LCD frame buffer address
	fb_size = pp_param.DstFullWidth * pp_param.DstFullHeight * 2;	// RGB565
	fb_addr = (char *)mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_addr == NULL) {
		printf("LCD frame buffer mmap failed\n");
		return -1;
	}

	// get LCD frame buffer address
	fb_size2 = pp_param2.DstFullWidth * pp_param2.DstFullHeight * 2;	// RGB565
	fb_addr2 = (char *)mmap(0, fb_size2, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd2, 0);
	if (fb_addr2 == NULL) {
		printf("LCD frame buffer mmap failed\n");
		return -1;
	}

	osd_info_to_driver.Bpp			= 16;	// RGB16
	osd_info_to_driver.LeftTop_x	= 0;	
	osd_info_to_driver.LeftTop_y	= 0;
	osd_info_to_driver.Width		= 400;	// display width
	osd_info_to_driver.Height		= 240;	// display height

	// set OSD's information 
	if(ioctl(fb_fd2, SET_OSD_INFO, &osd_info_to_driver)) {
		printf("Some problem with the ioctl SET_OSD_INFO\n");
		return -1;
	}

	ioctl(fb_fd2, SET_OSD_START);


	while(1)
	{
		
		dec_exe2.in_strmSize = nStrmSize;
		r = ioctl(mfc_fd2, IOCTL_MFC_MPEG4_DEC_EXE, &dec_exe2);
		if ( (r < 0) || (dec_exe2.ret_code < 0) ) {
			if (get_buf_addr2.ret_code == MFCDRV_RET_ERR_HANDLE_INVALIDATED) {
				printf("The Handle of MFC Instance was invalidated!!!\n"); 
			}
			printf("IOCTL_MFC_MPEG4_DEC_EXE failure...\n");
			printf("ret_code : %d, r : %d\n", get_buf_addr2.ret_code, r);
			return -1;
		}
		
		nStrmSize = NextFrameMpeg4(pFrameExCtx2, &file_strm2, (unsigned char *)pStrmBuf, VIDEO_BUFFER_SIZE, NULL);
		if (nStrmSize < 4) {
			break;
		}

		//printf("nStrmSize = %d\n", nStrmSize);
		
		if(nStrmSize == 6) {
			printf("nStrmSize == 0\n");
			file_strm2.p_start 	= file_strm2.p_cur = (unsigned char *)in_addr2;
			file_strm2.p_end	= (unsigned char *)(in_addr2 + file_size2);
			FrameExtractorFirst(pFrameExCtx2, &file_strm2);
			nStrmSize = ExtractConfigStreamMpeg4(pFrameExCtx2, &file_strm2, (unsigned char *)pStrmBuf, VIDEO_BUFFER_SIZE, NULL);
			continue;			
		}
		//////////////////////////////////
		//		(MFC ioctl)				//
		//	IOCTL_MFC_GET_LINE_BUF_ADDR	//
		//////////////////////////////////
		get_buf_addr2.in_usr_data = (int)addr2;
		r = ioctl(mfc_fd2, IOCTL_MFC_GET_LINE_BUF_ADDR, &get_buf_addr2);
		if ( (r < 0) || (get_buf_addr2.ret_code < 0) ) {
			if (get_buf_addr2.ret_code == MFCDRV_RET_ERR_HANDLE_INVALIDATED) {
				printf("The Handle of MFC Instance was invalidated!!!\n"); 
			}
			printf("IOCTL_MFC_GET_LINE_BUF_ADDR failure...\n");
			return -1;
		}
		
		memcpy((char *)get_buf_addr2.out_buf_addr, pStrmBuf, nStrmSize);
		
		
		//////////////////////////////////
		//		(MFC ioctl				//
		//	IOCTL_MFC_GET_FRAM_BUF_ADDR	//
		//////////////////////////////////
		ioctl(mfc_fd2, IOCTL_MFC_GET_PHY_FRAM_BUF_ADDR, &get_buf_addr2);
		
		// Post processing
		// pp_param.SrcFrmSt에는 MFC의 output buffer의 physical address가
		// pp_param.DstFrmSt에는 LCD frame buffer의 physical address가 입력으로 넣어야 한다.
		pp_param2.SrcFrmSt		= get_buf_addr2.out_buf_addr;	// MFC output buffer
		ioctl(fb_fd2, FBIOGET_FSCREENINFO, &lcd_info2);
		pp_param2.DstFrmSt		= lcd_info2.smem_start;			// LCD frame buffer
		ioctl(pp_fd, PPROC_SET_PARAMS, &pp_param2);
		ioctl(pp_fd, PPROC_START);	

	
	
		// if input stream
		get_buf_addr.in_usr_data = (int)addr;
		ioctl(mfc_fd, IOCTL_MFC_GET_RING_BUF_ADDR, &get_buf_addr);
		if(get_buf_addr.out_buf_size > 0) {
			if((file_pos_end - file_pos_cur) >= get_buf_addr.out_buf_size) {
				memcpy((char *)get_buf_addr.out_buf_addr, file_pos_cur, get_buf_addr.out_buf_size);
				file_pos_cur += get_buf_addr.out_buf_size;
				dec_exe.in_strmSize = get_buf_addr.out_buf_size;
			} else {	// if last unit
				last_unit_size = in_addr + file_size - file_pos_cur;
				memcpy((char *)get_buf_addr.out_buf_addr, file_pos_cur, last_unit_size);
				memcpy((char *)get_buf_addr.out_buf_addr + last_unit_size, in_addr, get_buf_addr.out_buf_size - last_unit_size);
				file_pos_cur = in_addr + get_buf_addr.out_buf_size - last_unit_size;
				dec_exe.in_strmSize = get_buf_addr.out_buf_size;
			}
		}
		else {
			dec_exe.in_strmSize = 0;
		}

	#ifdef FPS
		gettimeofday(&start, NULL);
	#endif

	
	// MFC decoding
		ioctl(mfc_fd, IOCTL_MFC_H264_DEC_EXE, &dec_exe);
		if(dec_exe.ret_code < 0) {
			printf("ret code : %d\n", dec_exe.ret_code);
			break;
		}

	#ifdef FPS
		gettimeofday(&stop, NULL);
		time += measureTime(&start, &stop);
		frame_cnt++;
	#endif


		// get output buffer address
		ioctl(mfc_fd, IOCTL_MFC_GET_PHY_FRAM_BUF_ADDR, &get_buf_addr);
		
		// Post processing
		// pp_param.SrcFrmSt에는 MFC의 output buffer의 physical address가
		// pp_param.DstFrmSt에는 LCD frame buffer의 physical address가 입력으로 넣어야 한다.
		pp_param.SrcFrmSt		= get_buf_addr.out_buf_addr;	// MFC output buffer
		ioctl(fb_fd, FBIOGET_FSCREENINFO, &lcd_info);
		pp_param.DstFrmSt		= lcd_info.smem_start;			// LCD frame buffer
		ioctl(pp_fd, PPROC_SET_PARAMS, &pp_param);
		ioctl(pp_fd, PPROC_START);	

	
		//printf("LCD1 ADDR : 0x%X,  LCD2 ADDR : 0x%X\n", lcd_info.smem_start, lcd_info2.smem_start);

	
	
	}

#ifdef FPS
	printf("Decoding Time : %u, Frame Count : %d, FPS : %f\n", time, frame_cnt, (float)frame_cnt*1000/time);
#endif
	
	close(mfc_fd);
	close(mfc_fd2);
	close(pp_fd);
	close(fb_fd);
	close(fb_fd2);
	close(in_fd);

	return 0;
}

