# Builds the real SupaChat firmware against the emulator's shims, and runs the scenarios.
#   make check                run every scenario against FIRMWARE
#   make FIRMWARE=dir build   build one firmware tree (TAG names its output folder)
#   SAN=asan or SAN=tsan      build with that sanitizer
HOME_PIO   ?= $(HOME)/.platformio
FIRMWARE   ?= $(HOME)/Work/cardputer-adv-supachat/firmware
LIBDEPS    ?= $(HOME)/Work/cardputer-adv-supachat/firmware/.pio/libdeps/cardputer-adv
SECRETS_DIR ?= $(HOME)/Work/cardputer-adv-supachat/firmware/include
CORE       := $(HOME_PIO)/packages/framework-arduinoespressif32/cores/esp32
ENV        ?= cardputer-adv
TAG        ?= current
SAN        ?=
DEVICE_DEFS ?=
ELF        := $(FIRMWARE)/.pio/build/$(ENV)/firmware.elf
NM         := $(HOME_PIO)/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm
HEAP_START := $(shell $(NM) $(ELF) 2>/dev/null | awk '$$3=="_heap_start"{print "0x"$$1}')
ifeq ($(HEAP_START),)
HEAP_START := 0
endif

SANFLAGS :=
ifeq ($(SAN),asan)
SANFLAGS := -fsanitize=address,undefined -fno-sanitize-recover=undefined
endif
ifeq ($(SAN),tsan)
SANFLAGS := -fsanitize=thread
endif
BUILD    := build/$(TAG)$(if $(SAN),-$(SAN))
LIBBUILD := build/libs$(if $(SAN),-$(SAN))

M5GFX    := $(LIBDEPS)/M5GFX/src
INCLUDES := -Ishim -Isrc -I$(CORE) -I$(M5GFX) -I$(LIBDEPS)/M5Cardputer/src -I$(LIBDEPS)/ArduinoJson/src -I$(LIBDEPS)/libhelix/src -I$(LIBDEPS)/libhelix/src/libhelix-mp3
DEFINES  := -DARDUINO=10819 -DLGFX_SDL -DESP_NOW_ETH_ALEN=6
COMMON   := -g -O1 -fno-omit-frame-pointer -funsigned-char -w $(SANFLAGS) $(DEFINES) $(INCLUDES)
CXXFLAGS := $(COMMON) -std=gnu++17 -fexceptions
CFLAGS   := $(COMMON) -std=gnu11
LDFLAGS  := $(SANFLAGS) -rdynamic -pthread -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc,--wrap=abort,--wrap=MP3Decode $(shell sdl2-config --libs)

CORE_SRC := WString.cpp Print.cpp Stream.cpp IPAddress.cpp
CORE_C   := stdlib_noniso.c
GFX_CPP  := lgfx/v1/LGFXBase.cpp lgfx/v1/lgfx_fonts.cpp lgfx/v1/lgfx_v1.cpp lgfx/v1/lgfx_v1_panel.cpp lgfx/v1/lgfx_v1_platforms.cpp lgfx/v1/lgfx_v1_touch.cpp
GFX_C    := lgfx/utility/lgfx_miniz.c lgfx/utility/lgfx_pngle.c lgfx/utility/lgfx_qoi.c lgfx/utility/lgfx_qrcode.c lgfx/utility/lgfx_tjpgd.c \
            $(patsubst $(M5GFX)/%,%,$(shell find $(M5GFX)/lgfx/Fonts -name '*.c'))
HELIX    := $(notdir $(wildcard $(LIBDEPS)/libhelix/src/libhelix-mp3/*.c))
KEYBOARD := $(LIBDEPS)/M5Cardputer/src/utility/Keyboard/Keyboard.cpp

LIB_OBJ := $(addprefix $(LIBBUILD)/core/,$(CORE_SRC:.cpp=.o) $(CORE_C:.c=.o)) \
           $(addprefix $(LIBBUILD)/gfx/,$(GFX_CPP:.cpp=.o) $(GFX_C:.c=.o)) \
           $(addprefix $(LIBBUILD)/helix/,$(HELIX:.c=.o)) $(LIBBUILD)/Keyboard.o
EMU_OBJ := $(addprefix $(LIBBUILD)/emu/,arduino_impl.o net_impl.o m5_impl.o scenario.o window.o mp3_cost.o heap.o runtime.o stubs.o power.o)
FW_SRC  := $(wildcard $(FIRMWARE)/src/*.cpp)
FW_OBJ  := $(addprefix $(BUILD)/fw/,$(notdir $(FW_SRC:.cpp=.o))) $(BUILD)/main_emu.o
FW_FLAGS := $(CXXFLAGS) -std=gnu++11 -I$(FIRMWARE)/include -idirafter $(SECRETS_DIR) $(DEVICE_DEFS) -DEMU_HEAP_START=$(HEAP_START) -include shim/firmware_prelude.h

BIN := $(BUILD)/cardputer-emulator
build: $(BIN)
$(BIN): $(LIB_OBJ) $(EMU_OBJ) $(FW_OBJ)
	g++ $^ -o $@ $(LDFLAGS)

$(LIBBUILD)/core/%.o: $(CORE)/%.cpp ; @mkdir -p $(@D); g++ $(CXXFLAGS) -include shim/Arduino.h -c $< -o $@
$(LIBBUILD)/core/%.o: $(CORE)/%.c ; @mkdir -p $(@D); gcc $(CFLAGS) -include shim/Arduino.h -c $< -o $@
$(LIBBUILD)/gfx/%.o: $(M5GFX)/%.cpp ; @mkdir -p $(@D); g++ $(CXXFLAGS) -c $< -o $@
$(LIBBUILD)/gfx/%.o: $(M5GFX)/%.c ; @mkdir -p $(@D); gcc $(CFLAGS) -c $< -o $@
# Helix is fixed-point code built on shifts UBSan objects to, all harmless on these targets.
$(LIBBUILD)/helix/%.o: $(LIBDEPS)/libhelix/src/libhelix-mp3/%.c ; @mkdir -p $(@D); gcc $(CFLAGS) $(if $(filter asan,$(SAN)),-fno-sanitize=undefined) -c $< -o $@
$(LIBBUILD)/Keyboard.o: $(KEYBOARD) shim/keyboard_prelude.h
	@mkdir -p $(@D)
	sed -e '/IOMatrix.h/d' -e '/TCA8418.h/d' -e '/M5Unified.h/d' -e 's#"Keyboard.h"#<utility/Keyboard/Keyboard.h>#' -e 's#"../common.h"#<utility/common.h>#' -e 's#"KeyboardReader/KeyboardReader.h"#<utility/Keyboard/KeyboardReader/KeyboardReader.h>#' $< > $(LIBBUILD)/Keyboard_gen.cpp
	g++ $(CXXFLAGS) -include shim/keyboard_prelude.h -c $(LIBBUILD)/Keyboard_gen.cpp -o $@
$(LIBBUILD)/emu/%.o: src/%.cpp src/emu.h src/device_model.h $(wildcard shim/*.h) ; @mkdir -p $(@D); g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/fw/%.o: $(FIRMWARE)/src/%.cpp shim/firmware_prelude.h ; @mkdir -p $(@D); g++ $(FW_FLAGS) -c $< -o $@
$(BUILD)/main_emu.o: src/main.cpp src/device_model.h src/emu.h ; @mkdir -p $(@D); g++ $(FW_FLAGS) -c $< -o $@

check:
	python3 tools/check.py

clean:
	rm -rf build run

.PHONY: build check clean
