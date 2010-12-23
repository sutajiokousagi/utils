# $Id$
# utility wrapper makefile

ifeq (${METAPROJECT_ROOT},)
export METAPROJECT_ROOT:=$(abspath ../..)
endif

include ${METAPROJECT_ROOT}/config/config.mk

export PLATFORM_TARGET=$(TARGET)-$(CNPLATFORM)

EXPORTS=$(abspath ../exports)
OUTPUT=$(abspath ../output/$(PLATFORM_TARGET))
TARGET_EXPORTS=$(EXPORTS)/$(PLATFORM_TARGET)
LINUX_FROM_SRC=${LINUX_DIR}

all:
	$(MAKE) -C ../src KERNDIR=$(abspath $(LINUX_FROM_SRC))

install:
	$(MAKE) -C ../src install

help:
	@echo "Meta-targets:"
	@echo "help build-all clean-all commit"
	@echo "Build process uses:"
	@echo " make -C build clean-all PRODUCTION=1"
	@echo " make -C build build-all PRODUCTION=1"
	@echo " make -C build commit PRODUCTION=1"
	@echo "Utility targets:"
	@echo "diff trunk-merge bumpbuild"

clean:
	$(MAKE) -C ../src clean KERNDIR=$(abspath $(LINUX_FROM_SRC))

exports:

clean-exports:

build-all:
	$(MAKE) install KERNDIR=$(abspath $(LINUX_FROM_SRC))

clean-all:
	$(MAKE) clean
	$(MAKE) clean-exports


.PHONY: all help clean exports clean-exports build-all clean-all

