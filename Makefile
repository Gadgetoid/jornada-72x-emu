HEADLESS = headless
APP      = jornada720
ARMSTEP  = $(BUILD)/arm-step
RAPI_TOOL = jornada-rapi
JORNADA_TOOLS ?= ../jornada-72x-tools
NATIVECHECK = $(BUILD)/native-check
BUILD    = build
PYTHON  ?= python3
UNCRUSTIFY ?= uncrustify

CFLAGS += -Isrc -I$(BUILD) -Wall -Wextra -O2 -std=c11 -fno-common -MMD -MP

SRC_APP = src/app/desktop.c src/app/serial_service.c src/app/input.c src/app/launch.c src/app/notices.c src/app/paths.c src/app/view.c src/app/main.c

SRC_MACHINE = src/core/agent.c src/core/arm.c src/core/mailbox.c src/core/optimiser.c src/native/native.c src/core/sa1110.c src/core/sa1111.c src/core/sed1356.c src/core/eeprom.c src/core/mcu.c src/core/machine.c src/core/state.c src/util/png.c

C_STYLE_SOURCES = $(filter-out src/vendor/%,$(shell git ls-files 'src/**/*.c' 'src/**/*.h' 'tools/*.c'))
PKG_CONFIG ?= pkg-config

ifeq ($(shell $(PKG_CONFIG) --exists slirp && echo yes),yes)
SRC_NET  = src/net/net_gateway.c src/net/ppp_gateway.c src/net/ethernet_gateway.c src/net/serial_link.c
CFLAGS  += $(shell $(PKG_CONFIG) --cflags slirp)
NET_LIBS = $(shell $(PKG_CONFIG) --libs slirp)
ifeq ($(shell $(PKG_CONFIG) --exists libcurl && echo yes),yes)
SRC_NET  += src/net/web_proxy.c src/net/web_image.c src/vendor/image.c src/vendor/svg.c
CFLAGS   += $(shell $(PKG_CONFIG) --cflags libcurl)
NET_LIBS += $(shell $(PKG_CONFIG) --libs libcurl)
else
SRC_NET  += src/net/web_proxy_none.c
endif
else
SRC_NET  = src/net/net_gateway_none.c src/net/ppp_gateway.c src/net/ethernet_gateway.c src/net/web_proxy_none.c src/net/serial_link.c
endif

SRC_RAPI = src/rapi/rapi.c src/rapi/rapi_load.c src/rapi/rapi_setup.c src/rapi/rapi_sync.c src/rapi/debugmgr_image.c

.DEFAULT_GOAL := all

SDL_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL_LIBS   = $(shell pkg-config --libs sdl3)

all: $(HEADLESS) $(ARMSTEP) $(APP) $(RAPI_TOOL)

$(APP): $(SRC_MACHINE:%.c=$(BUILD)/%.o) $(SRC_NET:%.c=$(BUILD)/%.o) $(SRC_RAPI:%.c=$(BUILD)/%.o) $(SRC_APP:%.c=$(BUILD)/%.o)
	$(CC) -o $@ $^ -lm -lz -lpthread $(NET_LIBS) $(SDL_LIBS)

$(BUILD)/src/app/%.o: CFLAGS += $(SDL_CFLAGS)
$(BUILD)/src/vendor/%.o: CFLAGS += -w

$(HEADLESS): $(SRC_MACHINE:%.c=$(BUILD)/%.o) $(SRC_NET:%.c=$(BUILD)/%.o) $(BUILD)/tools/headless.o
	$(CC) -o $@ $^ -lm -lz $(NET_LIBS)

$(RAPI_TOOL): $(SRC_RAPI:%.c=$(BUILD)/%.o) $(BUILD)/src/util/options.o $(BUILD)/tools/jornada_rapi.o
	$(CC) -o $@ $^

VERSION ?= $(shell git describe --always --dirty 2>/dev/null || echo unknown)

$(BUILD)/version.h: FORCE
	@mkdir -p $(BUILD)
	@printf '#define JORNADA_VERSION "%s"\n' "$(VERSION)" > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp

$(BUILD)/src/util/options.o: $(BUILD)/version.h

debugmgr:
	$(MAKE) -C $(JORNADA_TOOLS) debugmgr
	$(PYTHON) tools/mkdebugmgr.py $(JORNADA_TOOLS)/build/debugmgr/debugmgr.exe src/rapi/debugmgr_image.c

$(ARMSTEP): $(BUILD)/src/core/arm.o $(BUILD)/tools/arm_step.o
	$(CC) -o $@ $^ -lz

$(NATIVECHECK): $(BUILD)/src/core/arm.o $(BUILD)/src/native/native.o $(BUILD)/tools/native_check.o
	$(CC) -o $@ $^

native-check: $(NATIVECHECK)
	./$(NATIVECHECK)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(APP) $(HEADLESS) $(NATIVECHECK)
	./$(NATIVECHECK)
	tests/boot.sh

difftest: $(ARMSTEP)
	$(PYTHON) tests/arm_difftest.py $(ARMSTEP)

format:
	$(UNCRUSTIFY) -c .uncrustify.cfg --replace --no-backup $(C_STYLE_SOURCES)

format-check:
	$(UNCRUSTIFY) -c .uncrustify.cfg --check $(C_STYLE_SOURCES)

clean:
	rm -rf $(BUILD) $(HEADLESS) $(APP) $(RAPI_TOOL)

.PHONY: all clean debugmgr difftest format format-check native-check test FORCE

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
