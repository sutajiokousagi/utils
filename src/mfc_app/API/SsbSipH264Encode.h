#ifndef __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPH264ENCODE_H__
#define __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPH264ENCODE_H__


typedef unsigned int	H264_ENC_CONF;


#ifdef __cplusplus
extern "C" {
#endif


void *SsbSipH264EncodeInit(unsigned int uiWidth,     unsigned int uiHeight,
                           unsigned int uiFramerate, unsigned int uiBitrate_kbps,
                           unsigned int uiGOPNum);
int   SsbSipH264EncodeExe(void *openHandle);
int   SsbSipH264EncodeDeInit(void *openHandle);

int   SsbSipH264EncodeSetConfig(void *openHandle, H264_ENC_CONF conf_type, void *value);
int   SsbSipH264EncodeGetConfig(void *openHandle, H264_ENC_CONF conf_type, void *value);


void *SsbSipH264EncodeGetInBuf(void *openHandle, long size);
void *SsbSipH264EncodeGetOutBuf(void *openHandle, long *size);



#ifdef __cplusplus
}
#endif


// Error codes
#define SSBSIP_H264_ENC_RET_OK						(0)
#define SSBSIP_H264_ENC_RET_ERR_INVALID_HANDLE		(-1)
#define SSBSIP_H264_ENC_RET_ERR_INVALID_PARAM		(-2)

#define SSBSIP_H264_ENC_RET_ERR_ENCODE_FAIL			(-101)


#endif /* __SAMSUNG_SYSLSI_APDEV_MFCLIB_SSBSIPH264ENCODE_H__ */

