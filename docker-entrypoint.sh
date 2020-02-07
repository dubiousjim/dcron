#!/bin/sh

if [[ ! -d "/var/spool/cron/crontabs" ]]
then
	install -o root -d -m0755 -g root /var/spool/cron/crontabs
fi

if [[ ! -d "/var/spool/cron/cronstamps" ]]
then
	install -o root -d -m0755 -g root /var/spool/cron/cronstamps
fi

exec tini $*
