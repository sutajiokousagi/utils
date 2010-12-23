CC = arm-linux-gcc

#FPS means performance measurement. It doesn't make the output file
CFLAGS = -Wall -O1 

CSRCS = ./MPEG4Frames.c 		\
		./H264Frames.c 			\
		./VC1Frames.c 			\
		./H263Frames.c 			\
		./FrameExtractor.c 		\
		./line_buf_test.c 		\
		./ring_buf_test.c 		\
		./display_test.c		\
		./encoder_test.c		\
		./SsbSipH264Decode.c 	\
		./SsbSipMpeg4Decode.c 	\
		./SsbSipVC1Decode.c 	\
		./SsbSipMfcDecode.c		\
		./SsbSipH264Encode.c	\
		./SsbSipMpeg4Encode.c	\
		./SsbSipLogMsg.c 		\
		./performance.c 		\
		./test.c 

OBJS = $(CSRCS:.c=.o)

.SUFFIXES:.c.o

all:	mfc_api

mfc_api: $(OBJS)
	$(CC) $(CFLAGS) -g -o $@ $(OBJS)
	
.c.o:
	$(CC) $(CFLAGS) -g -c -o $@ $<

clean:
	rm -f mfc $(OBJS) 
