
/*
 * CHUSER.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2011 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype /*@maynotreturn@*/ uid_t ChangeUser(const char *user, STRING dochdir, const char *when, const char *desc);

static /*@noreturn@*/ void ChangeUserFailed(const char *reason, STRING post_reason, const char *user, const char *when, const char *desc);



void
ChangeUserFailed(const char *reason, STRING post_reason, const char *user, const char *when, const char *desc)
{
	char buf[LINE_BUF];
	/* [v]snprintf always \0-terminate; we don't care here if result was truncated */
	(void)snprintf(buf, sizeof(buf), "%s user %%s%s\n", reason, when);
	if (post_reason)
		logger(LOG_ERR, buf, post_reason, user, desc);
	else
		logger(LOG_ERR, buf, user, desc);
	exit(EXIT_FAILURE);
}


uid_t
ChangeUser(const char *user, STRING dochdir, const char *when, const char *desc)
{
	struct passwd *pas;

	/*
	 * Obtain password entry and change privilages
	 */
	if ((pas = getpwnam(user)) == NULL)
		ChangeUserFailed("could not change to unknown", NULL, user, when, desc);

	if (setenv("USER", pas->pw_name, (int)TRUE))
		ChangeUserFailed("could not set USER for", NULL, user, when, desc);
	if (setenv("HOME", pas->pw_dir, (int)TRUE))
		ChangeUserFailed("could not set HOME for", NULL, user, when, desc);
	if (setenv("SHELL", "/bin/sh", (int)TRUE))
		ChangeUserFailed("could not set SHELL for", NULL, user, when, desc);

	/*
	 * Change running state to the user in question
	 */

	if (initgroups(user, pas->pw_gid) < 0) {
		char buf[SMALL_BUF];
		(void)snprintf(buf, sizeof(buf), " gid %d%s", pas->pw_gid, when);
		ChangeUserFailed("could not initgroups for", NULL, user, buf, desc);
	}
	if (setregid(pas->pw_gid, pas->pw_gid) < 0) {
		char buf[SMALL_BUF];
		(void)snprintf(buf, sizeof(buf), " gid %d%s", pas->pw_gid, when);
		ChangeUserFailed("could not setregid for", NULL, user, buf, desc);
	}
	if (setreuid(pas->pw_uid, pas->pw_uid) < 0) {
		char buf[SMALL_BUF];
		(void)snprintf(buf, sizeof(buf), " uid %d%s", pas->pw_uid, when);
		ChangeUserFailed("could not setreuid for", NULL, user, buf, desc);
	}
	if (dochdir) {
		/* first try to cd $HOME */
		if (chdir(pas->pw_dir) < 0) {
			/* if that fails, complain then cd to the backup dochdir, usually /tmp */
			char buf[LINE_BUF];
			(void)snprintf(buf, sizeof(buf), "could not chdir to %s for user %%s%s\n", pas->pw_dir, when);
			logger(LOG_ERR, buf, user, desc);
			if (chdir(dochdir) < 0)
				ChangeUserFailed("could not chdir to %s for", dochdir, user, when, desc);
		}
	}
	return pas->pw_uid;
}

