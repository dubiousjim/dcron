
/*
 * SUBS.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void logn(int level, const char *ctl, ...);
Prototype void logfd(int level, int fd, const char *ctl, ...);
Prototype void fdprintf(int fd, const char *ctl, ...);
Prototype int  ChangeUser(const char *user, short dochdir);
Prototype void vlog(int level, int fd, const char *ctl, va_list va);
Prototype void startlogger(void);
Prototype void initsignals(void);
Prototype char Hostname[64];

int slog(char *buf, const char *ctl, int nmax, va_list va, short useDate);
char Hostname[64];


void
logn(int level, const char *ctl, ...)
{
	va_list va;

	va_start(va, ctl);
	vlog(level, 2, ctl, va);
	va_end(va);
}

void
logfd(int level, int fd, const char *ctl, ...)
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
	char buf[2048];

	va_start(va, ctl);
	vsnprintf(buf, sizeof(buf), ctl, va);
	write(fd, buf, strlen(buf));
	va_end(va);
}

void
vlog(int level, int fd, const char *ctl, va_list va)
{
	char buf[2048];
	int  logfd;
    short n;
    static short useDate = 1;

	if (level <= LogLevel) {
		vsnprintf(buf,sizeof(buf), ctl, va);
		if (ForegroundOpt == 1)
			/* when -d or -f, we always (and only) log to stderr
			 * fd will be 2 except when 2 is bound to a execing subprocess, then it will be 8
			 */
			write(fd, buf, strlen(buf));
		else
			if (LoggerOpt == 0) syslog(level, "%s", buf);
			else {
				if ((logfd = open(LogFile,O_WRONLY|O_CREAT|O_APPEND,0600)) >= 0) {
					write(logfd, buf, n = slog(buf, ctl, sizeof(buf), va, useDate));
					useDate = (n && buf[n-1] == '\n');
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
}

int
slog(char *buf, const char *ctl, int nmax, va_list va, short useDate)
{
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);
    buf[0] = 0;
    if (useDate) {
		char hdr[128];
		hdr[0] = 0;
		strftime(hdr, 128, LOG_DATE_FMT, tp);
		if (!gethostname(Hostname, 63))
			Hostname[63] = 0;  // if hostname is larger than buffer, gethostname() doesn't promise to null-terminate it
		else
			Hostname[0] = 0;   // gethostname() call failed
		snprintf(buf, 194, hdr, Hostname);
	}
    vsnprintf(buf + strlen(buf), nmax, ctl, va);
    return(strlen(buf));
}

int
ChangeUser(const char *user, short dochdir)
{
	struct passwd *pas;

	/*
	 * Obtain password entry and change privilages
	 */

	if ((pas = getpwnam(user)) == 0) {
		logn(LOG_ERR, "failed to get uid for %s\n", user);
		return(-1);
	}
	setenv("USER", pas->pw_name, 1);
	setenv("HOME", pas->pw_dir, 1);
	setenv("SHELL", "/bin/sh", 1);

	/*
	 * Change running state to the user in question
	 */

	if (initgroups(user, pas->pw_gid) < 0) {
		logn(LOG_ERR, "initgroups failed: %s %s\n", user, strerror(errno));
		return(-1);
	}
	if (setregid(pas->pw_gid, pas->pw_gid) < 0) {
		logn(LOG_ERR, "setregid failed: %s %d\n", user, pas->pw_gid);
		return(-1);
	}
	if (setreuid(pas->pw_uid, pas->pw_uid) < 0) {
		logn(LOG_ERR, "setreuid failed: %s %d\n", user, pas->pw_uid);
		return(-1);
	}
	if (dochdir) {
		if (chdir(pas->pw_dir) < 0) {
			logn(LOG_ERR, "chdir failed: %s %s\n", user, pas->pw_dir);
			if (chdir(TempDir) < 0) {
				logn(LOG_ERR, "chdir failed: %s %s\n", user, TempDir);
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
		openlog(LOG_IDENT, LOG_CONS|LOG_PID,LOG_CRON);

	else { /* test logfile */
		if ((logfd = open(LogFile,O_WRONLY|O_CREAT|O_APPEND,0600)) >= 0)
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
	signal(SIGHUP,SIG_IGN);	/* JP: hmm.. but, if kill -HUP original
							 * version - has died. ;(
							 */
}
