#==============================================================================
# Makefile K1921VG015 - Bootloader
#==============================================================================
TARGET_NAME ?= bootloader

COMPILER_PATH ?=
ADDITIONAL_FLAGS ?=
DEFINES ?= -DRETARGET
PREFIX ?=

# Force cmd.exe on Windows so batch-style for-loops work correctly
ifeq ($(OS), Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
endif

LINKER_PATH ?=
MARCH ?=
MABI ?=
BUILD_NAME ?= build
BUILD_PATH := ./$(BUILD_NAME)

OPTIMISATION ?=

SRCS := ./src/bl_main.c \
        ./src/bl_hal.c \
        ./src/bl_image.c \
        ./src/bl_crc32.c \
        ./src/bl_jump.c \
        ./src/bl_extflash_w25q32.c \
        ../vg015/device/source/sys_init.c

ASMS := ../vg015/device/source/startup_k1921vg015.S

OBJS := $(SRCS:.c=.o)
OBJS += $(ASMS:.S=.o)

INCLUDE_FOLDERS = -I"./include" -I"./src" -I"../vg015/device/include" -I"../vg015/plib/inc" -I"../vg015"
OBJ_DIRS = $(sort $(dir $(OBJS)))
OBJ_DIRS := $(patsubst ./%,%,$(OBJ_DIRS))

LIBS ?= -L$(LINKER_PATH)
LIBS_ACR ?= -lc_nano -lg_nano -lgcc
LD_FLAGS ?= -static $(LIBS) $(LIBS_ACR)

# Normalize Windows-style paths for sh-based recipes.
COMPILER_PATH_NORM := $(subst \,/,$(COMPILER_PATH))
export CROSS_PREFIX ?= $(COMPILER_PATH_NORM)/$(PREFIX)
ifeq ($(OS), Windows_NT)
export RISCV-GCC     ?= $(CROSS_PREFIX)gcc.exe -c -Wall $(OPTIMISATION) -march=$(MARCH) -mabi=$(MABI) -g -ggdb3 $(DEFINES) $(INCLUDE_FOLDERS) -static -std=gnu99 $(LIBS) $(LIBS_ACR) $(ADDITIONAL_FLAGS)
export RISCV-OBJDUMP ?= $(CROSS_PREFIX)objdump.exe -S
export RISCV-OBJCOPY ?= $(CROSS_PREFIX)objcopy.exe
export RISCV-SIZE    ?= $(CROSS_PREFIX)size.exe
export RISCV-GDB     ?= $(CROSS_PREFIX)gdb.exe
else
export RISCV-GCC     ?= $(CROSS_PREFIX)gcc -c -Wall $(OPTIMISATION) -march=$(MARCH) -mabi=$(MABI) -g -ggdb3 $(DEFINES) $(INCLUDE_FOLDERS) -static -std=gnu99 $(LIBS) $(LIBS_ACR) $(ADDITIONAL_FLAGS)
export RISCV-OBJDUMP ?= $(CROSS_PREFIX)objdump -S
export RISCV-OBJCOPY ?= $(CROSS_PREFIX)objcopy
export RISCV-SIZE    ?= $(CROSS_PREFIX)size
export RISCV-GDB     ?= $(CROSS_PREFIX)gdb
endif

make_build:
ifeq ($(OS), Windows_NT)
	@$(foreach dir,$(OBJ_DIRS),if not exist "$(subst /,\,$(BUILD_PATH)/$(dir))" mkdir "$(subst /,\,$(BUILD_PATH)/$(dir))" &)
else
	@$(foreach dir,$(OBJ_DIRS),mkdir -p "$(BUILD_PATH)/$(dir)";)
endif

header:
	@ echo "K1921VG015 Bootloader build"

%.o: %.S
	@ echo "ASSEMBLY "$^""
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(BUILD_PATH)/$(dir $@))" mkdir "$(subst /,\,$(BUILD_PATH)/$(dir $@))"
else
	@mkdir -p "$(BUILD_PATH)/$(dir $@)"
endif
	$(RISCV-GCC) $^ -o $(BUILD_PATH)/$@
	@ echo "...DONE"

%.o: %.c
	@ echo "COMPILE "$^""
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(BUILD_PATH)/$(dir $@))" mkdir "$(subst /,\,$(BUILD_PATH)/$(dir $@))"
else
	@mkdir -p "$(BUILD_PATH)/$(dir $@)"
endif
	$(RISCV-GCC) $^ -o $(BUILD_PATH)/$@
	@ echo "...DONE"

$(BUILD_PATH)/$(TARGET_NAME).elf: $(OBJS)
	@ echo "LINK "$^""
	$(CROSS_PREFIX)g++ -o $@ $(foreach obj,$(OBJS),$(BUILD_PATH)/$(obj)) -static -march=$(MARCH) -mabi=$(MABI) -g -ggdb3 -std=gnu99 $(LD_FLAGS) -nostartfiles -T ./device/ldscripts/k1921vg015_boot.ld
	@ echo "...DONE"

$(BUILD_PATH)/$(TARGET_NAME).dump: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "DUMP "$^""
	$(RISCV-OBJDUMP) $^ > $@
	@ echo "...DONE"

size: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "~~SIZE~~"
	$(RISCV-SIZE) $^

hex: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "HEX"
	$(RISCV-OBJCOPY) -O ihex $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).hex
	@ echo "...DONE"

clean:
	@ echo "CLEANING"
ifeq ($(OS), Windows_NT)
	@ -if exist "$(BUILD_PATH)" rmdir /s /q "$(BUILD_PATH)"
else
	@rm -rf "$(BUILD_PATH)"
endif
	@ echo "...DONE"

bin: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "BIN "
	$(RISCV-OBJCOPY) -O binary $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).bin
	@ echo "...DONE"


all_noclean: header make_build $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).dump hex bin size

rebuild:
	@$(MAKE) clean
	@$(MAKE) all_noclean

# Backward-compatible "all", but safe with -j:
# don't run clean as part of parallel build; use `rebuild` when you need a clean build.
all: all_noclean

debug: all
	@ echo "LOAD "$(TARGET_NAME).elf""
	$(RISCV-GDB) $(BUILD_PATH)/$(TARGET_NAME).elf -iex="target extended-remote localhost:3333" -ex="monitor reset halt" -ex="load"
