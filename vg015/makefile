#==============================================================================
# Makefile NIIET K1921VG015 - VG015 Project
# Created by NIIET
# free to use
#==============================================================================
TARGET_NAME ?= vg015
	
COMPILER_PATH ?=
ADDITIONAL_FLAGS ?=
DEFINES ?= -DRETARGET -DHSECLK_VAL=16000000 -DSYSCLK_PLL -DCKO_PLL0
PREFIX ?= 

# Ensure Windows recipes run under cmd.exe even if make provides sh.exe
ifeq ($(OS), Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /c
endif
	
LINKER_PATH  		?=
LD_SRC 				?=
MARCH				?=
MABI				?=
BUILD_NAME			?= build
BUILD_PATH  		:= ./$(BUILD_NAME)
	
OPTIMISATION ?= -Os

rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
	
SRCS := $(call rwildcard,.,*.c)
ASMS := $(call rwildcard,.,*.S)
HEADERS := $(call rwildcard,.,*.h)
	
OBJS := $(SRCS:.c=.o)
OBJS_SRC := $(SRCS:.c=.o)
OBJS_SRC += $(ASMS:.S=.o)
OBJS := $(addprefix $(BUILD_PATH)/,$(OBJS_SRC))
	
INCLUDE_FOLDERS = $(foreach inc, $(sort $(dir $(HEADERS))), -I"$(inc)") -I"./device/Include" -I"./plib/inc" -I"."
OBJ_DIRS := $(sort $(dir $(OBJS)))
	
LIBS ?= -L$(LINKER_PATH)
LIBS_ACR ?= -lc_nano -lg_nano -lgcc
	
LD_FLAGS ?= -static $(LIBS)  $(LIBS_ACR) -Wl,--gc-sections

# Normalize Windows-style paths for sh-based recipes.
COMPILER_PATH_NORM := $(subst \,/,$(COMPILER_PATH))
export CROSS_PREFIX  ?= $(COMPILER_PATH_NORM)/$(PREFIX)
ifeq ($(OS), Windows_NT)
	export RISCV-GCC	 ?= $(CROSS_PREFIX)gcc.exe -c -Wall -ffunction-sections -fdata-sections $(OPTIMISATION) -march=$(MARCH) -mabi=$(MABI) -g -ggdb3 $(DEFINES) $(INCLUDE_FOLDERS) -static -std=gnu99 $(LIBS) $(LIBS_ACR) $(ADDITIONAL_FLAGS)
export RISCV-AS 	 ?= $(CROSS_PREFIX)as.exe -mno-arch-attr $(OPTIMISATION) -march=$(MARCH) -mabi=$(MABI) $(ADDITIONAL_FLAGS)
export RISCV-LD 	 ?= $(CROSS_PREFIX)ld.exe 
export RISCV-OBJDUMP ?= $(CROSS_PREFIX)objdump.exe -S
export RISCV-OBJCOPY ?= $(CROSS_PREFIX)objcopy.exe
export RISCV-SIZE 	 ?= $(CROSS_PREFIX)size.exe
export RISCV-GDB	 ?= $(CROSS_PREFIX)gdb.exe
else 
	export RISCV-GCC	 ?= $(CROSS_PREFIX)gcc -c -Wall -ffunction-sections -fdata-sections $(OPTIMISATION) -g -ggdb3 -march=$(MARCH) -mabi=$(MABI) $(DEFINES) $(INCLUDE_FOLDERS) -static -std=gnu99 $(LIBS) $(LIBS_ACR) $(ADDITIONAL_FLAGS)
export RISCV-AS 	 ?= $(CROSS_PREFIX)as -mno-arch-attr $(OPTIMISATION) -march=$(MARCH) -mabi=$(MABI) $(ADDITIONAL_FLAGS)
export RISCV-LD 	 ?= $(CROSS_PREFIX)ld 
export RISCV-OBJDUMP ?= $(CROSS_PREFIX)objdump -S
export RISCV-OBJCOPY ?= $(CROSS_PREFIX)objcopy
export RISCV-SIZE 	 ?= $(CROSS_PREFIX)size
export RISCV-GDB	 ?= $(CROSS_PREFIX)gdb
endif

make_build:
ifeq ($(OS), Windows_NT)
	@$(foreach dir,$(OBJ_DIRS),if not exist "$(subst /,\,$(dir))" mkdir "$(subst /,\,$(dir))" &)
else
	@$(foreach dir,$(OBJ_DIRS),mkdir -p "$(dir)";)
endif
	
help:
	@ echo "help: make [all, clean, load]"
	
header:
	@ echo "NIIET MAKEFILE v.1.0 - VG015"
	
$(BUILD_PATH)/%.o: %.S
	@ echo "ASSEMBLY "$^"" 
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@mkdir -p "$(dir $@)"
endif
	$(RISCV-GCC) $^ -o $@
	@ echo "...DONE"
	
$(BUILD_PATH)/%.o: %.c
	@ echo "COMPILE "$^""
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@mkdir -p "$(dir $@)"
endif
	$(RISCV-GCC) $^ -o $@ 
	@ echo "...DONE"
	
%.elf: $(OBJS)
	@ echo "LINK "$^"" 
	$(CROSS_PREFIX)gcc -o $@ $(OBJS) -static -march=$(MARCH) -mabi=$(MABI) -g -ggdb3 -std=gnu99 $(LD_FLAGS) -L./device/ldscripts -nostartfiles -T ./device/ldscripts/k1921vg015_flash_bl.ld
	@ echo "...DONE" 
	
%.dump: %.elf
	@ echo "DUMP "$^"" 
	$(RISCV-OBJDUMP) $^ > $@
	@ echo "...DONE"
	
size: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "~~SIZE~~ """ 
	$(RISCV-SIZE) $^
	
clean:
	@ echo "CLEANING "$^""
ifeq ($(OS), Windows_NT)
	@ -if exist "$(BUILD_PATH)" rmdir /s /q "$(BUILD_PATH)"
else
	@rm -rf "$(BUILD_PATH)"
endif
	@ echo "...DONE"
	
hex: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "HEX "
	$(RISCV-OBJCOPY) -O ihex $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).hex
	@ echo "...DONE"
	
bin: $(BUILD_PATH)/$(TARGET_NAME).elf
	@ echo "BIN "
	$(RISCV-OBJCOPY) -O binary $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).bin
	@ echo "...DONE"


all: header make_build $(BUILD_PATH)/$(TARGET_NAME).elf $(BUILD_PATH)/$(TARGET_NAME).dump hex bin size
	
debug: all
	@ echo "LOAD "$(TARGET_NAME).elf"" 
	$(RISCV-GDB) $(BUILD_PATH)/$(TARGET_NAME).elf -iex="target extended-remote localhost:3333" -ex="monitor reset halt" -ex="load"

