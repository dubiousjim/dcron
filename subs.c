
/*
 * SUBS.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2019 James Pryor <dubiousjim@gmail.com>
 * May be distributed under the GNU General Public License version 2 or any later version.
 */

#include "defs.h"

Prototype void printlogf(int level, const char *ctl, ...);
Prototype void fdprintlogf(int level, int fd, const char *ctl, ...);
Prototype int fdprintf(int fd, const char *ctl, ...);
Prototype void initsignals(void);
Prototype char Hostname[SMALL_BUFFER];

void vlog(int level, int fd, const char *ctl, va_list va);

char Hostname[SMALL_BUFFER];


void
printlogf(int level, const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vlog(level, 2, ctl, va);
	va_end(va);
}

void
fdprintlogf(int level, int fd, const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vlog(level, fd, ctl, va);
	va_end(va);
}

int
fdprintf(int fd, const char *ctl, ...)
{
	va_list va;
	char buf[LOG_BUFFER];
	int n;

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	n = write(fd, buf, strlen(buf));
	va_end(va);

	return n;
}

void
vlog(int level, int fd, const char *ctl, va_list va)
{
	char buf[LOG_BUFFER];
	static short suppressHeader = 0;

	if (level <= LogLevel) {
		if (ForegroundOpt) {
			/*
			 * when -d or -f, we always (and only) log to stderr
			 * fd will be 2 except when 2 is bound to a execing subprocess, then it will be 8
			 * [v]snprintf write at most size including \0; they'll null-terminate, even when they truncate
			 * we don't care here whether it truncates
			 */
			vsnprintf(buf, sizeof(buf), ctl, va);
			write(fd, buf, strlen(buf));
		} else if (SyslogOpt) {
			/* log to syslog */
			vsnprintf(buf, sizeof(buf), ctl, va);
			syslog(level, "%s", buf);

		} else {
			/* log to file */

			time_t t = time(NULL);
			struct tm *tp = localtime(&t);
			int buflen, hdrlen = 0;
			buf[0] = 0; /* in case suppressHeader or strftime fails */
			if (!suppressHeader) {
				/*
				 * run LogHeader through strftime --> [yields hdr] plug in Hostname --> [yields buf]
				 */
				char hdr[SMALL_BUFFER];
				/* strftime returns strlen of result, provided that result plus a \0 fit into buf of size */
				if (strftime(hdr, sizeof(hdr), LogHeader, tp)) {
					if (gethostname(Hostname, sizeof(Hostname))==0)
						/* gethostname successful */
						/* result will be \0-terminated except gethostname doesn't promise to do so if it has to truncate */
						Hostname[sizeof(Hostname)-1] = 0;
					else
						Hostname[0] = 0;   /* gethostname() call failed */
					/* [v]snprintf write at most size including \0; they'll null-terminate, even when they truncate */
					/* return value >= size means result was truncated */
					if ((hdrlen = snprintf(buf, sizeof(hdr), hdr, Hostname)) >= sizeof(hdr))
						hdrlen = sizeof(hdr) - 1;
				}
			}
			if ((buflen = vsnprintf(buf + hdrlen, sizeof(buf) - hdrlen, ctl, va) + hdrlen) >= sizeof(buf))
				buflen = sizeof(buf) - 1;

			write(fd, buf, buflen);
			/* if previous write wasn't \n-terminated, we suppress header on next write */
			suppressHeader = (buf[buflen-1] != '\n');

		}
	}
}

void reopenlogger(int sig) {
	int fd;
	if (getpid() == DaemonPid) {
		/* only daemon handles, children should ignore */
		if ((fd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) < 0) {
			/* can't reopen log file, exit */
			exit(errno);
		}
		dup2(fd, 2);
		close(fd);
	}
}

void waitmailjob(int sig) {
	/*
	 * Wait for any children in our process group.
	 * These will all be mailjobs.
	 */
	pid_t child;
	do {
		child = waitpid(-DaemonPid, NULL, WNOHANG);
		/* call was interrupted, try again: won't happen because we use SA_RESTART */
		/* if (child == (pid_t)-1 && errno == EINTR) continue; */
	} while (child > (pid_t) 0);
	/* if no pending children, child,errno == -1,ECHILD */
	/* if all children still running, child == 0 */
}

void
initsignals (void) {
	struct sigaction sa;
	int n;

	/* save daemon's pid globally */
	DaemonPid = getpid();

	/* restart any system calls that were interrupted by signal */
	sa.sa_flags = SA_RESTART;
	if (!ForegroundOpt && !SyslogOpt)
		sa.sa_handler = reopenlogger;
	else
		sa.sa_handler = SIG_IGN;
	if (sigaction (SIGHUP, &sa, NULL) != 0) {
		n = errno;
		fdprintf(2, "failed to start SIGHUP handling, reason: %s", strerror(errno));
		exit(n);
	}
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = waitmailjob;
	if (sigaction (SIGCHLD, &sa, NULL) != 0) {
		n = errno;
		fdprintf(2, "failed to start SIGCHLD handling, reason: %s", strerror(errno));
		exit(n);
	}

}

