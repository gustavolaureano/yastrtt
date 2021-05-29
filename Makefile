
SOURCEDIR = src
BUILDDIR = build
EXECUTABLE = yastrtt

SOURCES = $(wildcard $(SOURCEDIR)/*.c)
OBJECTS = $(patsubst $(SOURCEDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))


ifeq ($(OS),Windows_NT)
EXECUTABLE += .exe
CC = i686-w64-mingw32-gcc
LINKER = i686-w64-mingw32-g++
RM=del
LD_LIB  = ./lib/win_32
else
CC = gcc
LINKER = g++
RM=rm
LBITS := $(shell getconf LONG_BIT)
ifeq ($(LBITS),64)
LD_LIB  = ./lib/linux_64
else
LD_LIB  = ./lib/linux_86
endif
endif
C_FLAG = -I. -I./inc -I./inc/stlink -I./inc/stlink/stlink-lib -I./inc -I./inc/libusb -c -std=c99
L_FLAG = $(LD_LIB)/libstlink.a $(LD_LIB)/libusb-1.0.a

ifeq ($(OS),Windows_NT)
L_FLAG += -lkernel32 -luser32 -lgdi32 -lwinspool -lcomdlg32 -ladvapi32 -lshell32 -lole32 -loleaut32 -luuid -lcomctl32 -lsetupapi -mwindows -static 
else
L_FLAG += -lpthread -ludev
endif


all: dir $(BUILDDIR)/$(EXECUTABLE)

dir:
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/$(EXECUTABLE): $(OBJECTS)
	$(LINKER) $^ $(L_FLAG) -o $@

$(OBJECTS): $(BUILDDIR)/%.o : $(SOURCEDIR)/%.c
	$(CC) $(C_FLAG) $< -o $@

clean:
	rm -f $(BUILDDIR)/*o $(BUILDDIR)/$(EXECUTABLE)