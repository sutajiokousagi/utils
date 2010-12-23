
# -*- sh -*-

# $Id: Makefile,v 1.60 2005/11/07 11:15:09 gleixner Exp $
# henry@chumby.com - There are local mods to this automake-generated
# makefile, otherwise we end up with incorrect linkage using a
# standard toolchain install.

SBINDIR=/usr/sbin
MANDIR=/usr/man
INCLUDEDIR=/usr/include
#CROSS=arm-linux-
CC := $(CROSS)gcc
LD := $(CROSS)ld
FBWIDTH=320
FBHEIGHT=240
CFLAGS := -I$(KERNDIR)/include -O2 -Iinclude -D__user= -Wall -DWIDTH=$(FBWIDTH) -DHEIGHT=$(FBHEIGHT) -DCHUMBY_PLATFORM=$(CNPLATFORM) -DCHUMBY_PLATFORM_$(CNPLATFORM)
JFFS2_CFLAGS := -DWITHOUT_XATTR -DWITHOUT_LZO -I$(KERNDIR)/fs/jffs2 -I../../../imports/libs/all/all/include
JFFS2_LDFLAGS := -L../../../imports/libs/$(TARGET)/lib

TARGETS = flash_erase flash_eraseall nanddump \
	mkfs.jffs flash_lock flash_unlock flash_info \
	flash_otp_info flash_otp_dump mtd_debug flashcp nandwrite \
	jffs2dump \
	nftldump nftl_format docfdisk \
	rfddump rfdformat \
	sumtool mkfs.jffs2 # jffs2reader

BROKEN_TARGETS = ftl_format ftl_check

NOBUILD_TARGETS = $(BROKEN_TARGETS) doc_loadbios

SYMLINKS =

%: %.o
	$(CC) $(LDFLAGS) $(JFFS2_LDFLAGS) -g -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(JFFS2_CFLAGS) -g -c -o $@ $< -g -Wp,-MD,.$<.dep

.SUFFIXES:

all: $(TARGETS)

IGNORE=${wildcard .*.c.dep}
-include ${IGNORE}

clean:
	rm -f *.o $(TARGETS) .*.c.dep $(SYMLINKS)

$(SYMLINKS):
	ln -sf ../fs/jffs2/$@ $@

mkfs.jffs2: crc32.o compr_rtime.o mkfs.jffs2.o compr_zlib.o compr.o rbtree.o
	$(CC) $(LDFLAGS) $(JFFS2_LDFLAGS) -o $@ $^ -lz

flash_eraseall: crc32.o flash_eraseall.o
	$(CC) $(LDFLAGS) -o $@ $^ fbtext.c

jffs2reader: jffs2reader.o
	$(CC) $(LDFLAGS) $(JFFS2_LDFLAGS) -o $@ $^ -lz

jffs2dump: jffs2dump.o crc32.o
	$(CC) $(LDFLAGS) -o $@ $^

sumtool: sumtool.o crc32.o
	$(CC) $(LDFLAGS) -o $@ $^

install: ${TARGETS}
	mkdir -p ${DESTDIR}/${SBINDIR}
	install -m0755 ${TARGETS} ${DESTDIR}/${SBINDIR}/
	mkdir -p ${DESTDIR}/${MANDIR}/man1
	gzip -c mkfs.jffs2.1 > ${DESTDIR}/${MANDIR}/man1/mkfs.jffs2.1.gz
