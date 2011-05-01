
/*
 * DEFS.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2009-2011 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

/*
 * portability issues
 * 0. gcc defaults to _BSD_SOURCE and _POSIX_SOURCE
 * 1. need _POSIX_SOURCE or _XOPEN_SOURCE for getopt, fileno, sigaction
 * 2. need _XOPEN_SOURCE for strptime
 * 3. need _BSD_SOURCE || _XOPEN_SOURCE >= 600 for setenv, [v]snprintf, setre{u,g}id, strdup, gethostname, mkstemp, fsync
 * 4. need _BSD_SOURCE for mkdtemp, initgroups, strsep
 * 5. use stringdupmany in utils.c instead of requiring asprintf / _GNU_SOURCE
 * 6. use stringdup in utils.c instead of requiring strndup / _GNU_SOURCE
 * 7. use stringcpy in utils.c as more useful strncpy
 * 8. use [v]stringf in utils.c to ensure C99-ish behavior for [v]sprintf
 */

/* see /usr/include/features.h */
#define _XOPEN_SOURCE 1
#define _BSD_SOURCE 1

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#ifndef S_SPLINT_S
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#include <grp.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <err.h>
#include <limits.h>

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define Prototype extern
#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

#ifndef SCRONTABS
#define SCRONTABS	"/etc/cron.d"
#endif
#ifndef CRONTABS
#define CRONTABS	"/var/spool/cron"
#endif
#ifndef CRONSTAMPS
#define CRONSTAMPS	"/var/spool/cronstamps"
#endif
#ifndef LOG_IDENT
#define LOG_IDENT	"crond"
#endif
#ifndef TIMESTAMP_FMT
#define TIMESTAMP_FMT	"%b %e %H:%M:%S"
#endif

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_NOTICE
#endif
#ifndef CRONSTAMP_FMT
#define CRONSTAMP_FMT	"%Y-%m-%d %H:%M"
#endif
#ifndef CRONUPDATE
#define CRONUPDATE	"cron.update"
#endif
#ifndef TMPDIR
#define TMPDIR		"/tmp"
#endif

#ifndef SENDMAIL
#define SENDMAIL	"/usr/sbin/sendmail"
#endif
#ifndef SENDMAIL_ARGS
#define SENDMAIL_ARGS	"-t", "-oem", "-i"
#endif
#ifndef PATH_VI
#define PATH_VI		"/usr/bin/vi"	/* location of vi */
#endif

#ifndef ID_TAG
#define ID_TAG			"ID="
#endif
#ifndef WAIT_TAG
#define WAIT_TAG		"AFTER="
#endif
#ifndef FREQ_TAG
#define FREQ_TAG		"FREQ="
#endif

#define HOURLY_FREQ		60 * 60
#define DAILY_FREQ		24 * HOURLY_FREQ
#define WEEKLY_FREQ		7 * DAILY_FREQ
#define MONTHLY_FREQ	30 * DAILY_FREQ
#define YEARLY_FREQ		365 * DAILY_FREQ

#define LOGHEADER TIMESTAMP_FMT " %%s " LOG_IDENT ": "
#define LOCALE_LOGHEADER "%c %%s " LOG_IDENT ": "

/* limits */
#define FD_MAX			256		/* close fds < this limit */
#define LINES_MAX		256		/* max lines in non-root crontabs */
#define SMALL_BUF		256
#define LINE_BUF		1024	/* max size of log and crontab lines */

#define UNUSED(x) /*@-noeffect@*/ (void)x /*@=noeffect@*/

#ifndef __GNUC__
#ifndef S_SPLINT_S
#define __attribute__(attrlist) /*nothing*/
#endif
#endif

/* types */

/* bool is a keyword in C++ */
/*@-cppnames@*/
typedef int bool;
/*@=cppnames@*/

#ifndef FALSE
#define FALSE ((bool)0)
#endif

#ifndef TRUE
#define TRUE ((bool)!FALSE)
#endif

typedef /*@null@*/ const char *STRING;
typedef unsigned long hash_t;

#define FREQ_NOAUTO (-1)
#define FREQ_REBOOT (-2)

#define PID_ARMED (-1)
#define PID_WAITING (-2)


typedef struct CronFile {
	/*@null@*/ /*@dependent@*/ struct CronFile *cf_Next;
	/*@null@*/ /*@owned@*/ struct CronLine *cf_LineBase;
	char *cf_DPath;		/* directory path to cronfile */
	char *cf_FileName;	/* name of cronfile */
	char *cf_UserName;	/* username to execute jobs as */
	bool cf_Ready;		/* one or more jobs ready */
	bool cf_Running;	/* one or more jobs running */
	bool cf_Deleted;	/* marked for deletion, ignore */
} CronFile;

typedef struct CronLine {
	/*@null@*/ /*@dependent@*/ struct CronLine *cl_Next;
	/*@reldef@*/ char *cl_Shell;			/* shell command */
	/*@reldef@*/ char *cl_Description;	/* either "<cl_Shell>" or "job <cl_JobName>" */
	/*@reldef@*/ char *cl_JobName;	/* job name, if any */
	/*@null@*/ char *cl_Timestamp;		/* path to timestamp file, if cl_Freq defined */
	/*@null@*/ /*@owned@*/ struct CronWaiter *cl_Waiters;
	/*@null@*/ /*@owned@*/ struct CronNotifier *cl_Notifs;
	time_t cl_Freq;		/* 0=use arrays, >0 minutes, noauto, reboot */
	time_t cl_Delay;	/* defaults to cl_Freq or hourly */
	time_t cl_LastRan;
	time_t cl_NotUntil;
	pid_t cl_Pid;		/* running pid, 0, or armed (-1), or waiting (-2) */
	bool cl_MailFlag;	/* running pid is for mail */
	off_t cl_MailPos;	/* 'empty file' size */
	short cl_Mins[60];	/* 0-59 */
	short cl_Hrs[24];	/* 0-23 */
	short cl_Days[32];	/* 1-31 */
	short cl_Mons[12];	/* 0-11 */
	short cl_Dow[7];	/* 0-6, beginning sunday */
} CronLine;

typedef /*@null@*/ /*@owned@*/ CronFile *CronFile_p;
typedef /*@null@*/ /*@owned@*/ CronLine *CronLine_p;

typedef struct CronWaiter {
	/*@null@*/ /*@dependent@*/ struct CronWaiter *cw_Next;
	/*@dependent@*/ struct CronNotifier *cw_Notifier;
	/*@dependent@*/ struct CronLine *cw_NotifLine;
	int cw_Flag;
	time_t cw_MaxWait;
} CronWaiter;

typedef struct CronNotifier {
	/*@null@*/ /*@dependent@*/ struct CronNotifier *cn_Next;
	/*@dependent@*/ struct CronWaiter *cn_Waiter;
} CronNotifier;

#include "protos.h"

