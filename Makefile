# Makefile for Lab 6 – Virtual Memory Manager
# Compile:  make
# Clean:    make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
TARGET  = group1_manager
SRC     = group1_manager.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
