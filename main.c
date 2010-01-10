
/*
 * MAIN.C
 *
 * crond [-l#] [-L logfile | -S ] [-M mailer] [-m mailto] [-d|-f|-b] [-c crondir] [-s systemdir] [-t timestamps]
 *
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype short DebugOpt;
Prototype short LogLevel;
Prototype short ForegroundOpt;
Prototype short LoggerOpt;
Prototype const char *CDir;
Prototype const char *SCDir;
Prototype const char *TSDir;
Prototype const char *LogFile;
Prototype const char *LogHeader;
Prototype uid_t DaemonUid;
Prototype int InSyncFileRoot;
Prototype const char *SendMail;
Prototype const char *Mailto;
Prototype char *TempDir;
Prototype char *TempFileFmt;

short DebugOpt;
short LogLevel = LOG_LEVEL;
short ForegroundOpt = 0;
short LoggerOpt;
const char  *CDir = CRONTABS;
const char  *SCDir = SCRONTABS;
const char *TSDir = TIMESTAMPS;
const char *LogFile = LOG_FILE; /* opened with mode 0600 */
const char *LogHeader = LOGHEADER;
const char *SendMail = NULL;
const char *Mailto = NULL;
char *TempDir;
char *TempFileFmt;

uid_t DaemonUid;
int InSyncFileRoot;

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

	while ((i = getopt(ac,av,"dl:L:fbSc:s:m:M:t:")) != EOF) {
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
				LoggerOpt = 0;
				break;
			case 'L':			/* use internal log formatter */
				LoggerOpt = 1;
				if (*optarg != 0) {
					LogFile = optarg;
				}
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
				printf("-t            directory of timestamps (defaults to %s)\n", TIMESTAMPS);
				printf("-m user@host  where should cron output be directed? (defaults to local user)\n");
				printf("-M mailer     (defaults to %s)\n", SENDMAIL);
				printf("-S            log to syslog using identity '%s' (default)\n", LOG_IDENT);
				printf("-L [file]     log to file instead of syslog (if no file specified, uses %s)\n", LOG_FILE);
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

	for (i = 3; i < OPEN_MAX; ++i) {
        close(i);
    }

	i = open("/dev/null", O_RDWR);
	if (i < 0) {
		perror("open: /dev/null");
		exit(1);
	}
	dup2(i, 0);
	dup2(i, 1);

	if (ForegroundOpt == 0) {

		fclose(stderr);
		dup2(i, 2);

		int fd;
		int pid;
		if (setsid() < 0)
			perror("setsid");

		if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
			ioctl(fd, TIOCNOTTY, 0);
			close(fd);
		}

		pid = fork();

		if (pid < 0) {
			perror("fork");
			exit(1);
		}
		if (pid > 0)
			exit(0);
	}

	(void)startlogger();		/* need if syslog mode selected */
	(void)initsignals();		/* set some signal handlers */

	/* create tempdir with permissions 755 for cron output */
	TempDir = strdup(TMPDIR "/cron.XXXXXX"); 
	if (mkdtemp(TempDir) == NULL) {
		perror("mkdtemp");
		exit(1);
	}
	if (chmod(TempDir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)) {
		perror("chmod");
		exit(1);
	}
	asprintf(&TempFileFmt, "%s/cron.%%s.%%d", TempDir);

	/*
	 * main loop - synchronize to 1 second after the minute, minimum sleep
	 *             of 1 second.
	 */

	logn(LOG_NOTICE,"%s " VERSION " dillon's cron daemon, started with loglevel %s\n", av[0], LevelAry[LogLevel]);
	SynchronizeDir(CDir, NULL, 1);
	SynchronizeDir(SCDir, "root", 1);
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
				rescan = 60;
				SynchronizeDir(CDir, NULL, 0);
				SynchronizeDir(SCDir, "root", 0);
				ReadTimestamps(NULL);
			} else {
				CheckUpdates(CDir, NULL, t1, t2);
				CheckUpdates(SCDir, "root", t1, t2);
			}
			if (DebugOpt)
				logn(LOG_DEBUG, "Wakeup dt=%d\n", dt);
			if (dt < -60*60 || dt > 60*60) {
				t1 = t2;
				logn(LOG_NOTICE,"time disparity of %d minutes detected\n", dt / 60);
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

