.POSIX:

PREFIX = /usr/local
BINDIR = ${DESTDIR}${PREFIX}/bin
LIBDIR = ${DESTDIR}${PREFIX}/lib
HDRDIR = ${DESTDIR}${PREFIX}/include/xxxir

SRC = interp.c ir.c nasm.c utils.c validation.c
HDR = interp.h ir.h nasm.h utils.h validation.h
OBJ = ${SRC:.c=.o}
LIBS = -llis

all: xxxirc libxxxir.a

xxxirc: ${OBJ} irc.o
	${CC} -o $@ irc.o ${OBJ} ${LIBS} ${LDFLAGS}

libxxxir.a: ${OBJ}
	${AR} -rc $@ $?

.c.o:
	${CC} -c -o $@ $< ${CFLAGS}

${OBJ}: ${HDR}

install: all
	mkdir -p -- "${BINDIR}"
	cp -f -- xxxirc "${BINDIR}"
	cd -- "${BINDIR}" && chmod 755 xxxirc
	mkdir -p -- "${LIBDIR}"
	cp -f -- libxxxir.a "${LIBDIR}"
	cd -- "${LIBDIR}" && chmod 644 libxxxir.a
	mkdir -p -- "${HDRDIR}"
	cp -f -- ${HDR} "${HDRDIR}"
	cd -- "${HDRDIR}" && chmod 644 ${HDR}

uninstall:
	cd -- "${BINDIR}" && rm -f xxxirc
	cd -- "${LIBDIR}" && rm -f libxxxir.a
	cd -- "${HDRDIR}" && rm -f ${HDR}
	rmdir -- "${HDRDIR}"

clean:
	rm -f xxxirc libxxxir.a irc.o ${OBJ}

.PHONY: all install uninstall clean
