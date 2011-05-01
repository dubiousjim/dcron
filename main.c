
/*
 * MAIN.C
 *
 * crond [-s dir] [-c dir] [-t dir] [-m user@host] [-M mailhandler] [-S|-L [file]] [-l loglevel] [-b|-f|-d]
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2011 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype bool DebugOpt;
Prototype int LogLevel;
Prototype bool ForegroundOpt;
Prototype bool SyslogOpt;
Prototype /*@observer@*/ const char *CDir;
Prototype /*@observer@*/ const char *SCDir;
Prototype /*@observer@*/ const char *TSDir;
Prototype STRING LogFile;
Prototype /*@observer@*/ const char *LogHeader;
Prototype uid_t DaemonUid;
Prototype pid_t DaemonPid;
Prototype /*@observer@*/ STRING SendMail;
Prototype STRING Mailto;
Prototype char *TempDir;
Prototype char *TempFileFmt;
Prototype /*@observer@*/ const char progname[];

bool DebugOpt = FALSE;
int LogLevel = LOG_LEVEL;
bool ForegroundOpt = FALSE;
bool SyslogOpt = TRUE;
/*@observer@*/ const char  *CDir = CRONTABS;
/*@observer@*/ const char  *SCDir = SCRONTABS;
/*@observer@*/ const char *TSDir = CRONSTAMPS;
STRING LogFile = NULL; 	/* opened with mode 0600 */
/*@observer@*/ const char *LogHeader = LOGHEADER;
/*@observer@*/ STRING SendMail = NULL;
STRING Mailto = NULL;
char *TempDir;
char *TempFileFmt;
/*@observer@*/ const char progname[] = "crond";

uid_t DaemonUid;
pid_t DaemonPid;

int
main(int ac, char **av) /*@requires maxRead(av) >= ( ac - 1) /\ maxRead(av) >= 0;@*/
{
	/*@observer@*/ STRING LevelAry[] = {
		"emerg",
		"alert",
		"crit",
		"err",
		"warning",
		"notice",
		"info",
		"debug",
		"panic",
		"error",
		"warn",
		NULL
	};
	int i;
	assert(LOG_LEVEL<11);
	/*@-boundsread@*/
	assert(LevelAry[0]!=NULL);
	assert(LevelAry[LOG_LEVEL]!=NULL);
	/*@=boundsread@*/

	/*
	 * parse options
	 */

	DaemonUid = getuid();

	opterr = 0;

	while ((i = getopt(ac,av,"dl:L:fbSc:s:m:M:t:")) != -1) {
		switch (i) {
			case 'l':
				{
					char *ptr;
					int j;
					ptr = optarg;
					for (j = 0; LevelAry[j]; ++j) {
						if (strncmp(ptr, LevelAry[j], strlen(LevelAry[j])) == 0) {
							/*@innerbreak@*/
							break;
						}
					}
					switch(j) {
						case 0:
						case 8:
							/* #define	LOG_EMERG	0	[* system is unusable *] */
							LogLevel = LOG_EMERG;
							break;
						case 1:
							/* #define	LOG_ALERT	1	[* action must be taken immediately *] */
							LogLevel = LOG_ALERT;
							break;
						case 2:
							/* #define	LOG_CRIT	2	[* critical conditions *] */
							LogLevel = LOG_CRIT;
							break;
						case 3:
						case 9:
							/* #define	LOG_ERR		3	[* error conditions *] */
							LogLevel = LOG_ERR;
							break;
						case 4:
						case 10:
							/* #define	LOG_WARNING	4	[* warning conditions *] */
							LogLevel = LOG_WARNING;
							break;
						case 5:
							/* #define	LOG_NOTICE	5	[* normal but significant condition *] */
							LogLevel = LOG_NOTICE;
							break;
						case 6:
							/* #define	LOG_INFO	6	[* informational *] */
							LogLevel = LOG_INFO;
							break;
						case 7:
							/* #define	LOG_DEBUG	7	[* debug-level messages *] */
							LogLevel = LOG_DEBUG;
							break;
						default:
							(void)dprintf(2, "-l option: unrecognized loglevel %s\n", optarg);
							exit(EXIT_FAILURE);
					}
				}
				break;
			case 'd':
				DebugOpt = TRUE;
				LogLevel = LOG_DEBUG;
				/*@fallthrough@*/
			case 'f':
				ForegroundOpt = TRUE;
				break;
			case 'b':
				ForegroundOpt = FALSE;
				break;
			case 'S':			/* log through syslog */
				SyslogOpt = TRUE;
				break;
			case 'L':			/* use internal log formatter */
				SyslogOpt = FALSE;
				LogFile = optarg;
				/* if LC_TIME is defined, we use it for logging to file instead of compiled-in TIMESTAMP_FMT */
				if (getenv("LC_TIME") != NULL) {
					LogHeader = LOCALE_LOGHEADER;
				}
				break;
			/*@-boundsread@*/
			case 'c':
				if (*optarg != '\0') CDir = optarg;
				break;
			case 's':
				if (*optarg != '\0') SCDir = optarg;
				break;
			case 't':
				if (*optarg != '\0') TSDir = optarg;
				break;
			case 'M':
				if (*optarg != '\0') SendMail = optarg;
				break;
			case 'm':
				if (*optarg != '\0') Mailto = optarg;
				break;
			/*@=boundsread@*/
			default:
				/*
				 * check for parse error
				 */
				printf("dillon's cron daemon " VERSION "\n");
				printf("crond [-s dir] [-c dir] [-t dir] [-m user@host] [-M mailhandler] [-S|-L [file]] [-l loglevel] [-b|-f|-d]\n");
				printf("-s             directory of system crontabs (defaults to %s)\n", SCRONTABS);
				printf("-c             directory of per-user crontabs (defaults to %s)\n", CRONTABS);
				printf("-t             directory of timestamps (defaults to %s)\n", CRONSTAMPS);
				printf("-m user@host   where should cronjobs' output be directed? (defaults to local user)\n");
				printf("-M mailhandler (defaults to %s)\n", SENDMAIL);
				printf("-S             log to syslog using identity '%s' (default)\n", LOG_IDENT);
				printf("-L file        log to specified file instead of syslog\n");
				printf("-l loglevel    log events at specified loglevel (defaults to %s)\n", LevelAry[LOG_LEVEL]);
				printf("-b             run in background (default)\n");
				printf("-f             run in foreground and log to stderr\n");
				printf("-d             run in debugging mode\n");
				exit(EXIT_FAILURE);
		}
	}

	/*
	 * close stdin and stdout.
	 * close unused descriptors -  don't need.
	 * optional detach from controlling terminal
	 */

	(void)fclose(stdin);
	(void)fclose(stdout);

	i = open("/dev/null", O_RDWR);
	if (i < 0) {
		perror("open: /dev/null");
		exit(EXIT_FAILURE);
	}
	(void)dup2(i, 0);
	(void)dup2(i, 1);

	/* create tempdir with permissions 0755 for cron output */
	TempDir = stringdup(TMPDIR "/cron.XXXXXX", PATH_MAX);
	if (chmod(TempDir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) {
		perror("chmod");
		exit(EXIT_FAILURE);
	}
	TempFileFmt = stringdupmany(TempDir, "/cron.%s.%d", (char *)NULL);

	if (!ForegroundOpt) {

		int fd;
		pid_t pid;

		if ((pid = fork()) < 0) {
			/* fork failed */
			perror("fork");
			exit(EXIT_FAILURE);
		} else if (pid > 0) {
			/* parent */
			exit(EXIT_SUCCESS);
		}
		/* child continues */

		/* become session leader, detach from terminal */

		if (setsid() < 0)
			perror("setsid");
		if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
			/*@-nullpass@*/
			(void)ioctl(fd, (unsigned long)TIOCNOTTY, 0);
			/*@=nullpass@*/
			(void)close(fd);
		}

		/* setup logging for backgrounded daemons */

		if (SyslogOpt) {
			/* start SIGHUP and SIGCHLD handling while stderr still open */
			initsignals();
			/* 2> /dev/null */
			(void)fclose(stderr);
			(void)dup2(1, 2);

			/* open syslog */
			openlog(LOG_IDENT, LOG_CONS|LOG_PID, LOG_CRON);

		} else {
			/* open logfile */
			assert(LogFile!=NULL);
			if ((fd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) >= 0) {
				/* start SIGHUP ignoring, SIGCHLD handling while stderr still open */
				initsignals();
				/* 2> LogFile */
				(void)fclose(stderr);
				(void)dup2(fd, 2);
			} else {
				(void)dprintf(2, "crond: opening logfile %s failed: %s\n", LogFile, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
	} else {
		/* daemon in foreground */

		/* stay in existing session, but start a new process group */
		if (setpgid(0,0)) {
			perror("setpgid");
			exit(EXIT_FAILURE);
		}

		/* stderr stays open, start SIGHUP ignoring, SIGCHLD handling */
		initsignals();
	}

	/* close all other fds, including the ones we opened as /dev/null and LogFile */
	for (i = 3; i < FD_MAX; ++i) {
        (void)close(i);
    }


	/*
	 * main loop - synchronize to 1 second after the minute, minimum sleep
	 *             of 1 second.
	 */

	/*@-boundsread@*/
	logger(LOG_NOTICE,"%s " VERSION " dillon's cron daemon, started with loglevel %s\n", av[0], LevelAry[LogLevel]);
	/*@=boundsread@*/
	SynchronizeDir(CDir, NULL, 1);
	SynchronizeDir(SCDir, "root", 1);
	ReadTimestamps(NULL);
	(void)TestStartupJobs(); /* @startup jobs only run when crond is started, not when their crontab is loaded */

	{
		time_t t1 = time(NULL);
		time_t t2, dt;
		int rescan = 60;
		int stime = 60;

		for (;;) {
			(void)sleep((unsigned)((stime + 1) - (short)(time(NULL) % stime)));

			t2 = time(NULL);
			dt = t2 - t1;

			/*
			 * The file 'cron.update' is checked to determine new cron
			 * jobs.  The directory is rescanned once an hour to deal
			 * with any screwups.
			 *
			 * check for disparity.  Disparities over an hour either way
			 * result in resynchronization.  A reverse-indexed disparity
			 * less then an hour causes us to effectively sleep until we
			 * match the original time (i.e. no re-execution of jobs that
			 * have just been run).  A forward-indexed disparity less then
			 * an hour causes intermediate jobs to be run, but only once
			 * in the worst case.
			 *
			 * when running jobs, the inequality used is greater but not
			 * equal to t1, and less then or equal to t2.
			 */

			if (--rescan == 0) {
				/*
				 * If we resynchronize while jobs are running, we'll clobber
				 * the job pids, so we won't know what's already running.
				 */
				if (CheckJobs() > 0) {
					rescan = 1;
				} else {
					rescan = 60;
					SynchronizeDir(CDir, NULL, 0);
					SynchronizeDir(SCDir, "root", 0);
					ReadTimestamps(NULL);
				}
			} else {
				CheckUpdates(CDir, NULL, t1, t2);
				CheckUpdates(SCDir, "root", t1, t2);
			}
			if (DebugOpt)
				logger(LOG_DEBUG, "Wakeup dt=%d\n", dt);
			if (dt < -60*60 || dt > 60*60) {
				t1 = t2;
				logger(LOG_NOTICE,"time disparity of %d minutes detected\n", dt / 60);
			} else if (dt > 0) {
				(void)TestJobs(t1, t2);
				RunJobs();
				(void)sleep((unsigned)5);
				if (CheckJobs() > 0)
					stime = 10;
				else
					stime = 60;
				t1 = t2;
			}
		}
	}
	/* not reached */
}

