CC=gcc
CFLAGS=-Wall
LIBS=-lutil

ifeq ($(shell uname -s),FreeBSD)
	CFLAGS+= -I/usr/local/include
	LIBS+= -L/usr/local/lib -liconv
endif

cp437: cp437.c
	$(CC) $(CFLAGS) $(LIBS) -o $@ $<

clean:
	rm -f cp437

