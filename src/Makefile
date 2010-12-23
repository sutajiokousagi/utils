# $Id$
# source build makefile for miscellaneous utils

include ../config/config.mk

MTD_DIR=mtd-utils
MTD_BINARIES=nandwrite nanddump flash_erase flash_eraseall \
	mkfs.jffs flash_lock flash_unlock flash_info \
	flash_otp_info flash_otp_dump mtd_debug flashcp \
	jffs2dump \
	nftldump nftl_format docfdisk \
	rfddump rfdformat \
	sumtool mkfs.jffs2
MTD_SRC_BINARIES=$(addprefix $(MTD_DIR)/,$(MTD_BINARIES))
MTD_SOURCES=$(wildcard $(MTD_DIR)/*.c $(MTD_DIR)/*.h)

FBWRITE_DIR=fbwrite
FBWRITE_BINARIES=fbwrite
FBWRITE_SRC_BINARIES=$(addprefix $(FBWRITE_DIR)/,$(FBWRITE_BINARIES))
FBWRITE_SOURCES=$(wildcard $(FBWRITE_DIR)/*.c $(FBWRITE_DIR)/*.h)

CHUMBYFLASH_DIR=chumbyflash
CHUMBYFLASH_BINARIES=badblock bbscan chumbyflash
CHUMBYFLASH_SRC_BINARIES=$(addprefix $(CHUMBYFLASH_DIR)/,$(CHUMBYFLASH_BINARIES))
CHUMBYFLASH_SOURCES=$(wildcard $(CHUMBYFLASH_DIR)/*.c $(CHUMBYFLASH_DIR)/*.h)

WGET_DIR=wget
WGET_BINARIES=wget
WGET_SRC_BINARIES=$(addprefix $(WGET_DIR)/src/,$(WGET_BINARIES))
WGET_SOURCES=$(wildcard $(WGET_DIR)/src/*.c $(WGET_DIR)/src/*.h)


# This was a bad idea - we should not have had any platform-specific binaries
# in this project.
ifeq (${CNPLATFORM},stormwind)
CAMAPP_DIR=camApp_final
CAMAPP_BINARIES=camApp_final
CAMAPP_SRC_BINARIES=$(addprefix $(CAMAPP_DIR)/,$(CAMAPP_BINARIES))
CAMAPP_SOURCES=$(wildcard $(CAMAPP_DIR)/*.c $(CAMAPP_DIR)/*.h)

MMAPP_DIR=multimedia_app
MMAPP_BINARIES=mm_app
MMAPP_SRC_BINARIES=$(addprefix ${MMAPP_DIR}/,${MMAPP_BINARIES})
MMAPP_SOURCES=$(wildcard ${MMAPP_DIR}/*.c ${MMAPP_DIR}/*.h)
else
CAMAPP_BINARIES=
CAMAPP_SRC_BINARIES=
MMAPP_BINARIES=
MMAPP_SRC_BINARIES=
endif

ifeq (${CNPLATFORM},falconwing)
SWITCHOUTPUT_SRC_BINARIES=switch_output
SWITCHOUTPUT_BINARIES=switch_output
else
SWITCHOUTPUT_SRC_BINARIES=
SWITCHOUTPUT_BINARIES=
endif

ifeq (${CNPLATFORM},falconwing)
OLFADE_SRC_BINARIES=olfade
OLFADE_BINARIES=olfade
else
OLFADE_SRC_BINARIES=
OLFADE_BINARIES=
endif


PTPLIST_DIR=ptplist
PTPLIST_BINARIES=ptplist
PTPLIST_SRC_BINARIES=$(addprefix ${PTPLIST_DIR}/,${PTPLIST_BINARIES})
PTPLIST_SOURCES=$(wildcard ${PTPLIST_DIR}/*.c ${PTPLIST_DIR}/*.h)

ifeq (${CNPLATFORM},stormwind)
PPUTILS_DIR=pp_utils
PPUTILS_BINARIES=fast-scale
PPUTILS_SRC_BINARIES=$(addprefix ${PPUTILS_DIR}/,${PPUTILS_BINARIES})
PPUTILS_SOURCES=$(wildcard ${PPUTILS_DIR}/*.c ${PPUTILS_DIR}/*.h)
else
PPUTILS_DIR=
PPUTILS_BINARIES=
PPUTILS_SRC_BINARIES=
PPUTILS_SOURCES=
endif

ROOT_BINARIES=linuxrc
ROOT_SRC_BINARIES=$(ROOT_BINARIES)
BIN_BINARIES=$(CHUMBYFLASH_BINARIES)
BIN_SRC_BINARIES=$(CHUMBYFLASH_SRC_BINARIES)
USRSBIN_BINARIES=hub-ctrl usbreset config_util progress progress_updater $(MTD_BINARIES)
USRSBIN_SRC_BINARIES=hub-ctrl usbreset config_util progress progress_updater $(MTD_SRC_BINARIES)
USRBIN_BINARIES=regutil \
	capture-date \
	tscat \
	findsyms \
	fake_mm_player \
	usbkbd \
	chumby_fbctl \
	hid_keys \
	fake_touchscreen \
	accelcat \
	bddio \
	aplaypipe \
	proc_kmsg_cat \
	$(CAMAPP_BINARIES) $(FBWRITE_BINARIES) \
	$(WGET_BINARIES) ${MMAPP_BINARIES} ${PTPLIST_BINARIES} \
	${OLFADE_BINARIES} ${SWITCHOUTPUT_BINARIES}
USRBIN_SRC_BINARIES=regutil \
	capture-date \
	tscat \
	findsyms \
	fake_mm_player \
	usbkbd \
	chumby_fbctl \
	hid_keys \
	fake_touchscreen \
	accelcat \
	bddio \
	aplaypipe \
	proc_kmsg_cat \
	$(CAMAPP_SRC_BINARIES) $(FBWRITE_SRC_BINARIES) \
	$(WGET_SRC_BINARIES) ${MMAPP_SRC_BINARIES} ${PTPLIST_SRC_BINARIES} \
	${OLFADE_SRC_BINARIES} ${SWITCHOUTPUT_SRC_BINARIES}
USRBIN_SRC_BINARIES_RFS1=$(SQLITE_SRC_BINARIES) $(PPUTILS_SRC_BINARIES)
USRLIB_SRC_LIBS_RFS1=$(SQLITE_SRC_LIBS)

# Binaries we build locally but never copy to the destination image
TESTONLY_BINARIES=timerx_test yume_fptest test_sound_output ipc_ping test_dev_shimd
TESTONLY_SRC_BINARIES=${TESTONLY_BINARIES}

SRC_BINARIES=$(BIN_SRC_BINARIES) $(USRSBIN_SRC_BINARIES) $(USRBIN_SRC_BINARIES) ${TESTONLY_SRC_BINARIES}
BINARIES=$(BIN_BINARIES) $(USRSBIN_BINARIES) $(USRBIN_BINARIES) ${TESTONLY_BINARIES}

CONFIGS=linuxrc.config
CONFIG_RFS1_SOURCES=$(addprefix ../config/rfs1/,$(CONFIGS))
CONFIG_RFS2_SOURCES=$(addprefix ../config/rfs2/,$(CONFIGS))
PLATFORM_PROFILE_INC:=../config/profile-${CNPLATFORM}-${CONFIGNAME}.inc
HAS_PLATFORM_PROFILE:=$(shell if test -f ${PLATFORM_PROFILE_INC}; then echo 1; else echo 0; fi)
ifeq (${HAS_PLATFORM_PROFILE},0)
PLATFORM_PROFILE_INC:=
endif

C_OPT_FLAGS = -Os
CFLAGS += ${C_OPT_FLAGS} -DCHUMBY_CONFIGNAME=\"$(CONFIGNAME)\" -DCHUMBY_CONFIGNAME_$(CONFIGNAME) -DCNPLATFORM_$(CNPLATFORM)

WGET_HAS_MAKEFILE:=$(shell if test -f $(WGET_DIR)/Makefile; then echo 1; else echo 0; fi)

all: $(SRC_BINARIES) $(ROOT_SRC_BINARIES)

install: $(SRC_BINARIES) $(ROOT_SRC_BINARIES) \
	.rfs1_root .rfs1_bin .rfs1_usrbin .rfs1_usrsbin .rfs1_usrlib .rfs1_config \
	.rfs2_root .rfs2_bin .rfs2_usrbin .rfs2_usrsbin .rfs2_config

.rfs1_root: $(ROOT_SRC_BINARIES) $(INSTALL_DIR)/rfs1
	@echo "Removing any possible busybox link in rfs1"
	-rm -f $(addprefix $(INSTALL_DIR)/rfs1/,$(ROOT_BINARIES))
	cp --preserve --no-dereference $(ROOT_SRC_BINARIES) $(INSTALL_DIR)/rfs1/
	@touch $@

.rfs1_bin: $(BIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/bin
	cp --preserve --no-dereference $(BIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/bin/
	@touch $@

.rfs1_usrbin: $(USRBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/usr/bin
	cp --preserve --no-dereference $(USRBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/usr/bin/
#	cp --preserve --no-dereference $(USRBIN_SRC_BINARIES_RFS1) $(INSTALL_DIR)/rfs1/usr/bin/
	@touch $@

.rfs1_usrsbin: $(USRSBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/usr/sbin
	cp --preserve --no-dereference $(USRSBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs1/usr/sbin/
	@touch $@

.rfs1_usrlib: $(USRLIB_SRC_LIBS_RFS1) $(INSTALL_DIR)/rfs1/usr/lib
ifneq (${USERLIB_SRC_LIBS_RFS1},)
	cp --preserve --no-dereference $(USRLIB_SRC_LIBS_RFS1) $(INSTALL_DIR)/rfs1/usr/lib/
endif
	@touch $@

.rfs1_config: $(CONFIG_RFS1_SOURCES) .rfs1_config_$(CONFIGNAME) $(INSTALL_DIR)/rfs1/etc/init.d ${PLATFORM_PROFILE_INC} ../config/${CNPLATFORM}.mk
	cp --preserve --no-dereference $(CONFIG_RFS1_SOURCES) $(INSTALL_DIR)/rfs1/etc/init.d/
	cat .rfs1_config_$(CONFIGNAME) >> $(INSTALL_DIR)/rfs1/etc/init.d/linuxrc.config
	sed 's/setenv/export/' .rfs1_config_${CONFIGNAME} >> ${INSTALL_DIR}/rfs1/etc/profile
ifneq (${PLATFORM_PROFILE_INC},)
	cat ${PLATFORM_PROFILE_INC} >> ${INSTALL_DIR}/rfs1/etc/profile
endif
	@touch $@

.rfs1_config_$(CONFIGNAME) .rfs2_config_$(CONFIGNAME): Makefile
	echo '# Values added by $Id$ ' > $@
	echo "setenv PATH=/usr/bin:/bin:/usr/sbin:/usr/local/sbin:/sbin:/usr/chumby/scripts" >> $@
	echo "setenv CNPLATFORM=$(CNPLATFORM)" >> $@
	echo "setenv CONFIGNAME=$(CONFIGNAME)" >> $@
	echo "setenv VIDEO_RES=$(FBWIDTH)x$(FBHEIGHT)" >> $@
	echo "setenv VIDEO_X_RES=$(FBWIDTH)" >> $@
	echo "setenv VIDEO_Y_RES=$(FBHEIGHT)" >> $@
ifneq (${DFBWIDTH},)
	echo "setenv DFB_X_RES=${DFBWIDTH}" >> $@
endif
ifneq (${DFBHEIGHT},)
	echo "setenv DFB_Y_RES=${DFBHEIGHT}" >> $@
endif
	echo "setenv HAS_CP=${HAS_CP}" >> $@
	echo "setenv LANGUAGE=${LANGUAGE}" >> $@
	echo "setenv GST_PLUGIN_PATH=/lib/gstreamer-0.10/" >> $@
	echo "setenv CAMLIBS=/lib/libgphoto2-2.4.3" >> $@
	echo "setenv IOLIBS=/lib/libgphoto2_port-0.8.0" >> $@
	echo "# End of values added to $@" >> $@

.rfs2_root: $(ROOT_SRC_BINARIES) $(INSTALL_DIR)/rfs2
	@echo "Removing any possible busybox link in rfs2"
	-rm -f $(addprefix $(INSTALL_DIR)/rfs2/,$(ROOT_BINARIES))
	cp --preserve --no-dereference $(ROOT_SRC_BINARIES) $(INSTALL_DIR)/rfs2/
	@touch $@

.rfs2_bin: $(BIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/bin
	cp --preserve --no-dereference $(BIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/bin/
	@touch $@

.rfs2_usrbin: $(USRBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/usr/bin
	cp --preserve --no-dereference $(USRBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/usr/bin/
	@touch $@

.rfs2_usrsbin: $(USRSBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/usr/sbin
	cp --preserve --no-dereference $(USRSBIN_SRC_BINARIES) $(INSTALL_DIR)/rfs2/usr/sbin/
	@touch $@

.rfs2_config: $(CONFIG_RFS2_SOURCES) .rfs2_config_$(CONFIGNAME) $(INSTALL_DIR)/rfs2/etc/init.d ${PLATFORM_PROFILE_INC} ../config/${CNPLATFORM}.mk
	cp --preserve --no-dereference $(CONFIG_RFS2_SOURCES) $(INSTALL_DIR)/rfs2/etc/init.d/
	cat .rfs2_config_$(CONFIGNAME) >> $(INSTALL_DIR)/rfs2/etc/init.d/linuxrc.config
	sed 's/setenv/export/' .rfs2_config_${CONFIGNAME} >> ${INSTALL_DIR}/rfs2/etc/profile
ifneq (${PLATFORM_PROFILE_INC},)
	cat ${PLATFORM_PROFILE_INC} >> ${INSTALL_DIR}/rfs2/etc/profile
endif
	@touch $@

$(INSTALL_DIR)/rfs1 $(INSTALL_DIR)/rfs2 \
$(INSTALL_DIR)/rfs1/bin $(INSTALL_DIR)/rfs1/usr/bin $(INSTALL_DIR)/rfs1/usr/sbin $(INSTALL_DIR)/rfs1/etc/init.d \
$(INSTALL_DIR)/rfs1/usr/lib $(INSTALL_DIR)/rfs2/usr/lib \
$(INSTALL_DIR)/rfs2/bin $(INSTALL_DIR)/rfs2/usr/bin $(INSTALL_DIR)/rfs2/usr/sbin $(INSTALL_DIR)/rfs2/etc/init.d:
ifeq ($(INSTALL_DIR),)
	error INSTALL_DIR not defined - refusing to create directories in root!
else
	@echo -n "Creating $@: "
	mkdir -p $@
endif

clean:
	-rm -f $(SRC_BINARIES) $(ROOT_SRC_BINARIES) .rfs* .wget*
	$(MAKE) -C $(MTD_DIR) CROSS=$(CROSS_COMPILE) clean
	$(MAKE) -C $(FBWRITE_DIR) clean
	$(MAKE) -C $(CHUMBYFLASH_DIR) clean
ifeq ($(WGET_HAS_MAKEFILE),1)
	$(MAKE) -C $(WGET_DIR) clean
else
	@echo "No makefile in $(WGET_DIR) - nothing to do for $@"
endif
ifeq ($(SQLITE_HAS_MAKEFILE),1)
	$(MAKE) -C $(SQLITE_DIR) clean
else
	@echo "No makefile in $(SQLITE_DIR) - nothing to do for $@"
endif
ifeq (${CNPLATFORM},stormwind)
	$(MAKE) -C $(CAMAPP_DIR) clean
	${MAKE} -C ${MMAPP_DIR} clean
endif
	${MAKE} -C ${PTPLIST_DIR} clean

linuxrc: linuxrc.cpp
	$(CROSS_COMPILE)g++ -o $@ $(CFLAGS) linuxrc.cpp

tscat: tscat.cpp
	$(CROSS_COMPILE)g++ -o $@ $(CFLAGS) tscat.cpp

usbkbd: usbkbd.cpp
	$(CROSS_COMPILE)g++ -o $@ $(CFLAGS) usbkbd.cpp

regutil: regutil.c
	$(CROSS_COMPILE)gcc -o $@ $(CFLAGS) regutil.c

switch_output: switch_output.c
	$(CROSS_COMPILE)gcc -o $@ $(CFLAGS) switch_output.c

olfade: olfade.c
	$(CROSS_COMPILE)gcc -o $@ $(CFLAGS) olfade.c

capture-date: capture-date.c
	$(CROSS_COMPILE)gcc -o $@ $(CFLAGS) capture-date.c \
        -L../../imports/libs/${TARGET}/lib -lexif \
        -I../../imports/libs/all/all/include

findsyms: findsyms.cpp
	$(CROSS_COMPILE)g++ -o $@ $(CFLAGS) findsyms.cpp

fake_mm_player: fake_mm_player.cpp
	$(CROSS_COMPILE)g++ -o $@ $(CFLAGS) fake_mm_player.cpp

usbreset: usbreset.cpp
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} usbreset.cpp

hub-ctrl: hub-ctrl.c
	${CROSS_COMPILE}gcc -o $@ ${CFLAGS} hub-ctrl.c -I../../imports/libs2/all/all/include -L../../imports/libs2/${TARGET}/lib -lusb

config_util: config_util.cpp esd_config_area.h
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} config_util.cpp
	g++ -o $@.host ${CFLAGS} config_util.cpp

progress: progress.c
	${CROSS_COMPILE}gcc -o $@ ${CFLAGS} -Ifbwrite progress.c

progress_updater: progress_updater.cpp fbwrite/fbtext.c fbwrite/fbtext.h
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} \
		-Ifbwrite -DWIDTH=${FBWIDTH} -DHEIGHT=${FBHEIGHT} progress_updater.cpp fbwrite/fbtext.c

timerx_test: timerx_test.cpp
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} -lrt timerx_test.cpp

yume_fptest: yume_fptest.cpp
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} -lm $^

aplaypipe: aplaypipe.cpp
	${CROSS_COMPILE}g++ -g -o $@ ${CFLAGS} \
		-I../../imports/libs/all/all/include \
		-L../../imports/libs/${TARGET}/lib \
		-lrt -lpthread -lasound $^
	cp $@ $@.debug
	${CROSS_COMPILE}strip $@

proc_kmsg_cat: proc_kmsg_cat.cpp
	${CROSS_COMPILE}g++ -o $@ ${CFLAGS} -lrt $^

# Catch-all rules for simple apps
%: %.cpp
	${CROSS_COMPILE}g++ -g -o $@ ${CFLAGS} -DWIDTH=${FBWIDTH} -DHEIGHT=${FBHEIGHT} $<
	cp $@ $@.debug
	${CROSS_COMPILE}strip $@

$(MTD_SRC_BINARIES): $(MTD_SOURCES)
	$(MAKE) -C $(MTD_DIR) CROSS=$(CROSS_COMPILE) FBWIDTH=$(FBWIDTH) FBHEIGHT=$(FBHEIGHT)

$(FBWRITE_SRC_BINARIES): $(FBWRITE_SOURCES)
	$(MAKE) -C $(FBWRITE_DIR) FBWIDTH=$(FBWIDTH) FBHEIGHT=$(FBHEIGHT)

$(CHUMBYFLASH_SRC_BINARIES): $(CHUMBYFLASH_SOURCES)
	$(MAKE) -C $(CHUMBYFLASH_DIR) FBWIDTH=$(FBWIDTH) FBHEIGHT=$(FBHEIGHT)

$(WGET_SRC_BINARIES): $(WGET_SOURCES) .wget-config
	$(MAKE) -C $(WGET_DIR) CFLAGS="-DWIDTH=$(FBWIDTH) -DHEIGHT=$(FBHEIGHT) -DCNPLATFORM_${CNPLATFORM}"

$(CAMAPP_SRC_BINARIES): $(CAMAPP_SOURCES)
	$(MAKE) -C $(CAMAPP_DIR) FBWIDTH=$(FBWIDTH) FBHEIGHT=$(FBHEIGHT)

${MMAPP_SRC_BINARIES}: ${MMAPP_SOURCES}
	$(MAKE) -C ${MMAPP_DIR} FBWIDTH=$(FBWIDTH) FBHEIGHT=$(FBHEIGHT)

${PTPLIST_SRC_BINARIES}: ${PTPLIST_SOURCES}
	$(MAKE) -C ${PTPLIST_DIR}

$(PPUTILS_SRC_BINARIES): $(PPUTILS_SOURCES)
	$(MAKE) -C $(PPUTILS_DIR)

.wget-config: ../output/$(TARGET)-$(CNPLATFORM)
	cd $(WGET_DIR) ; CFLAGS="-DWIDTH=$(FBWIDTH) -DHEIGHT=$(FBHEIGHT)" ./configure --host=$(TARGET) --prefix=$(abspath ../output/$(TARGET)-$(CNPLATFORM))
	touch $@

../output/$(TARGET)-$(CNPLATFORM):
	@echo "Creating directory: $@"
	-mkdir -p $@
