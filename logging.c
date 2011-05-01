
/*
 * LOGGING.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2011 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void logger(int level, const char *fmt, ...);
Prototype void dlogger(int level, int fd, const char *fmt, ...);
Prototype void initsignals(void);
Prototype char Hostname[HOST_NAME_MAX];

static void vlogger(int level, int fd, const char *fmt, va_list va);
static void reopenlogger(/*@unused@*/ int sig) __attribute__((signal));

char Hostname[HOST_NAME_MAX];


void
logger(int level, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vlogger(level, 2, fmt, va);
	va_end(va);
}

void
dlogger(int level, int fd, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vlogger(level, fd, fmt, va);
	va_end(va);
}

void
vlogger(int level, int fd, const char *fmt, va_list va)
{
	char buf[LINE_BUF];
	static bool suppressHeader = FALSE;

	if (level <= LogLevel) {
		if (ForegroundOpt) {
			/*
			 * when -d or -f, we always (and only) log to stderr
			 * fd will be 2 except when 2 is bound to a execing subprocess, then it will be 8
			 * [v]snprintf always \0-terminate; we don't care here if result was truncated
			 */
			(void)vsnprintf(buf, sizeof(buf), fmt, va);
			(void)write(fd, buf, strlen(buf));
		} else if (SyslogOpt) {
			/* log to syslog */
			(void)vsnprintf(buf, sizeof(buf), fmt, va);
			syslog(level, "%s", buf);

		} else {
			/* log to file */

			time_t t = time(NULL);
			struct tm *tp = localtime(&t);
			size_t buflen, hdrlen = 0;
			buf[0] = '\0'; /* in case suppressHeader or strftime fails */
			if (!suppressHeader) {
				/*
				 * run LogHeader through strftime --> [yields hdr] plug in Hostname --> [yields buf]
				 */
				char hdr[SMALL_BUF];
				/* strftime returns strlen of result, provided that result plus a \0 fit into buf of size */
				if (strftime(hdr, sizeof(hdr), LogHeader, tp)) {
					if (gethostname(Hostname, sizeof(Hostname))==0)
						/* gethostname successful */
						/* result will be \0-terminated except gethostname doesn't promise to do so if it has to truncate */
						Hostname[sizeof(Hostname)-1] = '\0';
					else
						Hostname[0] = '\0';   /* gethostname() call failed */
					/* return value >= size means result was truncated */
					if ((hdrlen = stringf(buf, sizeof(hdr), hdr, Hostname)) >= sizeof(hdr))
						hdrlen = sizeof(hdr) - 1;
				}
			}
			if ((buflen = vstringf(buf + hdrlen, sizeof(buf) - hdrlen, fmt, va) + hdrlen) >= sizeof(buf))
				buflen = sizeof(buf) - 1;

			(void)write(fd, buf, buflen);
			/* if previous write wasn't \n-terminated, we suppress header on next write */
			suppressHeader = buflen>0 && (buf[buflen-1] != '\n');

		}
	}
}

static void reopenlogger(/*@unused@*/ int sig)
{
	int fd;
	int saverr = errno;
	UNUSED(sig);

	if (getpid() == DaemonPid) {
		/* only daemon handles, children should ignore */
		assert(LogFile!=NULL);
		if ((fd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) < 0) {
			/* can't reopen log file, exit */
			char errmsg[] = "reopening logfile failed\n";
			/* unclear whether the va_start/end and vsnprintf calls of dprintf are safe to call during signal handler */
			(void)write(2, errmsg, sizeof(errmsg)-1);
			exit(EXIT_FAILURE);
		}
		(void)dup2(fd, 2);
		(void)close(fd);
	}
	errno =  saverr;
}

static void waitmailjob(/*@unused@*/ int sig)
{
	/*
	 * Wait for any children in our process group.
	 * These will all be mailjobs.
	 */
	pid_t child;
	int saverr = errno;
	UNUSED(sig);

	do {
		child = waitpid(-DaemonPid, NULL, WNOHANG);
		/* call was interrupted, try again: won't happen because we use SA_RESTART */
		/* if (child == (pid_t)-1 && errno == EINTR) continue; */
	} while (child > (pid_t) 0);
	/* if no pending children, child,errno == -1,ECHILD */
	/* if all children still running, child == 0 */
	errno =  saverr;
}

void
initsignals (void)
{
	struct sigaction sa;

	/* save daemon's pid globally */
	DaemonPid = getpid();

	/* restart any system calls that were interrupted by signal */
	sa.sa_flags = SA_RESTART;
	if (!ForegroundOpt && !SyslogOpt) {
		assert(LogFile != NULL);
		sa.sa_handler = reopenlogger;
	} else
		sa.sa_handler = SIG_IGN;
	if ( /*@-compdef@*/
		sigaction (SIGHUP, &sa, NULL) != 0
		/*@=compdef@*/) {
		(void)dprintf(2, "failed starting SIGHUP handling: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = waitmailjob;
	if ( /*@-compdef@*/
		sigaction (SIGCHLD, &sa, NULL) != 0
		/*@=compdef@*/) {
		(void)dprintf(2, "failed starting SIGCHLD handling: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

}

