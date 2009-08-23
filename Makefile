# Makefile for yet another cron and crontab
#

# Overriding defines from defs.h
# DEFS =  -DSCRONTABS='"/etc/cron.d"' \
#          -DCRONTABS='"/var/spool/cron/crontabs"' \
#          -DCRONUPDATE='"cron.update"' \
#          -DTIMESTAMPS='"/var/sool/cron/timestamps"' \
#          -DTMPDIR='"/tmp"' \
#          -DLOG_FILE='"/var/log/crond.log"' \
#          -DLOG_IDENT='"crond"' \
#          -DSENDMAIL='"/usr/sbin/sendmail"' \

DESTDIR ?= /usr/local
DISTOWN= jim
DISTTAR= /home/abs/yacron40beta.tgz

CC  = gcc
CFLAGS = -O2 -Wall -Wstrict-prototypes ${DEFS}
LIB = 
SRCS = main.c subs.c database.c job.c
OBJS = main.o subs.o database.o job.o
D_SRCS = crontab.c subs.c
D_OBJS = crontab.o subs.o
PROTOS= protos.h

all:	${PROTOS} crond crontab

crond:	${OBJS}
	${CC} ${CFLAGS} -o crond ${OBJS} ${LIB}
	strip crond

crontab:  ${D_OBJS}
	${CC} ${CFLAGS} -o crontab ${D_OBJS}
	strip crontab

protos.h: ${SRCS} ${D_SRCS}
	fgrep -h Prototype ${SRCS} ${D_SRCS} >protos.h

clean:  cleano
	rm -f crond crontab

cleano:
	rm -f *.o dcron.tgz ${PROTOS}

install:
	install -o root -g wheel -m 0755 crond ${DESTDIR}/sbin/crond
	install -o root -g wheel -m 4755 crontab ${DESTDIR}/bin/crontab
	install -o root -g wheel -m 0644 crontab.1 ${DESTDIR}/man/man1/crontab.1
	install -o root -g wheel -m 0644 crond.8 ${DESTDIR}/man/man8/crond.8

tar: clean
	(cd ..; tar czf ${DISTTAR}.new repo)
	chown ${DISTOWN} ${DISTTAR}.new
	chmod 644 ${DISTTAR}.new
	mv -f ${DISTTAR}.new ${DISTTAR}

