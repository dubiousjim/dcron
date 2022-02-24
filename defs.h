
/*
 * DEFS.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2009-2019 James Pryor <dubiousjim@gmail.com>
 * May be distributed under the GNU General Public License version 2 or any later version.
 */

/*
 * portability issues
 * 0. gcc defaults to _BSD_SOURCE and _POSIX_SOURCE
 * 1. need _POSIX_SOURCE or _XOPEN_SOURCE for getopt, fileno, sigaction
 * 2. need _XOPEN_SOURCE for strptime
 * 3. need _BSD_SOURCE for setenv, mk{d,s}temp, [v]snprintf, initgroups, strsep, strdup, setre{u,g}id, gethostname, perror
 * 4. use concat.c instead of requiring asprintf / _GNU_SOURCE
 */

#define _XOPEN_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#ifndef S_SPLINT_S
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

#define Prototype extern
#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

#ifndef SCRONTABS
#define SCRONTABS	"/etc/cron.d"
#endif
#ifndef CRONTABS
#define CRONTABS	"/var/spool/cron/crontabs"
#endif
#ifndef CRONSTAMPS
#define CRONSTAMPS	"/var/spool/cron/cronstamps"
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
#define PATH_VI		"/usr/bin/vi"	/* location of vi	*/
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
#define	WEEKLY_FREQ		7 * DAILY_FREQ
#define MONTHLY_FREQ	30 * DAILY_FREQ
#define YEARLY_FREQ		365 * DAILY_FREQ

#define FIELD_MINUTES   60
#define FIELD_HOURS     24
#define FIELD_M_DAYS    32
#define FIELD_MONTHS    12
#define FIELD_W_DAYS     7

#define JOB_NONE        0
#define JOB_ARMED       -1
#define JOB_WAITING     -2

#define LOGHEADER TIMESTAMP_FMT " %%s " LOG_IDENT ": "
#define LOCALE_LOGHEADER "%c %%s " LOG_IDENT ": "

/* Limits */
#define MAXOPEN			256		/* close fds < this limit */ 
#define MAXLINES		256		/* max lines in non-root crontabs */
#define SMALL_BUFFER	256
#define RW_BUFFER		1024
#define LOG_BUFFER		2048 	/* max size of log line */

typedef struct CronFile {
    struct CronFile *cf_Next;
    struct CronLine *cf_LineBase;
    char	*cf_DPath;	/* Directory path to cronfile */
    char	*cf_FileName;	/* Name of cronfile */
    char	*cf_UserName;	/* username to execute jobs as */
    int		cf_Ready;	/* bool: one or more jobs ready	*/
    int		cf_Running;	/* bool: one or more jobs running */
    int		cf_Deleted;	/* marked for deletion, ignore	*/
} CronFile;

typedef struct CronLine {
    struct CronLine *cl_Next;
    char	*cl_Shell;	/* shell command			*/
    char        *cl_UserName;   /* to execute as                        */
	char	*cl_Description;	/* either "<cl_Shell>" or "job <cl_JobName>" */
	char	*cl_JobName;	/* job name, if any			*/
	char	*cl_Timestamp;	/* path to timestamp file, if cl_Freq defined */
	struct	CronWaiter *cl_Waiters;
	struct	CronNotifier *cl_Notifs;
	int		cl_Freq;		/* 0 (use arrays),  minutes, -1 (noauto), -2 (startup)	*/
	int		cl_Delay;		/* defaults to cl_Freq or hourly	*/
	time_t	cl_LastRan;
	time_t	cl_NotUntil;
	int		cl_Pid;			/* running pid, 0, or armed (-1), or waiting (-2) */
    int		cl_MailFlag;	/* running pid is for mail		*/
    int		cl_MailPos;	/* 'empty file' size			*/
    char	cl_Mins[FIELD_MINUTES];	/* 0-59				*/
    char	cl_Hrs[FIELD_HOURS];	/* 0-23					*/
    char	cl_Days[FIELD_M_DAYS];	/* 1-31					*/
    char	cl_Mons[FIELD_MONTHS];	/* 0-11				*/
    char	cl_Dow[FIELD_W_DAYS];	/* 0-6, beginning sunday		*/
} CronLine;

typedef struct CronWaiter {
	struct	CronWaiter *cw_Next;
	struct	CronNotifier *cw_Notifier;
	struct	CronLine *cw_NotifLine;
	short	cw_Flag;
	int		cw_MaxWait;
} CronWaiter;

typedef struct CronNotifier {
	struct	CronNotifier *cn_Next;
	struct	CronWaiter *cn_Waiter;
} CronNotifier;

#include "protos.h"

