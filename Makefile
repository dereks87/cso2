# Makefile (default target builds gettimings as required)
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11 -DNDEBUG
LDFLAGS ?=

all: gettimings

gettimings: gettimings.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f gettimings
