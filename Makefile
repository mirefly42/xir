.POSIX:

PREFIX = /usr/local
BINDIR = ${DESTDIR}${PREFIX}/bin
LIBDIR = ${DESTDIR}${PREFIX}/lib
HDRDIR = ${DESTDIR}${PREFIX}/include/xir

SRC = bit_set.c dump.c foreign_imports.c interp.c ir.c nasm.c utils.c validation.c
HDR = bit_set.h dump.h foreign_imports.h interp.h ir.h nasm.h utils.h validation.h
OBJ = ${SRC:.c=.o}
LIBS = -lffi -llis

all: xirc libxir.a

xirc: ${OBJ} irc.o
	${CC} -o $@ irc.o ${OBJ} ${LIBS} ${LDFLAGS}

libxir.a: ${OBJ}
	${AR} -rc $@ $?

.c.o:
	${CC} -c -o $@ $< ${CFLAGS}

${OBJ}: ${HDR}
irc.o: ${HDR}

install: all
	mkdir -p -- "${BINDIR}"
	cp -f -- xirc "${BINDIR}"
	cd -- "${BINDIR}" && chmod 755 xirc
	mkdir -p -- "${LIBDIR}"
	cp -f -- libxir.a "${LIBDIR}"
	cd -- "${LIBDIR}" && chmod 644 libxir.a
	mkdir -p -- "${HDRDIR}"
	cp -f -- ${HDR} "${HDRDIR}"
	cd -- "${HDRDIR}" && chmod 644 ${HDR}

uninstall:
	cd -- "${BINDIR}" && rm -f xirc
	cd -- "${LIBDIR}" && rm -f libxir.a
	cd -- "${HDRDIR}" && rm -f ${HDR}
	rmdir -- "${HDRDIR}"

clean:
	rm -f xirc libxir.a irc.o ${OBJ}

.PHONY: all install uninstall clean
