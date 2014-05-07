CC=gcc
CFLAGS=-Wall
LIBS=-lutil

ifeq ($(shell uname -s),FreeBSD)
	CFLAGS+= -I/usr/local/include
	LIBS+= -L/usr/local/lib -liconv
else
ifeq ($(shell uname -s),Darwin)
	LIBS+= -liconv
endif
endif

cp437: cp437.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f cp437

