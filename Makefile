.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error Run scripts/build.sh or set DEVKITARM to your devkitARM installation)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules
.RECIPEPREFIX := >

TARGET := 3DSLink
BUILD := build
SOURCES := source third_party/qrcodegen
INCLUDES := include third_party/qrcodegen
ROMFS := romfs
APP_TITLE := 3DSLink
APP_DESCRIPTION := SD card file sharing
APP_AUTHOR := 3DSLink
ICON := meta/icon.png

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export LD := $(CXX)
export OFILES := Main.o Server.o ConsoleUi.o qrcodegen.o
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(CURDIR)/$(ICON)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS := --smdh=$(OUTPUT).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean
all: $(BUILD)
>    @$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
>    @mkdir -p $@

clean:
>    rm -rf build 3DSLink.elf 3DSLink.3dsx 3DSLink.smdh

else

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS) $(wildcard $(TOPDIR)/romfs/web/*) $(wildcard $(TOPDIR)/romfs/fonts/*)
$(OUTPUT).elf: $(OFILES)
qrcodegen.o: CFLAGS += -Wno-type-limits
-include $(DEPSDIR)/*.d

endif
