#ifndef __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPMPEG4ENCODE_H__
#define __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPMPEG4ENCODE_H__


#define SSBSIPMFCENC_MPEG4		0x3035
#define SSBSIPMFCENC_H263		0x3036

typedef enum {
	SET_H263_MULTIPLE_SLICE
}MPEG4_ENC_CONF;


#ifdef __cplusplus
extern "C" {
#endif


void *SsbSipMPEG4EncodeInit(int strmType, unsigned int uiWidth,     unsigned int uiHeight,
                            unsigned int uiFramerate, unsigned int uiBitrate_kbps,
                            unsigned int uiGOPNum);
int   SsbSipMPEG4EncodeExe(void *openHandle);
int   SsbSipMPEG4EncodeDeInit(void *openHandle);

int   SsbSipMPEG4EncodeSetConfig(void *openHandle, MPEG4_ENC_CONF conf_type, void *value);
int   SsbSipMPEG4EncodeGetConfig(void *openHandle, MPEG4_ENC_CONF conf_type, void *value);


void *SsbSipMPEG4EncodeGetInBuf(void *openHandle, long size);
void *SsbSipMPEG4EncodeGetOutBuf(void *openHandle, long *size);



#ifdef __cplusplus
}
#endif


// Error codes
#define SSBSIP_MPEG4_ENC_RET_OK						(0)
#define SSBSIP_MPEG4_ENC_RET_ERR_INVALID_HANDLE		(-1)
#define SSBSIP_MPEG4_ENC_RET_ERR_INVALID_PARAM		(-2)

#define SSBSIP_MPEG4_ENC_RET_ERR_ENCODE_FAIL		(-101)

#endif /* __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPMPEG4ENCODE_H__ */

