# RadioScan build
#   make                 desktop build (needs SDL 1.2 dev files) -> build/desktop/radioscan
#   make miyoo           cross build for Miyoo Mini / Mini Plus  -> build/miyoo/radioscan
#   make package         miyoo build + dist/RadioScan.zip ready to unzip onto the SD card
#   make install SD=/path/to/card   copy the packaged app onto a mounted OnionOS card
#
# For `make miyoo`, use the OnionOS / union-miyoomini toolchain (see README), or set CROSS and SYSROOT.

TARGET   ?= desktop
CROSS    ?= arm-linux-gnueabihf-
BUILD    := build/$(TARGET)
APP      := radioscan
SRC      := src/main.c src/dsp.c src/stream.c src/net.c src/stations.c src/sync.c
MBED_DIR := third_party/mbedtls
MBED_SRC := $(wildcard $(MBED_DIR)/library/*.c)
MBED_OBJ := $(patsubst $(MBED_DIR)/library/%.c,$(BUILD)/mbedtls/%.o,$(MBED_SRC))
OBJ      := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

CFLAGS   ?= -O2
CFLAGS   += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$(MBED_DIR)/include
MBED_CFLAGS := -O2 -I$(MBED_DIR)/include -I$(MBED_DIR)/library

ifeq ($(TARGET),miyoo)
  CC      := $(CROSS)gcc
  AR      := $(CROSS)ar
  SYSROOT ?= $(shell $(CC) -print-sysroot)
  ARCH    := -marm -mtune=cortex-a7 -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
  CFLAGS  += $(ARCH) -I$(SYSROOT)/usr/include -D_GNU_SOURCE
  MBED_CFLAGS += $(ARCH)
  LDLIBS  := -lSDL -lpthread -lm
else
  CFLAGS  += $(shell sdl-config --cflags | sed 's|-I\([^ ]*\)/SDL|-I\1|')
  LDLIBS  := $(shell sdl-config --libs) -lpthread -lm
endif

all: $(BUILD)/$(APP)

$(BUILD)/$(APP): $(OBJ) $(BUILD)/libmbedtls.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(BUILD)/libmbedtls.a $(LDLIBS)

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/libmbedtls.a: $(MBED_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/mbedtls/%.o: $(MBED_DIR)/library/%.c | $(BUILD)/mbedtls
	$(CC) $(MBED_CFLAGS) -c -o $@ $<

$(BUILD) $(BUILD)/mbedtls:
	mkdir -p $@

miyoo:
	$(MAKE) TARGET=miyoo

package: miyoo
	@test -f build/miyoo/$(APP)
	rm -rf dist/RadioScan && mkdir -p dist
	cp -r App/RadioScan dist/RadioScan
	cp build/miyoo/$(APP) dist/RadioScan/$(APP)
	$(CROSS)strip dist/RadioScan/$(APP) 2>/dev/null || true
	rm -f dist/RadioScan/settings.cfg dist/RadioScan/log.txt
	cd dist && rm -f RadioScan.zip && zip -qr RadioScan.zip RadioScan
	@echo "dist/RadioScan.zip ready: unzip into /mnt/SDCARD/App/ on the Miyoo"

install: package
	@test -n "$(SD)" || (echo "usage: make install SD=/path/to/mounted/sdcard" && exit 1)
	@test -d "$(SD)/App" || (echo "$(SD)/App not found - is this the OnionOS card?" && exit 1)
	mkdir -p "$(SD)/App/RadioScan"
	cp -r dist/RadioScan/. "$(SD)/App/RadioScan/"
	sync

clean:
	rm -rf build dist

.PHONY: all miyoo package install clean
