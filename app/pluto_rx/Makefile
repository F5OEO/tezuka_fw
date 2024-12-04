#CROSS_COMPILE=arm-linux-gnueabihf-
#SYSROOT=/home/hp-z2-dev/prog/tezuka_fw/buildroot-2024.08/output/staging
#CXX=$(CROSS_COMPILE)g++
#CC=$(CROSS_COMPILE)gcc
#HOST_DIR=/home/linuxdev/prog/pluto/firm033/pluto_radar/plutosdr-fw/buildroot/output/host
LDFLAGS+=-liio -lm 
CFLAGS+=-std=c99 -O3 
DESTDIR?=$(out)
#DESTDIR?=/usr/local

TARGET = plutorx

$(TARGET): $(TARGET).c
	$(CC) $(TARGET).c $(CFLAGS) $(LDFLAGS) -o $(TARGET)

all: $(TARGET)

install: $(TARGET)
	install -d -m 0755 $(DESTDIR)/bin
	install -m 0755 $(TARGET) $(DESTDIR)/bin/$(TARGET)

clean:
	$(RM) $(TARGET)
