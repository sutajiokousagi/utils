
FAST_SCALE_OBJECTS = fast-scale.o \
          jpeg.o \
          postprocessor.o \
          JPGApi.o
ifeq (${HW_ROTATOR},1)
    FAST_SCALE_OBJECTS += rotator-hw.o
else
    FAST_SCALE_OBJECTS += rotator-sw.o
endif


OBJECTS=$(FAST_SCALE_OBJECTS)

SRCS = $(OBJECTS:.o=.c)
DEPENDENCY = lcd.h 				\
		     LogMsg.h 			\
		     mfc.h 				\
		     MfcDriver.h 		\
		     MfcDrvParams.h 	\
		     performance.h 		\
			 post.h				\
			 videodev2.h		\
			 videodev2_s3c.h	


LIBDIR = ../../../imports/libs/${TARGET}/lib
INCDIR = ../../../imports/libs/all/all/include
CC = $(TARGET)-gcc
CFLAGS = -g -O0 -Wall -I$(INCDIR) -DUSE_LIB_EXIF
LDFLAGS = -L$(LIBDIR) -lexif
SOURCES = $($(FAST_SCALE_OBJECTS):.c=.o)


ifeq (${LIBJPEG},0)
else
    CFLAGS += -DUSE_LIB_JPEG
    LDFLAGS += $(LIBDIR)/libjpeg.a
endif

all: fast-scale

fast-scale: $(FAST_SCALE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(FAST_SCALE_OBJECTS) $(LDFLAGS)

#$(TARGET) : $(OBJECTS)
#		$(CC) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

.c.o:
		$(CC) -c $(CFLAGS) $< -o $(basename $<).o

clean :
		rm -rf $(OBJECTS) $(TARGET) core

#performance.o : $(DEPENDENCY) performance.c
#LogMsg.o : $(DEPENDENCY) LogMsg.c
#
