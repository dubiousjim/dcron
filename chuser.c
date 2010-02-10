
/*
 * CHUSER.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype int ChangeUser(const char *user, char *dochdir);

int
ChangeUser(const char *user, char *dochdir)
{
	struct passwd *pas;

	/*
	 * Obtain password entry and change privilages
	 */

	if ((pas = getpwnam(user)) == 0) {
		printlogf(LOG_ERR, "failed to get uid for %s\n", user);
		return(-1);
	}
	setenv("USER", pas->pw_name, 1);
	setenv("HOME", pas->pw_dir, 1);
	setenv("SHELL", "/bin/sh", 1);

	/*
	 * Change running state to the user in question
	 */

	if (initgroups(user, pas->pw_gid) < 0) {
		printlogf(LOG_ERR, "initgroups failed: %s %s\n", user, strerror(errno));
		return(-1);
	}
	if (setregid(pas->pw_gid, pas->pw_gid) < 0) {
		printlogf(LOG_ERR, "setregid failed: %s %d\n", user, pas->pw_gid);
		return(-1);
	}
	if (setreuid(pas->pw_uid, pas->pw_uid) < 0) {
		printlogf(LOG_ERR, "setreuid failed: %s %d\n", user, pas->pw_uid);
		return(-1);
	}
	if (dochdir) {
		/* try to change to $HOME */
		if (chdir(pas->pw_dir) < 0) {
			printlogf(LOG_ERR, "chdir failed: %s %s\n", user, pas->pw_dir);
			/* dochdir is a backup directory, usually /tmp */
			if (chdir(dochdir) < 0) {
				printlogf(LOG_ERR, "chdir failed: %s %s\n", user, dochdir);
				return(-1);
			}
		}
	}
	return(pas->pw_uid);
}

