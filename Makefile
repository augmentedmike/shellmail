
CC     = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc
LDFLAGS = -lmbedtls -lmbedx509 -lmbedcrypto -lncurses -lsqlite3 -lpthread

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
    # Homebrew puts libraries in non-standard prefixes on Apple Silicon
    MBEDTLS_PREFIX  := $(shell brew --prefix mbedtls)
    NCURSES_PREFIX  := $(shell brew --prefix ncurses)
    CFLAGS  += -I$(MBEDTLS_PREFIX)/include -I$(NCURSES_PREFIX)/include
    LDFLAGS += -L$(MBEDTLS_PREFIX)/lib -L$(NCURSES_PREFIX)/lib
endif

ifeq ($(UNAME), Linux)
    # Use pkg-config where available; fall back to bare -lncurses.
    # Install deps: apt-get install libmbedtls-dev libncurses-dev libsqlite3-dev
    NCURSES_CFLAGS  := $(shell pkg-config --cflags ncurses 2>/dev/null)
    NCURSES_LIBS    := $(shell pkg-config --libs   ncurses 2>/dev/null || echo -lncurses)
    CFLAGS  += $(NCURSES_CFLAGS)
    LDFLAGS := $(filter-out -lncurses,$(LDFLAGS)) $(NCURSES_LIBS) -lm
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
