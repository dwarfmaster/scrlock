# slock - simple screen locker
# See LICENSE file for copyright and license details.

include config.mk

SRC = slock.c
OBJ = ${SRC:.c=.o}

all: options scrlock

options:
	@echo slock build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

scrlock: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f scrlock ${OBJ} scrlock-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p scrlock-${VERSION}
	@cp -R LICENSE Makefile README config.mk ${SRC} scrlock-${VERSION}
	@tar -cf scrlock-${VERSION}.tar scrlock-${VERSION}
	@gzip scrlock-${VERSION}.tar
	@rm -rf scrlock-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f scrlock ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/scrlock
	@chmod u+s ${DESTDIR}${PREFIX}/bin/scrlock

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/scrlock

chmod:
	@echo change scrlock owner to root:root
	@chown root:root scrlock
	@echo change scrlock permissions
	@chmod u+s scrlock

.PHONY: all options clean dist install uninstall chmod
