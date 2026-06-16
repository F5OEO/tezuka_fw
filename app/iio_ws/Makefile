
CXX=$(CROSS_COMPILE)g++
CC=$(CROSS_COMPILE)gcc

all: iio_ws_proxy

iio_ws_proxy: iio_ws_proxy.c
	$(CC) -O2 -mfpu=neon -mfloat-abi=hard iio_ws_proxy.c -o iio_ws_proxy -liio -lwebsockets -lpthread

clean:
	rm -f iio_ws_proxy
