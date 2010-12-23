# $Id$
# Makefile for chumbyflash
# ken@chumby.com

CROSS_COMPILE=${TARGET}-
CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)ld
FBWIDTH=320
FBHEIGHT=240
#-I$(wildcard ../../../linux-*)/src/include
CFLAGS += -I$(KERNDIR)/include -O2 -D__user= -DWIDTH=$(FBWIDTH) -DHEIGHT=$(FBHEIGHT) fbtext.c

TARGETS = chumbyflash bbscan badblock

all: $(TARGETS)

clean:
	rm -f *.o $(TARGETS)
