
CC = gcc

CFLAGS =  -Wall -Wextra -O2
LDFLAGS = -lmbedtls -lmbedx509 -lmbedcrypto -lncurses

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
	MBEDTLS_PREFIX = $(shell brew --prefix mbedtls)
	NCURSES_PREFIX = $(shell brew --prefix ncurses)
	CFLAGS += -I$(MBEDTLS_PREFIX)/include -I$(NCURSES_PREFIX)/include
	LDFLAGS += -L$(MBEDTLS_PREFIX)/lib -L$(NCURSES_PREFIX)/lib
endif

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, build/%.o, $(SRCS))

TARGET = shellmail

all: build $(TARGET)

build:
	mkdir -p build

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET)
