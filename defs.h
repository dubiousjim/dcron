
/*
 * DEFS.H
 *
 * Copyright 1994-1998 Matthew Dillon (dillon@backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <grp.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <err.h>

#define Prototype extern
#define arysize(ary)	(sizeof(ary)/sizeof((ary)[0]))

#ifndef CRONTABS
#define CRONTABS	"/var/spool/cron"
#endif
#ifndef SCRONTABS
#define SCRONTABS	"/etc/cron.d"
#endif
#ifndef TMPDIR
#define TMPDIR		"/tmp"
#endif
#ifndef LOG_FILE
#define LOG_FILE	"/var/log/crond.log"
#endif
#ifndef LOG_IDENT
#define LOG_IDENT	"crond"
#endif
#ifndef LOG_DATE_FMT
#define LOG_DATE_FMT	"%b %e %H:%M:%S %%s " LOG_IDENT ": "
#endif
#ifndef OPEN_MAX
#define OPEN_MAX	256
#endif

#ifndef SENDMAIL
#define SENDMAIL	"/usr/sbin/sendmail"
#endif
#ifndef SENDMAIL_ARGS
#define SENDMAIL_ARGS	"-t", "-oem", "-i"
#endif
#ifndef CRONUPDATE
#define CRONUPDATE	"cron.update"
#endif
#ifndef MAXLINES
#define MAXLINES	256		/* max lines in non-root crontabs */
#endif
#ifndef PATH_VI
#define PATH_VI		"/usr/bin/vi"	/* location of vi	*/
#endif

#define VERSION	"V4.0b1"

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
    int		cl_Pid;		/* running pid, 0, or armed (-1)	*/
    int		cl_MailFlag;	/* running pid is for mail		*/
    int		cl_MailPos;	/* 'empty file' size			*/
    char	cl_Mins[60];	/* 0-59				*/
    char	cl_Hrs[24];	/* 0-23					*/
    char	cl_Days[32];	/* 1-31					*/
    char	cl_Mons[12];	/* 0-11				*/
    char	cl_Dow[7];	/* 0-6, beginning sunday		*/
} CronLine;

// #define RUN_RANOUT	1
// #define RUN_RUNNING	2
// #define RUN_FAILED	3

#include "protos.h"

