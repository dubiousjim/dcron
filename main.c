
/*
 * MAIN.C
 *
 * crond [-s dir] [-c dir] [-t dir] [-m user@host] [-M mailer] [-S|-L [file]] [-l level] [-b|-f|-d]
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2019 James Pryor <dubiousjim@gmail.com>
 * May be distributed under the GNU General Public License version 2 or any later version.
 */

#include "defs.h"

Prototype short DebugOpt;
Prototype short LogLevel;
Prototype short ForegroundOpt;
Prototype short SyslogOpt;
Prototype const char *CDir;
Prototype const char *SCDir;
Prototype const char *TSDir;
Prototype const char *LogFile;
Prototype const char *LogHeader;
Prototype uid_t DaemonUid;
Prototype pid_t DaemonPid;
Prototype const char *SendMail;
Prototype const char *Mailto;
Prototype char *TempDir;
Prototype char *TempFileFmt;

short DebugOpt = 0;
short LogLevel = LOG_LEVEL;
short ForegroundOpt = 0;
short SyslogOpt = 1;
const char  *CDir = CRONTABS;
const char  *SCDir = SCRONTABS;
const char *TSDir = CRONSTAMPS;
const char *LogFile = NULL; 	/* opened with mode 0600 */
const char *LogHeader = LOGHEADER;
const char *SendMail = NULL;
const char *Mailto = NULL;
char *TempDir;
char *TempFileFmt;

uid_t DaemonUid;
pid_t DaemonPid;

int
main(int ac, char **av)
{
	const char *LevelAry[] = {
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
							LogLevel = atoi(optarg);
					}
				}
				break;
			case 'd':
				DebugOpt = 1;
				LogLevel = LOG_DEBUG;
				/* fall through to include f too */
			case 'f':
				ForegroundOpt = 1;
				break;
			case 'b':
				ForegroundOpt = 0;
				break;
			case 'S':			/* log through syslog */
				SyslogOpt = 1;
				break;
			case 'L':			/* use internal log formatter */
				SyslogOpt = 0;
				LogFile = optarg;
				/* if LC_TIME is defined, we use it for logging to file instead of compiled-in TIMESTAMP_FMT */
				if (getenv("LC_TIME") != NULL) {
					LogHeader = LOCALE_LOGHEADER;
				}
				break;
			case 'c':
				if (*optarg != 0) CDir = optarg;
				break;
			case 's':
				if (*optarg != 0) SCDir = optarg;
				break;
			case 't':
				if (*optarg != 0) TSDir = optarg;
				break;
			case 'M':
				if (*optarg != 0) SendMail = optarg;
				break;
			case 'm':
				if (*optarg != 0) Mailto = optarg;
				break;
			default:
				/*
				 * check for parse error
				 */
				printf("dillon's cron daemon " VERSION "\n");
				printf("crond [-s dir] [-c dir] [-t dir] [-m user@host] [-M mailer] [-S|-L [file]] [-l level] [-b|-f|-d]\n");
				printf("-s            directory of system crontabs (defaults to %s)\n", SCRONTABS);
				printf("-c            directory of per-user crontabs (defaults to %s)\n", CRONTABS);
				printf("-t            directory of timestamps (defaults to %s)\n", CRONSTAMPS);
				printf("-m user@host  where should cron output be directed? (defaults to local user)\n");
				printf("-M mailer     (defaults to %s)\n", SENDMAIL);
				printf("-S            log to syslog using identity '%s' (default)\n", LOG_IDENT);
				printf("-L file       log to specified file instead of syslog\n");
				printf("-l loglevel   log events <= this level (defaults to %s (level %d))\n", LevelAry[LOG_LEVEL], LOG_LEVEL);
				printf("-b            run in background (default)\n");
				printf("-f            run in foreground\n");
				printf("-d            run in debugging mode\n");
				exit(2);
		}
	}

	/*
	 * close stdin and stdout.
	 * close unused descriptors -  don't need.
	 * optional detach from controlling terminal
	 */

	fclose(stdin);
	fclose(stdout);

	i = open("/dev/null", O_RDWR);
	if (i < 0) {
		perror("open: /dev/null");
		exit(1);
	}
	dup2(i, 0);
	dup2(i, 1);

	/* create tempdir with permissions 0755 for cron output */
	TempDir = strdup(TMPDIR "/cron.XXXXXX");
	if (mkdtemp(TempDir) == NULL) {
		perror("mkdtemp");
		exit(1);
	}
	if (chmod(TempDir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) {
		perror("chmod");
		exit(1);
	}
	if (!(TempFileFmt = concat(TempDir, "/cron.%s.%d", NULL))) {
		errno = ENOMEM;
		perror("main");
		exit(1);
	}

	if (ForegroundOpt == 0) {

		int fd;
		int pid;

		if ((pid = fork()) < 0) {
			/* fork failed */
			perror("fork");
			exit(1);
		} else if (pid > 0) {
			/* parent */
			exit(0);
		}
		/* child continues */

		/* become session leader, detach from terminal */

		if (setsid() < 0)
			perror("setsid");
		if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
			ioctl(fd, TIOCNOTTY, 0);
			close(fd);
		}

		/* setup logging for backgrounded daemons */

		if (SyslogOpt) {
			/* start SIGHUP and SIGCHLD handling while stderr still open */
			initsignals();
			/* 2> /dev/null */
			fclose(stderr);
			dup2(1, 2);

			/* open syslog */
			openlog(LOG_IDENT, LOG_CONS|LOG_PID, LOG_CRON);

		} else {
			/* open logfile */
			if ((fd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) >= 0) {
				/* start SIGHUP ignoring, SIGCHLD handling while stderr still open */
				initsignals();
				/* 2> LogFile */
				fclose(stderr);
				dup2(fd, 2);
			} else {
				int n = errno;
				fdprintf(2, "failed to open logfile '%s', reason: %s", LogFile, strerror(n));
				exit(n);
			}
		}
	} else {
		/* daemon in foreground */

		/* stay in existing session, but start a new process group */
		if (setpgid(0,0)) {
			perror("setpgid");
			exit(1);
		}

		/* stderr stays open, start SIGHUP ignoring, SIGCHLD handling */
		initsignals();
	}

	/* close all other fds, including the ones we opened as /dev/null and LogFile */
	for (i = 3; i < MAXOPEN; ++i) {
        close(i);
    }


	/*
	 * main loop - synchronize to 1 second after the minute, minimum sleep
	 *             of 1 second.
	 */

	printlogf(LOG_NOTICE,"%s " VERSION " dillon's cron daemon, started with loglevel %s\n", av[0], LevelAry[LogLevel]);
	SynchronizeDir(CDir, 0, 1);
	SynchronizeDir(SCDir, 1, 1);
	ReadTimestamps(NULL);
	TestStartupJobs(); /* @startup jobs only run when crond is started, not when their crontab is loaded */

	{
		time_t t1 = time(NULL);
		time_t t2;
		long dt;
		short rescan = 60;
		short stime = 60;

		for (;;) {
			sleep((stime + 1) - (short)(time(NULL) % stime));

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
					SynchronizeDir(CDir, 0, 0);
					SynchronizeDir(SCDir, 1, 0);
					ReadTimestamps(NULL);
				}
			} 
			if (rescan < 60) {
				CheckUpdates(CDir, 0, t1, t2);
				CheckUpdates(SCDir, 1, t1, t2);
			}
			if (DebugOpt)
				printlogf(LOG_DEBUG, "Wakeup dt=%d\n", dt);
			if (dt < -60*60 || dt > 60*60) {
				t1 = t2;
				printlogf(LOG_NOTICE,"time disparity of %d minutes detected\n", dt / 60);
			} else if (dt > 0) {
				TestJobs(t1, t2);
				RunJobs();
				sleep(5);
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

