# $Id$
# ken@chumby.com

CROSS_COMPILE=${TARGET}-
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)ld
FBWIDTH=320
FBHEIGHT=240
# henry@chumby.com - removed -I/usr/src/linux/include
CFLAGS += -O2 -DWIDTH=$(FBWIDTH) -DHEIGHT=$(FBHEIGHT) -DCNPLATFORM_${CNPLATFORM} -DCONFIGNAME_${CONFIGNAME} fbtext.c

TARGETS = fbwrite

all: $(TARGETS)

fbwrite: fbwrite.c fbtext.c fbtext.h $(wildcard font*.c)
	${CC} ${CFLAGS} fbwrite.c -o $@

clean:
	rm -f *.o $(TARGETS)
