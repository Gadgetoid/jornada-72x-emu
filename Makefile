HEADLESS = headless
APP      = jornada720
ARMSTEP  = $(BUILD)/arm-step
NATIVECHECK = $(BUILD)/native-check
BUILD    = build
PYTHON  ?= python3
UNCRUSTIFY ?= uncrustify

CFLAGS += -Isrc -Wall -Wextra -O2 -std=c11 -fno-common -MMD -MP

SRC_MACHINE = src/core/arm.c src/core/optimiser.c src/native/native.c src/core/sa1110.c src/core/sa1111.c src/core/sed1356.c src/core/eeprom.c src/core/mcu.c src/core/machine.c src/core/state.c src/util/png.c

C_STYLE_SOURCES = $(shell git ls-files 'src/**/*.c' 'src/**/*.h' 'tools/*.c')

.DEFAULT_GOAL := all

SDL_CFLAGS = $(shell pkg-config --cflags sdl3)
SDL_LIBS   = $(shell pkg-config --libs sdl3)

all: $(HEADLESS) $(ARMSTEP) $(APP)

$(APP): $(SRC_MACHINE:%.c=$(BUILD)/%.o) $(BUILD)/src/app/main.o
	$(CC) -o $@ $^ -lz $(SDL_LIBS)

$(BUILD)/src/app/%.o: CFLAGS += $(SDL_CFLAGS)

$(HEADLESS): $(SRC_MACHINE:%.c=$(BUILD)/%.o) $(BUILD)/tools/headless.o
	$(CC) -o $@ $^ -lz

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
	rm -rf $(BUILD) $(HEADLESS) $(APP)

.PHONY: all clean difftest format format-check native-check test

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
