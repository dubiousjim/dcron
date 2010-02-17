
/*
 * JOB.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2010 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void RunJob(CronFile *file, CronLine *line);
Prototype void EndJob(CronFile *file, CronLine *line, int exit_status);

Prototype const char *SendMail;

void
RunJob(CronFile *file, CronLine *line)
{
	char mailFile[PATH_MAX];
	int mailFd;
	const char *value = Mailto;

	line->cl_Pid = 0;
	line->cl_MailFlag = FALSE;

	/*
	 * try to open mail output file - owner root so nobody can screw with it.
	 * [v]snprintf always \0-terminate; we don't care here if result was truncated
	 */
	/*@-formatconst@*/
	(void)snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, (int)getpid());
	/*@=formatconst@*/

	if ((mailFd = open(mailFile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL|O_APPEND, 0600)) >= 0) {
		/* success: write headers to mailFile */
		line->cl_MailFlag = TRUE;
		/* if we didn't specify a -m Mailto, use the local user */
		if (!value)
			value = file->cf_UserName;
		(void)dprintf(mailFd, "To: %s\nSubject: cron for user %s %s\n\n",
				value,
				file->cf_UserName,
				line->cl_Description
				);
		/* remember mailFile's size */
		line->cl_MailPos = lseek(mailFd, 0, 1);
	}
	/*
	 * else no mailFd, we complain later and don't check job output
	 * but we still run the job if we can
	 */


	/*
	 * Fork as the user in question and run program
	 */

	if ((line->cl_Pid = fork()) == 0) {
		/*
		 * CHILD, FORK OK, PRE-EXEC
		 *
		 * Change running state to the user in question
		 */

		(void)ChangeUser(file->cf_UserName, TempDir,
				" for %s",
				line->cl_Description
				);

		/* from this point we are unpriviledged */

		if (DebugOpt)
			logger(LOG_DEBUG, "running child for user %s %s\n",
					file->cf_UserName,
					line->cl_Description
					);

		/*
		 * Inside child, we copy our fd 2 (which may be /dev/null) into
		 * an open-until-exec fd 8
		 */
		(void)dup2(2, 8);
		(void)fcntl(8, F_SETFD, FD_CLOEXEC);
		(void)fclose(stderr);

		if (mailFd >= 0) {
			/* stdin is already /dev/null, setup stdout and stderr > mailFile */
			(void)dup2(mailFd, 1);
			(void)dup2(mailFd, 2);
			(void)close(mailFd);
		} else {
			/* complain about no mailFd to log (now associated with fd 8) */
			dlogger(LOG_WARNING, 8, "creating mailfile %s for user %s %s failed: output to /dev/null\n",
					mailFile,
					file->cf_UserName,
					line->cl_Description
				   );
			/* stderr > /dev/null */
			(void)dup2(1, 2);
		}

		/*
		 * Start a new process group, so that children still in the crond's process group
		 * are all mailjobs.
		 */
		(void)setpgid(0, 0);

		(void)execl("/bin/sh", "/bin/sh", "-c", line->cl_Shell, NULL);
		/*
		 * CHILD FAILED TO EXEC CRONJOB
		 *
		 * Complain to our log (now associated with fd 8)
		 */
		dlogger(LOG_ERR, 8, "exec /bin/sh -c '%s' for user %s failed\n",
				line->cl_Shell,
				file->cf_UserName
			   );
		/*
		 * Also complain to stdout, which will be either the mailFile or /dev/null
		 */
		(void)dprintf(1, "exec /bin/sh -c '%s' failed\n", line->cl_Shell);
		/* we don't want crond to do any further tracking of child */
		exit(EXIT_SUCCESS);

	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 *
		 * Complain to log (with regular fd 2)
		 */
		logger(LOG_ERR, "forking for user %s %s failed\n",
				file->cf_UserName,
				line->cl_Description
				);
		line->cl_Pid = 0;
		(void)remove(mailFile);

	} else {
		/*
		 * PARENT, FORK SUCCESS
		 *
		 * rename mail-file based on pid of child process
		 * [v]snprintf always \0-terminate; we don't care here if result was truncated
		 */
		char mailFile2[PATH_MAX];

		/*@-formatconst@*/
		(void)snprintf(mailFile2, sizeof(mailFile2), TempFileFmt,
				file->cf_UserName, line->cl_Pid);
		/*@=formatconst@*/
		(void)rename(mailFile, mailFile2);
	}

	/*
	 * Close the mail file descriptor.. we can't just leave it open in
	 * a structure, closing it later, because we might run out of descriptors
	 */

	if (mailFd >= 0)
		(void)close(mailFd);
}

/*
 * EndJob - called when main job terminates
 */

void
EndJob(CronFile *file, CronLine *line, int exit_status)
{
	int mailFd;
	char mailFile[PATH_MAX];
	struct stat sbuf;
	struct	CronNotifier *notif;

	if (line->cl_Pid <= 0) {
		/*
		 * No job. This should never happen.
		 */
		line->cl_Pid = 0;
		return;
	}


	/*
	 * check return status
	 */
	if (line->cl_Delay > 0) {
		assert(line->cl_Timestamp!=NULL);
		if (exit_status == EAGAIN) {
			/*
			 * returned EAGAIN, wait cl_Delay then retry
			 * we base off the time the job was scheduled/started waiting, not the time it finished
			 *
			 * line->cl_NotUntil = time(NULL) + line->cl_Delay;	// use this to base off time finished
			 * line->cl_NotUntil += line->cl_Delay; 	// already applied
			 */
		} else {
			/*
			 * process finished without returning EAGAIN (it may have returned some other error)
			 * mark as having run and update timestamp
			 */
			FILE *fi;
			char buf[SMALL_BUF];
			int succeeded = 0;
			/*
			 * we base off the time the job was scheduled/started waiting, not the time it finished
			 *
			 * line->cl_LastRan = time(NULL);	// use this to base off time finished
			 */
			line->cl_LastRan = line->cl_NotUntil - line->cl_Delay;
			if ((fi = fopen(line->cl_Timestamp, "w")) != NULL) {
				if (strftime(buf, sizeof(buf), CRONSTAMP_FMT, localtime(&line->cl_LastRan)))
					if (fputs(buf, fi) >= 0)
						succeeded = 1;
				(void)fclose(fi);
			}
			if (!succeeded)
				logger(LOG_WARNING, "failed writing timestamp to %s for user %s %s\n",
						line->cl_Timestamp,
						file->cf_UserName,
						line->cl_Description
						);
			line->cl_NotUntil = line->cl_LastRan;
			line->cl_NotUntil += (line->cl_Freq > 0) ? line->cl_Freq : line->cl_Delay;
		}
	}

	if (exit_status != EAGAIN) {
		/*
		 * notify any waiters
		 */
		notif = line->cl_Notifs;
		while (notif) {
			if (notif->cn_Waiter) {
				notif->cn_Waiter->cw_Flag = exit_status;
			}
			notif = notif->cn_Next;
		}

		if (exit_status) {
			/*
			 * log non-zero exit_status
			 */
			logger(LOG_NOTICE, "exit status %d from user %s %s\n",
					exit_status,
					file->cf_UserName,
					line->cl_Description
				);

			}
	}
	if (!exit_status || exit_status == EAGAIN)
		if (DebugOpt)
			logger(LOG_DEBUG, "exit status %d from user %s %s\n",
						exit_status,
						file->cf_UserName,
						line->cl_Description
					);


	if (!line->cl_MailFlag) {
		/* End of job and no mail file */
		line->cl_Pid = 0;
		return;
	}

	/*
	 * Calculate mailFile's name before clearing cl_Pid
	 * [v]snprintf always \0-terminate; we don't care here if result was truncated
	 */
	/*@-formatconst@*/
	(void)snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, line->cl_Pid);
	/*@=formatconst@*/
	line->cl_Pid = 0;

	line->cl_MailFlag = FALSE;

	/*
	 * Check mail file. If size has increased and
	 * the file is still valid, we sendmail it.
	 */

	mailFd = open(mailFile, O_RDONLY);
	(void)remove(mailFile);
	if (mailFd < 0) {
		return;
	}

	/* Was mailFile tampered with, or didn't grow? */

	if (fstat(mailFd, &sbuf) < 0 ||
			sbuf.st_uid != DaemonUid ||
			sbuf.st_nlink != 0 ||
			sbuf.st_size == line->cl_MailPos ||
			!S_ISREG(sbuf.st_mode)
	   ) {
		(void)close(mailFd);
		return;
	}

	if ((line->cl_Pid = fork()) == 0) {
		/*
		 * CHILD, FORK OK, PRE-EXEC
		 *
		 * Change user id - no way in hell security can be compromised
		 * by the mailing and we already verified the mail file.
		 */

		(void)ChangeUser(file->cf_UserName, TempDir,
				" to mail %s output",
				line->cl_Description
				);

		/* from this point we are unpriviledged */

		/*
		 * Inside child, we copy our fd 2 (which may be /dev/null) into
		 * an open-until-exec fd 8
		 */

		(void)dup2(2, 8);
		(void)fcntl(8, F_SETFD, FD_CLOEXEC);
		(void)fclose(stderr);

		/*
		 * Run sendmail with stdin < mailFile and stderr > /dev/null
		 */

		(void)dup2(mailFd, 0);
		(void)dup2(1, 2);
		(void)close(mailFd);

		if (!SendMail) {
			/*
			 * If using standard sendmail, note in our log (now associated with fd 8)
			 * that we're trying to mail output
			 */
			dlogger(LOG_INFO, 8, "mailing %s output for user %s\n",
					line->cl_Description,
					file->cf_UserName
				 );
			(void)execl(SENDMAIL, SENDMAIL, SENDMAIL_ARGS, NULL);

			/* exec failed: pass through and log the error */
			SendMail = SENDMAIL;

		} else {
			/*
			 * If using custom mailer script, just try to exec it
			 */
			(void)execl(SendMail, SendMail, NULL);
		}

		/*
		 * CHILD FAILED TO EXEC SENDMAIL
		 *
		 * Complain to our log (now associated with fd 8)
		 */

		dlogger(LOG_WARNING, 8, "exec %s for user %s %s failed: output to /dev/null\n",
				SendMail,
				file->cf_UserName,
				line->cl_Description
			   );
		/* we don't want crond to do any further tracking of child */
		exit(EXIT_SUCCESS);

	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 *
		 * Complain to our log (with regular fd 2)
		 */
		logger(LOG_WARNING, "forking for user %s %s failed: output to /dev/null\n",
				file->cf_UserName,
				line->cl_Description
			);
		line->cl_Pid = 0;
	} else {
		/*
		 * PARENT, FORK OK
		 *
		 * We clear cl_Pid even when mailjob successfully forked
		 * and catch the dead mailjobs with our SIGCHLD handler.
		 */
		line->cl_Pid = 0;
	}

	(void)close(mailFd);

}
