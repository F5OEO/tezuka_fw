
CC=$(CROSS_COMPILE)gcc
ARCHFLAGS ?= -mfpu=neon -mfloat-abi=hard
LIBS ?= -liio

all: cpdetect

cpdetect: cpdetect.c
	$(CC) -O2 $(ARCHFLAGS) cpdetect.c -o cpdetect $(LIBS) -lm

clean:
	rm -f cpdetect
