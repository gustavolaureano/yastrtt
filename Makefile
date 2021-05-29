ifeq ($(OS),Windows_NT)
TARGET = yastrtt.exe
CC = i686-w64-mingw32-gcc
LINKER = i686-w64-mingw32-g++
RM=del
LD_LIB  = ./lib/win_32
else
TARGET = yastrtt
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
L_FLAG = $(LD_LIB)/libstlink.a


$(TARGET):yastrtt.o
	$(LINKER) $^ $(L_FLAG) -o $(TARGET)

yastrtt.o:./src/yastrtt.c
	$(CC) $^ $(C_FLAG)

clean:
	$(RM) -f *.o
	$(RM) -f $(TARGET)
