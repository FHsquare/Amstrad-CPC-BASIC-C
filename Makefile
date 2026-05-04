CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Iinclude
LDFLAGS =

SRC = src/main.c src/runtime.c src/firmware.c src/repl.c src/execution.c
OBJ = $(SRC:.c=.o)

TARGET = amstrad_cpc_basic

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
