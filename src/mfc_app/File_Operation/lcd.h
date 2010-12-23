#ifndef __SAMSUNG_SYSLSI_APDEV_S3C_LCD_H__
#define __SAMSUNG_SYSLSI_APDEV_S3C_LCD_H__

typedef struct {
	int Bpp;
	int LeftTop_x;
	int LeftTop_y;
	int Width;
	int Height;
} s3c_win_info_t;

#define SET_OSD_INFO	_IOW('F', 209, s3c_win_info_t)
#define SET_OSD_START	_IO('F', 201)

#define FB_DEV_NAME		"/dev/fb0"
#define FB_DEV_NAME1	"/dev/fb1"

#endif //__SAMSUNG_SYSLSI_APDEV_S3C_LCD_H__
