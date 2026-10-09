# Project Name
TARGET = FRIZZ

# make BENCH=1: FRIZZ-bench.bin, which measures the audio callback's load (Bench.h), built
# apart in build-bench/ so it never mixes with the normal build's objects
ifeq ($(BENCH),1)
TARGET = FRIZZ-bench
# cpu.txt names the source it was built from by a checksum of it, not by the time, so a
# rebuild of the same source gives the same FRIZZ-bench.bin
BENCH_SOURCE := $(shell cat $(sort $(wildcard *.h *.cpp)) | cksum | cut -d' ' -f1)
C_DEFS = -DFRIZZ_BENCH=1 -DFRIZZ_BENCH_SOURCE='"$(BENCH_SOURCE)"'
override BUILD_DIR = build-bench
endif

OPT=-O3

APP_TYPE=BOOT_SRAM
# make program-boot installs CHOMPI's own bootloader, not libDaisy's generic one
BOOT_BIN = ../../bin/CHOMPI_Bootloader_V6_2_0.bin
LDSCRIPT = chompi_sram.lds

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

# FatFs: the FX scenes and master settings on the card (SceneStore.h)
USE_FATFS = 1

# Libs
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
