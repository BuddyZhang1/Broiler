# SPDX-License-Identifier: GPL-2.0-only
#
# Broiler
#
# (C) 2022.07.12 BuddyZhang1 <buddy.zhang@aliyun.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.

## Target
ifeq ("$(origin TARGETA)", "command line")
TARGET			:= $(TARGETA)
else
TARGET			:= BiscuitOS-Broiler
endif

## Source Code
SRC			+= $(wildcard $(PWD)/broiler/*.c)
SRC			+= $(wildcard $(PWD)/lib/*.c)
SRC			+= $(wildcard $(PWD)/virtio/*.c)
SRC			+= $(wildcard $(PWD)/foodstuff/*.c)
SRC			+= main.c

## BIOS Source Code
BIOS_SRC		+= bios/entry.S
BIOS_SRC		+= bios/e820.c 
BIOS_SRC		+= bios/int10.c
BIOS_SRC		+= bios/int15.c
BIOS_SRC		+= bios/bios-rom.ld.S

## CFlags
LCFLAGS			+= -DCONFIG_X86_64
## Header
LCFLAGS			+= -I./ -I$(PWD)/include
LLIB			+= -lpthread -lbfd

DOT			:= -

#
# BIOS assembly weirdness
#
BIOS_CFLAGS += -m32
BIOS_CFLAGS += -march=i386
BIOS_CFLAGS += -mregparm=3

BIOS_CFLAGS += -fno-stack-protector
BIOS_CFLAGS += -fno-pic
BIOS_C16GCC := include/broiler/code16gcc.h

## X86/X64 Architecture
ifeq ($(ARCH), i386)
CROSS_COMPILE	=
LCFLAGS		+= -m32
DOT		:=
else ifeq ($(ARCH), x86_64)
CROSS_COMPILE   :=
DOT		:=
endif

# Compile
B_AS		= $(CROSS_COMPILE)$(DOT)as
B_LD		= $(CROSS_COMPILE)$(DOT)ld
B_CC		= $(CROSS_COMPILE)$(DOT)gcc
B_CPP		= $(CC) -E
B_AR		= $(CROSS_COMPILE)$(DOT)ar
B_NM		= $(CROSS_COMPILE)$(DOT)nm
B_STRIP		= $(CROSS_COMPILE)$(DOT)strip
B_OBJCOPY	= $(CROSS_COMPILE)$(DOT)objcopy
B_OBJDUMP	= $(CROSS_COMPILE)$(DOT)objdump

## Install PATH
ifeq ("$(origin INSPATH)", "command line")
INSTALL_PATH		:= $(INSPATH)
else
INSTALL_PATH		:= ./
endif

# Build Broiler
all: bios/bios.bin.elf bios/bios-rom.o
	$(B_CC) $(LCFLAGS) -o $(TARGET) $(SRC) bios/bios-rom.o $(LLIB)

# Build BIOS
bios/bios.bin.elf: $(BIOS_SRC) 
	@$(B_CC) -include $(BIOS_C16GCC) $(LCFLAGS) $(BIOS_CFLAGS) -c bios/bios-memcpy.c -o bios/bios-memcpy.o
	@$(B_CC) -include $(BIOS_C16GCC) $(LCFLAGS) $(BIOS_CFLAGS) -c bios/e820.c -o bios/e820.o
	@$(B_CC) -include $(BIOS_C16GCC) $(LCFLAGS) $(BIOS_CFLAGS) -c bios/int10.c -o bios/int10.o
	@$(B_CC) -include $(BIOS_C16GCC) $(LCFLAGS) $(BIOS_CFLAGS) -c bios/int15.c -o bios/int15.o
	@$(B_CC) $(LCFLAGS) $(BIOS_CFLAGS) -c bios/entry.S -o bios/entry.o
	@$(LD) -T bios/bios-rom.ld.S -o bios/bios.bin.elf bios/bios-memcpy.o bios/entry.o bios/int10.o bios/int15.o bios/e820.o

bios/bios.bin: bios/bios.bin.elf
	@$(B_OBJCOPY) -O binary -j .text bios/bios.bin.elf bios/bios.bin

bios/bios-rom.o: bios/bios-rom.S bios/bios.bin include/broiler/bios-rom.h
	@$(B_CC) -c $(LCFLAGS) bios/bios-rom.S -o bios/bios-rom.o

include/broiler/bios-rom.h: bios/bios.bin.elf
	@sh bios/gen-offset.sh > include/broiler/bios-rom.h

# Install into BiscuitOS
install:
	@chmod 755 RunBroiler.sh
	@cp -rfa RunBroiler.sh $(INSTALL_PATH)
	@cp -rfa $(TARGET) $(INSTALL_PATH)

clean:
	@rm -rf $(TARGET) BiscuitOS-Broiler-default *.o bios/*.o \
		bios/*.bin* include/broiler/bios-rom.h *.s *.i

.PHONY: FORCE
