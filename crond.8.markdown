% CROND(8)
% 
% 20 Nov 2019

NAME
====
crond - dillon's lightweight cron daemon

SYNOPSIS
========
**crond [-s dir] [-c dir] [-t dir] [-m user@host] [-M mailhandler]
[-S|-L file] [-l loglevel] [-b|-f|-d]**

OPTIONS
=======
**crond** is a background daemon that parses individual crontab files and
executes commands on behalf of the users in question.

-s dir
:	directory of system crontabs (defaults to /etc/cron.d)

-c dir
:	directory of per-user crontabs (defaults to /var/spool/cron/crontabs)

-t dir
:	directory of timestamps for @freq and FREQ=... jobs
	(defaults to /var/spool/cron/cronstamps)

-m user@host
:	where should the output of cronjobs be directed? (defaults to local user)
	Some mail handlers (like msmtp) can't route mail to local users. If that's
	what you're using, then you should supply a remote address using this switch.
	Cron output for all users will be directed to that address. Alternatively, you
	could supply a different mail handler using the -M switch, to log or otherwise
	process the messages instead of mailing them. Alternatively, you could just
	direct the stdout and stderr of your cron jobs to /dev/null.

-M mailhandler
:	Any output that cronjobs print to stdout or stderr gets formatted as an email
	and piped to `/usr/sbin/sendmail -t -oem -i`. Attempts to mail this are also
	logged. This switch permits the user to substitute a different mailhandler,
	or a script, for sendmail. That custom mailhandler is called with no
	arguments, and with the mail headers and cronjob output supplied to
	stdin. When a custom mailhandler is used, mailing is no longer logged
	(have your mailhandler do that if you want it). When cron jobs generate no
	stdout or stderr, nothing is sent to either sendmail or a custom mailhandler.

-S
:	log events to syslog, using syslog facility LOG_CRON and identity 'crond' (this is the default behavior).

-L file
:	log to specified file instead of syslog.

-l loglevel
:	log events at the specified, or more important, loglevels. The default is
	'notice'. Valid level names are as described in logger(1) and syslog(3):
	alert, crit, debug, emerg, err, error (deprecated synonym for err), info,
	notice, panic (deprecated synonym for emerg), warning, warn (deprecated
	synonym for warning).

-b
:	run **crond** in the background (default unless -d or -f is specified)

-f
:	run **crond** in the foreground. All log messages are sent to stderr instead
	of syslog or a -L file.

-d
:	turn on debugging. This option sets the logging level to 'debug' and causes
	**crond** to run in the foreground.

DESCRIPTION
===========

**crond** is responsible for scanning the crontab files and running their
commands at the appropriate time. It always synchronizes to the top of the
minute, matching the current time against its internal list of parsed crontabs.
That list is stored so that it can be scanned very quickly, and **crond** can deal
with several hundred crontabs with several thousand entries without using noticeable CPU.


Cron jobs are not re-executed if a previous instance of them is still running.
For example, if you have a crontab command `sleep 70`, that you request to be
run every minute, **crond** will skip this job when it sees it is still
running. So the job won't be run more frequently than once every two minutes.
If you do not like this feature, you can run your commands in the background
with an `&`.

**crond** automatically detects when the clock has been changed, during its
per-minute scans. Backwards time-changes of an hour or less won't re-run cron
jobs from the intervening period. **crond** will effectively sleep until it
catches back up to the original time. Forwards time-changes of an hour or less
(or if the computer is suspended and resumed again within an hour) will run any
missed jobs exactly once. Changes greater than an hour in either direction
cause **crond** to re-calculate when jobs should be run, and not attempt to
execute any missed commands. This is effectively the same as if **crond** had
been stopped and re-started.



For example, suppose it's 10 am, and a job is scheduled to run every day at
10:30 am. If you set the system's clock forward to 11 am, crond will immediately run
the 10:30 job. If on the other hand you set the system's clock forward to noon,
the 10:30 am job will be skipped until the next day. Jobs scheduled using
@daily and the like work differently; see crontab(1) for details.



**crond** has a number of built in limitations to reduce the chance of it being
ill-used. Potentially infinite loops during parsing are dealt with via a
failsafe counter, and non-root crontabs are limited to 256 crontab
entries. Crontab lines may not be longer than 1024 characters, including the
newline.

Whenever **crond** must run a job, it first creates a daemon-owned temporary
file O_EXCL and O_APPEND to store any output, then fork()s and changes its user
and group permissions to match that of the user the job is being run for, then
**exec**s **/bin/sh -c <command>** to run the job. The temporary file remains
under the ownership of the daemon to prevent the user from tampering with it.
Upon job completion, **crond** verifies the secureness of the mail file and, if
it has been appended to, mails the file to the specified address. The **sendmail** program
(or custom mail handler, if supplied) is run under the user's uid to prevent mail
related security holes.

When a user edits their crontab, **crontab** first copies the
crontab to a user owned file before running the user's preferred editor. The
suid **crontab** keeps an open descriptor to the file which it later uses to
copy the file back, thereby ensuring the user has not tampered with the file
type.




**crontab** notifies **crond** that a user's crontab file has been
modified (or created or deleted) through the "cron.update" file, which resides
in the per-user crontabs directory (usually /var/spool/cron/crontabs). **crontab**
appends the filename of the modified crontab file to "cron.update"; and
**crond** inspects this file to determine when to reparse or otherwise update
its internal list of parsed crontabs.

Whenever a "cron.update" file is seen, **crond** also re-reads timestamp
files from its timestamp directory (usually /var/spool/cron/cronstamps). Normally
these will just mirror **crond**'s own internal representations, but this
mechanism could be used to manually notify **crond** that you've externally
updated the timestamps.

The "cron.update" file can also be used to ask **crond** to schedule a "named"
cron job. To do this, append a line of the form:

	clio job1 !job2

to "cron.update". This request that user clio's job1 should be scheduled
(waiting first for the successful completion of any jobs named in job1's AFTER=
tag), and job2 should also be scheduled (without waiting for other jobs). See
crontab(1) for more about tags and named jobs.



The directory of per-user crontabs is re-parsed once every hour in any case.
Any crontabs in the system directory (usually /etc/cron.d) are parsed at the
same time. This directory can be used by packaging systems. When you install a
package foo, it might write its own foo-specific crontab to /etc/cron.d/foo.

The superuser has a per-user crontab along with other users. It usually resides
at /var/spool/cron/crontabs/root.

Users can only have a crontab if they have an entry in /etc/passwd; however
they do not need to have login shell privileges. Cron jobs are always run under
/bin/sh; see crontab(1) for more details.



Unlike **crontab**, the **crond** program does not keep open descriptors to
crontab files while running their jobs, as this could cause **crond** to run
out of descriptors.


SEE ALSO
========
**crontab**(1)
**crontab**(5)

AUTHORS
=======
Matthew Dillon (dillon@apollo.backplane.com): original developer  
James Pryor (dubiousjim@gmail.com): current developer
