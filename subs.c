
/*
 * SUBS.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void logf(int level, const char *ctl, ...);
Prototype void fdlogf(int level, int fd, const char *ctl, ...);
Prototype void fdprintf(int fd, const char *ctl, ...);
Prototype int  ChangeUser(const char *user, short dochdir);
Prototype void startlogger(void);
Prototype void initsignals(void);
Prototype char Hostname[SMALL_BUFFER];

void vlog(int level, int fd, const char *ctl, va_list va);

char Hostname[SMALL_BUFFER];


void
logf(int level, const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vlog(level, 2, ctl, va);
	va_end(va);
}

void
fdlogf(int level, int fd, const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vlog(level, fd, ctl, va);
	va_end(va);
}

void
fdprintf(int fd, const char *ctl, ...)
{
	va_list va;
	char buf[LOG_BUFFER];

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	write(fd, buf, strlen(buf));
	va_end(va);
}

void
vlog(int level, int fd, const char *ctl, va_list va)
{
	char buf[LOG_BUFFER];
	int  logfd;
	static short suppressHeader = 0;

	if (level <= LogLevel) {
		if (ForegroundOpt == 1) {
			/*
			 * when -d or -f, we always (and only) log to stderr
			 * fd will be 2 except when 2 is bound to a execing subprocess, then it will be 8
			 */
			vsnprintf(buf, sizeof(buf), ctl, va);
			write(fd, buf, strlen(buf));
		} else if (LoggerOpt == 0) {
			/* log to syslog */
			vsnprintf(buf, sizeof(buf), ctl, va);
			syslog(level, "%s", buf);

		} else if ((logfd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) >= 0) {
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

			write(logfd, buf, buflen);
			/* if previous write wasn't \n-terminated, we suppress header on next write */
			suppressHeader = (buf[buflen-1] != '\n');
			close(logfd);

		} else {
			int e = errno;
			fdprintf(fd, "failed to open logfile '%s' reason: %s\n",
					LogFile,
					strerror(e)
					);
			exit(e);
		}
	}
}

int
ChangeUser(const char *user, short dochdir)
{
	struct passwd *pas;

	/*
	 * Obtain password entry and change privilages
	 */

	if ((pas = getpwnam(user)) == 0) {
		logf(LOG_ERR, "failed to get uid for %s\n", user);
		return(-1);
	}
	setenv("USER", pas->pw_name, 1);
	setenv("HOME", pas->pw_dir, 1);
	setenv("SHELL", "/bin/sh", 1);

	/*
	 * Change running state to the user in question
	 */

	if (initgroups(user, pas->pw_gid) < 0) {
		logf(LOG_ERR, "initgroups failed: %s %s\n", user, strerror(errno));
		return(-1);
	}
	if (setregid(pas->pw_gid, pas->pw_gid) < 0) {
		logf(LOG_ERR, "setregid failed: %s %d\n", user, pas->pw_gid);
		return(-1);
	}
	if (setreuid(pas->pw_uid, pas->pw_uid) < 0) {
		logf(LOG_ERR, "setreuid failed: %s %d\n", user, pas->pw_uid);
		return(-1);
	}
	if (dochdir) {
		if (chdir(pas->pw_dir) < 0) {
			logf(LOG_ERR, "chdir failed: %s %s\n", user, pas->pw_dir);
			if (chdir(TempDir) < 0) {
				logf(LOG_ERR, "chdir failed: %s %s\n", user, TempDir);
				return(-1);
			}
		}
	}
	return(pas->pw_uid);
}


void
startlogger (void) {
	int logfd;

	if (LoggerOpt == 0)
		openlog(LOG_IDENT, LOG_CONS|LOG_PID, LOG_CRON);

	else { /* test logfile */
		if ((logfd = open(LogFile, O_WRONLY|O_CREAT|O_APPEND, 0600)) >= 0)
			close(logfd);
		else
			errx(errno, "failed to open logfile '%s' reason: %s",
					LogFile,
					strerror(errno)
				);
	}
}

void
initsignals (void) {
	signal(SIGHUP, SIG_IGN);	/* JP: hmm.. but, if kill -HUP original
							 * version - has died. ;(
							 */
}
