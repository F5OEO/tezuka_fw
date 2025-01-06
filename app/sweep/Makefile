LDFLAGS+=-lm -lcivetweb -lpthread -lssl -lcrypto
CFLAGS+=-std=c99 -O3 
DESTDIR?=$(out)

TARGET = sweepfft

$(TARGET): $(TARGET).c
	$(CC) $(TARGET).c $(CFLAGS) $(LDFLAGS) -o $(TARGET)

all: $(TARGET)

install: $(TARGET)
	install -d -m 0755 $(DESTDIR)/bin
	install -m 0755 $(TARGET) $(DESTDIR)/bin/$(TARGET)

clean:
	$(RM) $(TARGET)
