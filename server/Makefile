CROSS_COMPILE?=
CC?=${CROSS_COMPILE}gcc
LDFLAGS+=-lpthread -pthread
aesdsocket:
	@echo "Compiling aesdsocket"
	$(CC) $(CCFLAGS) -c aesdsocket.c
	$(CC) $(LDFLAGS) aesdsocket.o -o aesdsocket

all: aesdsocket

clean:
	rm -f aesdsocket.o aesdsocket
