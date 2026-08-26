
CC=$(CROSS_COMPILE)gcc

all: classifier

classifier: classifier.c
	$(CC) -O2 -mfpu=neon -mfloat-abi=hard classifier.c -o classifier -lwebsockets -lm

clean:
	rm -f classifier
