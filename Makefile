CC=gcc
CFLAGS=-Wall
LIBS=-lutil

SYSTEM=$(shell uname -s)
ifeq ($(SYSTEM),$(filter $(SYSTEM),FreeBSD OpenBSD))
	CFLAGS+= -I/usr/local/include
	LIBS+= -L/usr/local/lib -liconv
else
ifeq ($(SYSTEM),Darwin)
	LIBS+= -liconv
else
ifeq ($(shell uname -o),Android)
	LIBS+= -L/data/data/com.termux/files/usr/lib -liconv
endif
endif
endif

cp437: cp437.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f cp437

