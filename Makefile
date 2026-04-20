
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
OBJS = $(SRCS:.c=.o)

TARGET = shellmail

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)

