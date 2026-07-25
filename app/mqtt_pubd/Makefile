
CC=$(CROSS_COMPILE)gcc

all: mqtt_pubd

mqtt_pubd: mqtt_pubd.c
	$(CC) -O2 mqtt_pubd.c -o mqtt_pubd -lmosquitto

clean:
	rm -f mqtt_pubd
