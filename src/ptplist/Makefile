# ken@chumby.com

IMPORT_DIR:=$(abspath ../../../imports)
LIBGPHOTO2_DIR:=$(IMPORT_DIR)/libs2
LIBGPHOTO2_CFLAGS=-I$(LIBGPHOTO2_DIR)/all/all/include
LIBGPHOTO2_LDFLAGS=-L$(LIBGPHOTO2_DIR)/${TARGET}/lib -lgphoto2 -lgphoto2_port -lltdl

# Defined portably in ../../../config/config.mk
#CROSS_COMPILE=arm-linux-
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)ld
CFLAGS += -O2 $(LIBGPHOTO2_CFLAGS) $(LIBGPHOTO2_LDFLAGS)

TARGETS = ptplist

all: $(TARGETS)

clean:
	rm -f *.o $(TARGETS)
