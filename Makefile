PROG     = jornada720
HEADLESS = headless
APP      = Jornada720.app
ARMSTEP  = $(BUILD)/arm-step
RAPI_TOOL = jornada-rapi
JORNADA_TOOLS ?= ../jornada-72x-tools
NATIVECHECK = $(BUILD)/native-check
BUILD    = build
PYTHON  ?= python3
UNCRUSTIFY ?= uncrustify

.DEFAULT_GOAL := all

PKG_CONFIG ?= pkg-config

CFLAGS += -Isrc -I$(BUILD) -Wall -Wextra -O2 -std=c11 -fno-common -MMD -MP
ifeq ($(SDL_STATIC),1)
SDL_PKG_CONFIG = $(PKG_CONFIG) --static
else
SDL_PKG_CONFIG = $(PKG_CONFIG)
endif

SDL_CFLAGS  = $(shell $(SDL_PKG_CONFIG) --cflags sdl3)
THREAD_LIBS = -lpthread
LDFLAGS    += $(shell $(SDL_PKG_CONFIG) --libs sdl3) -lm -lz $(THREAD_LIBS)

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
MENU     ?= macos
else
MENU     ?= bar
CFLAGS   += -D_GNU_SOURCE
endif
ifeq ($(MENU),macos)
SRC_MENU  = src/frontend/macos/menu.m src/frontend/macos/dialog.m
LDFLAGS  += -framework Cocoa
else ifeq ($(MENU),android)
SRC_MENU  = src/frontend/android/menu.c src/frontend/android/dialog.c src/frontend/android/keystrip.c src/frontend/android/list.c src/frontend/android/text.c src/frontend/android/toast.c src/frontend/common/menu_state.c src/frontend/android/android.c src/vendor/truetype.c
else
SRC_MENU  = src/frontend/linux/menu.c src/frontend/linux/dialog.c src/frontend/linux/ui.c src/frontend/common/menu_state.c src/vendor/truetype.c
endif

ifeq ($(shell $(PKG_CONFIG) --exists slirp && echo yes),yes)
SRC_NET  = src/util/socket_path.c src/net/net_gateway.c src/net/ppp_gateway.c src/net/ethernet_gateway.c src/net/ethernet_link.c src/net/serial_link.c
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
SRC_NET  = src/util/socket_path.c src/net/net_gateway_none.c src/net/ppp_gateway.c src/net/ethernet_gateway.c src/net/ethernet_link.c src/net/web_proxy_none.c src/net/serial_link.c
endif

SRC_MACHINE = src/core/agent.c src/core/arm.c src/core/ce.c src/core/cfcard.c src/core/pccard.c src/core/ne2000.c src/core/gdb.c src/core/mailbox.c src/core/optimiser.c src/native/native.c src/core/sa1110.c src/core/sa1111.c src/core/sed1356.c src/core/eeprom.c src/core/mcu.c src/core/key_text.c src/core/machine.c src/core/state.c src/util/png.c
SRC_RAPI    = src/rapi/rapi.c src/rapi/rapi_load.c src/rapi/rapi_setup.c src/rapi/rapi_sync.c src/rapi/debugmgr_image.c
SRC_APP     = $(SRC_MACHINE) $(SRC_NET) $(SRC_RAPI) src/core/screen.c src/util/options.c src/util/file.c src/app/capture.c src/app/commands.c src/app/desktop.c src/app/host.c src/app/input.c src/app/launch.c src/app/library.c src/app/log.c src/app/machine_session.c src/app/notices.c src/app/paths.c src/app/picks.c src/app/profiles.c src/app/rom_catalog.c src/app/runner.c src/app/serial_service.c src/app/settings.c src/app/snapshot_store.c src/app/typer.c src/app/view.c src/app/main.c src/frontend/common/dialog.c src/frontend/common/menu_queue.c $(SRC_MENU)

OBJ_APP      = $(patsubst %.m,$(BUILD)/%.o,$(SRC_APP:%.c=$(BUILD)/%.o))
OBJ_HEADLESS = $(SRC_MACHINE:%.c=$(BUILD)/%.o) $(SRC_NET:%.c=$(BUILD)/%.o) $(BUILD)/tools/headless.o

C_STYLE_SOURCES = $(filter-out src/vendor/%,$(shell git ls-files 'src/**/*.c' 'src/**/*.h' 'tools/*.c'))

all: $(HEADLESS) $(ARMSTEP) $(PROG) $(RAPI_TOOL)

$(PROG): $(OBJ_APP)
	$(CC) -o $@ $^ $(LDFLAGS) $(NET_LIBS)

$(BUILD)/libmain.so: $(OBJ_APP)
	$(CC) -shared -Wl,--no-undefined -o $@ $^ $(LDFLAGS) $(NET_LIBS)

$(BUILD)/src/app/%.o $(BUILD)/src/frontend/%.o: CFLAGS += $(SDL_CFLAGS)
$(BUILD)/src/vendor/%.o: CFLAGS += -w

$(HEADLESS): $(OBJ_HEADLESS)
	$(CC) -o $@ $^ -lm -lz $(THREAD_LIBS) $(NET_LIBS)

$(RAPI_TOOL): $(SRC_RAPI:%.c=$(BUILD)/%.o) $(BUILD)/src/util/options.o $(BUILD)/tools/jornada_rapi.o
	$(CC) -o $@ $^

ICON_TOOL  = $(BUILD)/icon
ICON_SIZES = 16 32 64 128 256 512 1024

$(ICON_TOOL): $(BUILD)/tools/icon.o $(BUILD)/src/util/png.o $(BUILD)/src/vendor/svg.o
	$(CC) -o $@ $^ -lm -lz

icons: $(ICON_TOOL) assets/icon.svg
	@mkdir -p $(BUILD)/icons
	@for size in $(ICON_SIZES); do $(ICON_TOOL) assets/icon.svg $$size $(BUILD)/icons/icon-$$size.png || exit 1; done

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

$(BUILD)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c11,$(CFLAGS)) $(SDL_CFLAGS) -fobjc-arc -c -o $@ $<

app: $(PROG) $(RAPI_TOOL) icons
	ICONS=$(BUILD)/icons sh tools/mkapp.sh $(APP)

CARD       ?= card.img
CARD_MB    ?= 64
CARD_FILES ?=

card:
	sh tools/mkcard.sh $(CARD) $(CARD_MB) $(CARD_FILES)

deb:
	sh tools/mkdeb.sh dist

apk: icons
	ICONS=$(BUILD)/icons sh tools/mkapk.sh

apk-push:
	sh tools/mkapk.sh push

apk-install: icons
	ICONS=$(BUILD)/icons sh tools/mkapk.sh install

test: $(PROG) $(HEADLESS) $(NATIVECHECK)
	./$(NATIVECHECK)
	tests/boot.sh

difftest: $(ARMSTEP)
	$(PYTHON) tests/arm_difftest.py $(ARMSTEP)

format:
	$(UNCRUSTIFY) -c .uncrustify.cfg --replace --no-backup $(C_STYLE_SOURCES)

format-check:
	$(UNCRUSTIFY) -c .uncrustify.cfg --check $(C_STYLE_SOURCES)

clean:
	rm -rf $(BUILD) $(HEADLESS) $(PROG) $(RAPI_TOOL) $(APP) dist

.PHONY: all app apk apk-install apk-push card clean deb debugmgr difftest format format-check icons native-check test FORCE

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
