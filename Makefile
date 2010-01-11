# Makefile for Dillon's crond and crontab
VERSION = 4.2

# these variables can be configured by e.g. `make SCRONTABS=/different/path`
SCRONTABS = "/etc/cron.d"
CRONTABS = "/var/spool/cron"
TIMESTAMPS = "/var/spool/cronstamps"
LOG_IDENT = "crond"
# LOG_FILE is only used when syslog isn't
LOG_FILE = "/var/log/crond.log"
# this is only used for logging to file; syslog manages its own timestamps
# if LC_TIME is set, it will override the compiled-in timestamp format
TIMESTAMP_FMT = "%b %e %H:%M:%S"


# PREFIX and DESTDIR are only used when doing `make install`
PREFIX = /usr/local


SHELL = /bin/sh
INSTALL = install -o root -g root
INSTALL_PROGRAM = $(INSTALL) -D
INSTALL_DATA = $(INSTALL) -D -m0644
INSTALL_DIR = $(INSTALL) -d -m0755
# CC = gcc
CFLAGS = -O2 -Wall -Wstrict-prototypes
SRCS = main.c subs.c database.c job.c
OBJS = main.o subs.o database.o job.o
TABSRCS = crontab.c subs.c
TABOBJS = crontab.o subs.o
PROTOS = protos.h
LIBS =
DEFS =  -DVERSION='"$(VERSION)"' \
		-DSCRONTABS='$(SCRONTABS)' -DCRONTABS='$(CRONTABS)' \
		-DTIMESTAMPS='$(TIMESTAMPS)' -DLOG_IDENT='$(LOG_IDENT)' \
		-DLOG_FILE='$(LOG_FILE)' -DTIMESTAMP_FMT='$(TIMESTAMP_FMT)'


all: $(PROTOS) crond crontab ;

protos.h: $(SRCS) $(TABSRCS)
	fgrep -h Prototype $(SRCS) $(TABSRCS) > protos.h

crond: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o crond

crontab: $(TABOBJS)
	$(CC) $(CFLAGS) $(TABOBJS) -o crontab

%.o: %.c defs.h $(PROTOS)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(DEFS) $< -o $@

install:
	$(INSTALL_PROGRAM) -m0700 crond $(DESTDIR)$(PREFIX)/sbin/crond
	$(INSTALL_PROGRAM) -m4750 crontab $(DESTDIR)$(PREFIX)/bin/crontab
	$(INSTALL_DATA) crontab.1 $(DESTDIR)$(PREFIX)/share/man/man1/crontab.1
	$(INSTALL_DATA) crond.8 $(DESTDIR)$(PREFIX)/share/man/man8/crond.8
	$(INSTALL_DIR) $(DESTDIR)$(SCRONTABS)
	$(INSTALL_DIR) $(DESTDIR)$(CRONTABS)
	$(INSTALL_DIR) $(DESTDIR)$(TIMESTAMPS)

clean: force
	rm -f *.o $(PROTOS)
	rm -f crond crontab

force: ;

man: force
	-pandoc -t man -f markdown -s crontab.markdown -o crontab.1
	-pandoc -t man -f markdown -s crond.markdown -o crond.8


# for maintainer's use only
TARNAME = /home/abs/_dcron/dcron-$(VERSION).tar.gz
tar: clean man
	pax -wz ../repo  -s'=^\.\./repo/.git.*==' -s'=^\.\./repo=dcron-$(VERSION)=' -f $(TARNAME).new
	mv -f $(TARNAME).new $(TARNAME)

