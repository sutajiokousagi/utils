# $Id$
# config.mk - project-specific configuration details

ifeq (${CNPLATFORM},)
export CNPLATFORM=stormwind
endif
ifeq ($(CONFIGNAME),)
CONFIGNAME=$(CNPLATFORM)
endif
export ARCH=arm
export TARGET=$(ARCH)-linux
export CROSS_COMPILE=$(TARGET)-

include ../config/$(CONFIGNAME).mk
