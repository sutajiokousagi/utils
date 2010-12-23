CC = arm-linux-gcc

#FPS means performance measurement. It doesn't make the output file
CFLAGS = -Wall -O1  #-DFPS

CSRCS = ./MPEG4Frames.c 	\
		./H264Frames.c 		\
		./VC1Frames.c 		\
		./H263Frames.c 		\
		./FrameExtractor.c 	\
		./line_buf_test.c 	\
		./ring_buf_test.c 	\
		./encoder_test.c 	\
		./display_test.c 	\
		./demo.c			\
		./performance.c 	\
		./test.c 

OBJS = $(CSRCS:.c=.o)

.SUFFIXES:.c.o

all:	mfc_file

mfc_file: $(OBJS)
	$(CC) $(CFLAGS) -g -o $@ $(OBJS)
	
.c.o:
	$(CC) $(CFLAGS) -g -c -o $@ $<

clean:
	rm -f mfc $(OBJS) 
