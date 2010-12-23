#ifndef __SAMSUNG_SYSLSI_APDEV_MFC_DRV_PARAMS_H__
#define __SAMSUNG_SYSLSI_APDEV_MFC_DRV_PARAMS_H__

typedef struct {
	int ret_code;			// [OUT] Return code
	int in_width;			// [IN]  width  of YUV420 frame to be encoded
	int in_height;			// [IN]  height of YUV420 frame to be encoded
	int in_bitrate;			// [IN]  Encoding parameter: Bitrate (kbps)
	int in_gopNum;			// [IN]  Encoding parameter: GOP Number (interval of I-frame)
	int in_frameRateRes;	// [IN]  Encoding parameter: Frame rate (Res)
	int in_frameRateDiv;	// [IN]  Encoding parameter: Frame rate (Divider)
} MFC_ENC_INIT_ARG;

typedef struct {
	int ret_code;			// [OUT] Return code
	int out_encoded_size;	// [OUT] Length of Encoded video stream
} MFC_ENC_EXE_ARG;

typedef struct {
	int ret_code;			// [OUT] Return code
	int in_strmSize;		// [IN]  Size of video stream filled in STRM_BUF
	int out_width;			// [OUT] width  of YUV420 frame
	int out_height;			// [OUT] height of YUV420 frame
} MFC_DEC_INIT_ARG;

typedef struct {
	int ret_code;			// [OUT] Return code
	int in_strmSize;		// [IN]  Size of video stream filled in STRM_BUF
} MFC_DEC_EXE_ARG;

typedef struct {
	int ret_code;			// [OUT] Return code
	int in_usr_data;		// [IN]  User data for translating Kernel-mode address to User-mode address
	int out_buf_addr;		// [OUT] Buffer address
	int out_buf_size;		// [OUT] Size of buffer address
} MFC_GET_BUF_ADDR_ARG;


typedef union {
	int						ret_code;
	MFC_ENC_INIT_ARG		enc_init;
	MFC_ENC_EXE_ARG			enc_exe;
	MFC_DEC_INIT_ARG		dec_init;
	MFC_DEC_EXE_ARG			dec_exe;
	MFC_GET_BUF_ADDR_ARG	get_buf_addr;
} MFC_ARGS;



#endif /* __SAMSUNG_SYSLSI_APDEV_MFC_DRV_PARAMS_H__ */

