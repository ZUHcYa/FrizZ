# Project Name
TARGET = FRIZZ

DEBUG=0
# OPT=-O0
OPT=-O3

APP_TYPE=BOOT_SRAM
# make program-boot installs CHOMPI's own bootloader, not libDaisy's generic one
BOOT_BIN = ../../bin/CHOMPI_Bootloader_V6_2_0.bin
LDSCRIPT = chompi_sram.lds

LDSCRIPT=chompi_sram.lds

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp

# FatFs is only used by the hardware self-test's SD card check (TestPage)
USE_FATFS = 1

# Libs
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
