
CC = gcc

CFLAGS =  -Wall -Wextra -O2 -Isrc
LDFLAGS = -lmbedtls -lmbedx509 -lmbedcrypto -lncurses -lsqlite3 -lpthread

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
	MBEDTLS_PREFIX = $(shell brew --prefix mbedtls)
	NCURSES_PREFIX = $(shell brew --prefix ncurses)
	CFLAGS += -I$(MBEDTLS_PREFIX)/include -I$(NCURSES_PREFIX)/include
	LDFLAGS += -L$(MBEDTLS_PREFIX)/lib -L$(NCURSES_PREFIX)/lib
endif

SRCS = $(shell find src -name '*.c')
OBJS = $(patsubst src/%.c, build/%.o, $(SRCS))

TARGET = shellmail

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build $(TARGET)
