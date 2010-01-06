% CRONTAB(1)
% 
% 6 Jan 2010

NAME
====
crontab - manipulate per-user crontabs (Yet Another Cron)

SYNOPSIS
========
**crontab file [-u user]** - replace crontab from file

**crontab - [-u user]** - replace crontab from stdin

**crontab -l [user]** - list crontab for user

**crontab -e [user]** - edit crontab for user

**crontab -d [user]** - delete crontab for user

**crontab -c dir** - specify crontab directory

DESCRIPTION
===========
**crontab** manipulates the crontab for a particular user. Only the superuser
may specify a different user and/or crontab directory. Generally the -e option
is used to edit your crontab. **crontab** will use /usr/bin/vi or the editor
specified by your EDITOR or VISUAL environment variable to edit the crontab.

Unlike other crond/crontabs, this **crontab** does not try to do everything under
the sun. Frankly, a shell script is much more able to manipulate the
environment then cron and I see no particular reason to use the user's shell
(from his password entry) to run cron commands when this requires special
casing of non-user crontabs, such as those for UUCP. When a crontab command is
run, this **crontab** runs it with /bin/sh and sets up only three environment
variables: USER, HOME, and SHELL.

**crond** automatically detects changes in the time. Reverse-indexed time changes
less then an hour old will NOT re-run crontab commands already issued in the
recovered period. Forward-indexed changes less then an hour into the future
will issue missed commands exactly once. Changes greater then an hour into the
past or future cause **crond** to resynchronize and not issue missed commands.
Commands are not reissued if the previously issued command is still running.
For example, if you have a crontab command `sleep 70` that you wish to run once
a minute, cron will only be able to issue the command once every two minutes.
If you do not like this feature, you can run your commands in the background
with an `&`.

The crontab format is roughly similar to that used by vixiecron. Individual
fields may contain a time, a time range, a time range with a skip factor, a
symbolic range for the day of week and month in year, and additional subranges
delimited with commas. Blank lines in the crontab or lines that begin with a
hash (#) are ignored. If you specify both a day in the month and a day of week,
it will be interpreted as the nth such day in the month.

	# MIN HOUR DAY MONTH DAYOFWEEK	COMMAND
	# at 6:10 a.m. every day
	10 6 * * * date

	# every two hours at the top of the hour
	0 */2 * * * date

	# every two hours from 11p.m. to 7a.m., and at 8a.m.
	0 23-7/2,8 * * * date

	# at 11:00 a.m. on the first and last Mon, Tue, Wed of each month
	# if the fourth Monday in a month is the last, it will
	# match against both "4th" and "5th"
	0 11 1,5 * mon-wed date

	# 4:00 a.m. on january 1st
	0 4 1 jan * date

	# once an hour, all output appended to log file
	0 4 1 jan * date >>/var/log/messages 2>&1

The following formats are also recognized:

	# schedule only once, when crond starts up
	@reboot date

	# never schedule regularly, but only when triggered by other
	# jobs, or through the "cron.update" file
	@noauto date

	# schedule whenever at least one hour has elapsed since
	# it last ran successfully
	@hourly ID=job1 date

The options @hourly, @daily, @weekly, @monthly, and @yearly update timestamp
files when their job completes successfully (exits with code zero). The
timestamp files are saved as /var/spool/cron/timestamps/user.jobname. (So for
all of these options, the command must be prefixed with an ID=<jobname>.)

Frequencies can also be specified as follows:

	# run whenever it's between 2-4 am, and at least one day
	# has elapsed since it last ran successfully
	* 2-4 * * * ID=job2 FREQ=1d date

	# as before, but re-try every 10 minutes if my_command
	# exits with status EAGAIN (code 11)
	* 2-4 * * * ID=job3 FREQ=1d/10m my_command

These options also update timestamp files, and require the jobs to be assigned
an ID.

Jobs can be told to wait for the successful completion of other jobs. With the
following crontab:

	* * * * * ID=job5 FREQ=1d first_command
	* * * * * ID=job6 FREQ=1h AFTER=job5/1h second_command

whenever job6 is about to be scheduled, if job5 would be scheduled within the
next hour, job6 will first wait for it to successfully complete. If job5
returns with exit code EAGAIN, job6 will continue waiting (even if job5 will
not be retried again within an hour). If job5 returns with any other non-zero
exit code, job6 will be removed from the queue without running.

Jobs can be told to wait for multiple other jobs, as follows:

	10 * * * * ID=job7 AFTER=job5/1h,job4 third_command

The waiting job doesn't care what order job4 and job5 complete in. If job7 is
re-scheduled (an hour later) while an earlier instance is still waiting, only a
single instance of the job will remain in the queue. It will have all of its
"waiting flags" reset: so each of job4 and job5 (if job5 would run within the
next hour) will again have to complete before job7 will run.

If a job waits on a @reboot or @noauto job, the target job being waited on will
also be scheduled to run.

The command portion of the line is run with `/bin/sh -c <command>` and may
therefore contain any valid bourne shell command. A common practice is to run
your command with **exec** to keep the process table uncluttered. It is also common
to redirect output to a log file. If you do not, and the command generates
output on stdout or stderr, the result will be mailed to the user in question.
If you use this mechanism for special users, such as UUCP, you may want to
create an alias for the user to direct the mail to someone else, such as root
or postmaster.

Internally, this cron uses a quick indexing system to reduce CPU overhead when
looking for commands to execute. Several hundred crontabs with several thousand
entries can be handled without using noticable CPU resources.

BUGS
====
Ought to be able to have several crontab files for any given user, as
an organizational tool.

AUTHORS
=======
Matthew Dillon (dillon@apollo.backplane.com)  
Jim Pryor (profjim@jimpryor.net)
