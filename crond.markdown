% CROND(8)
% 
% 6 Jan 2010

NAME
====
crond - cron daemon (Dillon's Cron Daemon)

SYNOPSIS
========
**crond [-l loglevel] [-L [logfile]|-S] [-M mailscript] [-m mailto]
[-d|-f|-b] [-s systemdir] [-c crondir] [-t timestamps]**

OPTIONS
=======
**crond** is a background daemon that parses individual crontab files and
executes commands on behalf of the users in question.

-l loglevel
:	set logging level, default is <= notice = 5.

:	Valid level names are as described in logger.1 and syslog.3: alert,
	crit, debug, emerg, err, error (deprecated synonym for err), info,
	notice, panic (deprecated synonym for emerg), warning, warn (deprecated
	synonym for warning).

-L [logfile]
:	log to specified file (if none supplied, uses /var/log/crond.log).

-S
:	use syslogd (default).

-M mailscript
:	receives any cron job output as stdin (default is /usr/sbin/sendmail).

-m mailto
:	address to mail any cron output to (default is local user).

-d
:	turn on debugging. This option sets the logging level to <= debug and causes
	**crond** to run in the foreground.

-f
:	run **crond** in the foreground.

-b
:	run **crond** in the background (default unless -d or -f is specified).

-s systemdir
:	specify directory containing system-wide crontab files (default is
	/etc/cron.d).

-c crondir
:	specify crontab spool directory (default is /var/spool/cron/crontabs).

-t timestamps
:	specify directory containing cron timestamps for @freq jobs
	(default is /var/spool/cron/timestamps).

DESCRIPTION
===========
**crond** is responsible for scanning the crontab files and running their commands
at the appropriate time. The **crontab** program communicates with **crond** through
the "cron.update" file which resides in the crontabs directory, usually
/var/spool/cron/crontabs. This is accomplished by appending the filename of the
modified or deleted crontab file to "cron.update" which **crond** then picks up to
resynchronize or remove its internal representation of the file.

Whenever the "cron.update" file is seen, **crond** also re-reads all of timestamp
files from disk. Normally these will just mirror **crond**'s own internal
representations, but this mechanism could be used to externally update the
timestamps.

The "cron.update" file can also be used to instruct **crond** to schedule a named
job. Each line in this file should have the format:

	user job1 !job2

to request that user's job1 should be scheduled (waiting first for the
successful completion of any jobs named in job1's AFTER= argument), and job2
should also be scheduled (without waiting for other jobs).

**crond** has a number of built in limitations to reduce the chance of it being
ill-used. Potentially infinite loops during parsing are dealt with via a
failsafe counter, and user crontabs are generally limited to 256 crontab
entries. Crontab lines may not be longer than 1024 characters, including the
newline.

Whenever **crond** must run a job, it first creates a daemon-owned temporary file
O_EXCL and O_APPEND to store any output, then fork()s and changes its user and
group permissions to match that of the user the job is being run for, then
**exec**s **/bin/sh -c <command>** to run the job. The temporary file remains under the
ownership of the daemon to prevent the user from tampering with it. Upon job
completion, **crond** verifies the secureness of the mail file and, if it has been
appended to, mails to the file to user. The **sendmail** program (or custom
mailscript, if supplied) is run under the user's uid to prevent mail related
security holes.

When the **crontab** program allows a user to edit his crontab, it copies the
crontab to a user owned file before running the user's prefered editor. The
suid **crontab** keeps an open descriptor to the file which it later uses to copy
the file back, thereby ensuring the user has not tampered with the file type.
Unlike **crontab**, the **crond** program does not leave an open descriptor to the file
for the duration of the job's execution as this might cause **crond** to run out of
descriptors.

**crond** always synchronizes to the top of the minute, checking the current time
against the list of possible jobs. The list is stored such that the scan goes
very quickly, and **crond** can deal with several thousand entries without taking
any noticable amount of cpu.

AUTHORS
=======
Matthew Dillon (dillon@apollo.backplane.com)  
Jim Pryor (profjim@jimpryor.net)
