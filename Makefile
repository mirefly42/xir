.POSIX:

SRC = interp.c ir.c irc.c nasm.c utils.c
HDR = interp.h ir.h nasm.h utils.h
OBJ = ${SRC:.c=.o}
LIBS = -llis

all: irc

irc: ${OBJ}
	${CC} -o $@ ${OBJ} ${LIBS} ${LDFLAGS}

.c.o:
	${CC} -c -o $@ $< ${CFLAGS}

${OBJ}: ${HDR}

clean:
	rm -f irc ${OBJ}

.PHONY: all clean
