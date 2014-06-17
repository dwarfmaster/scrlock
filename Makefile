# slock - simple screen locker
# See LICENSE file for copyright and license details.

SRC = scrlock.c
OBJ = ${SRC:.c=.o}
PREFIX = /usr/local
CC = cc

INCS = -I. -I/usr/include `pkg-config --cflags xcb xcb-image`
LIBS = -L/usr/lib -lc -lcrypt `pkg-config --libs xcb xcb-image`
CFLAGS = -std=c99 -pedantic -Wall -O0 -g ${INCS}
LDFLAGS = -s ${LIBS}

all: options scrlock

options:
	@echo slock build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

scrlock: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f scrlock ${OBJ} scrlock.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p scrlock
	@cp -R LICENSE Makefile README config.mk ${SRC} scrlock
	@tar -cf scrlock.tar scrlock
	@gzip scrlock.tar
	@rm -rf scrlock

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
