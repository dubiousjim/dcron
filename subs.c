
/*
 * SUBS.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void logn(int level, const char *ctl, ...);
Prototype void log9(const char *ctl, ...);
Prototype void logfd(int fd, const char *ctl, ...);
Prototype void fdprintf(int fd, const char *ctl, ...);
Prototype int ChangeUser(const char *user, short dochdir);
Prototype void vlog(int level, int fd, const char *ctl, va_list va);
Prototype int slog(char *buf, const char *ctl, int nmax, va_list va, short useDate);

void 
log9(const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vlog(9, 2, ctl, va);
    va_end(va);
}

void 
logn(int level, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vlog(level, 2, ctl, va);
    va_end(va);
}

void 
logfd(int fd, const char *ctl, ...)
{
    va_list va;

    va_start(va, ctl);
    vlog(9, fd, ctl, va);
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
    short n;
    static short useDate = 1;

    if (level >= LogLevel) {
        write(fd, buf, n = slog(buf, ctl, sizeof(buf), va, useDate));
	useDate = (n && buf[n-1] == '\n');
    }
}

int
slog(char *buf, const char *ctl, int nmax, va_list va, short useDate)
{
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);

    buf[0] = 0;
    if (useDate)
	strftime(buf, 128, "%d-%b-%Y %H:%M  ", tp);
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
        logn(9, "failed to get uid for %s", user);
        return(-1);
    }
    setenv("USER", pas->pw_name, 1);
    setenv("HOME", pas->pw_dir, 1);
    setenv("SHELL", "/bin/sh", 1);

    /*
     * Change running state to the user in question
     */

    if (initgroups(user, pas->pw_gid) < 0) {
	logn(9, "initgroups failed: %s %s", user, strerror(errno));
	return(-1);
    }
    if (setregid(pas->pw_gid, pas->pw_gid) < 0) {
	logn(9, "setregid failed: %s %d", user, pas->pw_gid);
	return(-1);
    }
    if (setreuid(pas->pw_uid, pas->pw_uid) < 0) {
	logn(9, "setreuid failed: %s %d", user, pas->pw_uid);
	return(-1);
    }
    if (dochdir) {
	if (chdir(pas->pw_dir) < 0) {
	    logn(8, "chdir failed: %s %s", user, pas->pw_dir);
	    if (chdir(TMPDIR) < 0) {
		logn(9, "chdir failed: %s %s", user, pas->pw_dir);
		logn(9, "chdir failed: %s " TMPDIR, user);
		return(-1);
	    }
	}
    }
    return(pas->pw_uid);
}

#if 0

char *
strdup(const char *str)
{
    char *ptr = malloc(strlen(str) + 1);

    if (ptr)
        strcpy(ptr, str);
    return(ptr);
}

#endif
