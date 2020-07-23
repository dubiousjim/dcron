
/*
 * JOB.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2019 James Pryor <dubiousjim@gmail.com>
 * May be distributed under the GNU General Public License version 2 or any later version.
 */

#include "defs.h"

Prototype void RunJob(CronFile *file, CronLine *line);
Prototype void EndJob(CronFile *file, CronLine *line, int exit_status);

Prototype const char *SendMail;

void
RunJob(CronFile *file, CronLine *line)
{
	char mailFile[SMALL_BUFFER];
	int mailFd;
	const char *value = Mailto;

	line->cl_Pid = 0;
	line->cl_MailFlag = 0;

	/*
	 * try to open mail output file - owner root so nobody can screw with it.
	 */

	snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, (int)getpid());

	if ((mailFd = open(mailFile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL|O_APPEND, 0600)) >= 0) {
		/* success: write headers to mailFile */
		line->cl_MailFlag = 1;
		/* if we didn't specify a -m Mailto, use the local user */
		if (!value)
			value = file->cf_UserName;
		fdprintf(mailFd, "To: %s\nSubject: cron for user %s %s\n\n",
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

		if (ChangeUser(line->cl_UserName, TempDir) < 0) {
			printlogf(LOG_ERR, "unable to ChangeUser (user %s %s)\n",
					line->cl_UserName,
					line->cl_Description
					);
			exit(0);
		}

		/* from this point we are unpriviledged */

		if (DebugOpt)
			printlogf(LOG_DEBUG, "child running: %s\n", line->cl_Description);

		/*
		 * Inside child, we copy our fd 2 (which may be /dev/null) into
		 * an open-until-exec fd 8
		 */
		dup2(2, 8);
		fcntl(8, F_SETFD, FD_CLOEXEC);
		fclose(stderr);

		if (mailFd >= 0) {
			/* stdin is already /dev/null, setup stdout and stderr > mailFile */
			dup2(mailFd, 1);
			dup2(mailFd, 2);
			close(mailFd);
		} else {
			/* complain about no mailFd to log (now associated with fd 8) */
			fdprintlogf(LOG_WARNING, 8, "unable to create mail file %s: cron output for user %s %s to /dev/null\n",
					mailFile,
					file->cf_UserName,
					line->cl_Description
				   );
			/* stderr > /dev/null */
			dup2(1, 2);
		}

		/*
		 * Start a new process group, so that children still in the crond's process group
		 * are all mailjobs.
		 */
		setpgid(0, 0);

		execl("/bin/sh", "/bin/sh", "-c", line->cl_Shell, NULL);
		/*
		 * CHILD FAILED TO EXEC CRONJOB
		 *
		 * Complain to our log (now associated with fd 8)
		 */
		fdprintlogf(LOG_ERR, 8, "unable to exec (user %s cmd /bin/sh -c %s)\n",
				line->cl_UserName,
				line->cl_Shell
			   );
		/*
		 * Also complain to stdout, which will be either the mailFile or /dev/null
		 */
		fdprintf(1, "unable to exec: /bin/sh -c %s\n", line->cl_Shell);
		exit(0);

	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 *
		 * Complain to log (with regular fd 2)
		 */
		printlogf(LOG_ERR, "unable to fork (user %s %s)\n",
				line->cl_UserName,
				line->cl_Description
				);
		line->cl_Pid = 0;
		remove(mailFile);

	} else {
		/*
		 * PARENT, FORK SUCCESS
		 *
		 * rename mail-file based on pid of child process
		 */
		char mailFile2[SMALL_BUFFER];

		snprintf(mailFile2, sizeof(mailFile2), TempFileFmt,
				file->cf_UserName, line->cl_Pid);
		rename(mailFile, mailFile2);
	}

	/*
	 * Close the mail file descriptor.. we can't just leave it open in
	 * a structure, closing it later, because we might run out of descriptors
	 */

	if (mailFd >= 0)
		close(mailFd);
}

/*
 * EndJob - called when main job terminates
 */

void
EndJob(CronFile *file, CronLine *line, int exit_status)
{
	int mailFd;
	char mailFile[SMALL_BUFFER];
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
			char buf[SMALL_BUFFER];
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
				fclose(fi);
			}
			if (!succeeded)
				printlogf(LOG_WARNING, "unable to write timestamp to %s (user %s %s)\n", line->cl_Timestamp, file->cf_UserName, line->cl_Description);
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
			printlogf(LOG_NOTICE, "exit status %d from user %s %s\n",
					exit_status,
					file->cf_UserName,
					line->cl_Description
				);

			}
	}
	if (!exit_status || exit_status == EAGAIN)
		if (DebugOpt)
			printlogf(LOG_DEBUG, "exit status %d from user %s %s\n",
						exit_status,
						file->cf_UserName,
						line->cl_Description
					);


	if (line->cl_MailFlag != 1) {
		/* End of job and no mail file */
		line->cl_Pid = 0;
		return;
	}

	/*
	 * Calculate mailFile's name before clearing cl_Pid
	 */
	snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, line->cl_Pid);
	line->cl_Pid = 0;

	line->cl_MailFlag = 0;

	/*
	 * Check mail file. If size has increased and
	 * the file is still valid, we sendmail it.
	 */

	mailFd = open(mailFile, O_RDONLY);
	remove(mailFile);
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
		close(mailFd);
		return;
	}

	if ((line->cl_Pid = fork()) == 0) {
		/*
		 * CHILD, FORK OK, PRE-EXEC
		 *
		 * Change user id - no way in hell security can be compromised
		 * by the mailing and we already verified the mail file.
		 */

		if (ChangeUser(file->cf_UserName, TempDir) < 0) {
			printlogf(LOG_ERR, "unable to ChangeUser to send mail (user %s %s)\n",
					file->cf_UserName,
					line->cl_Description
					);
			exit(0);
		}

		/* from this point we are unpriviledged */

		/*
		 * Inside child, we copy our fd 2 (which may be /dev/null) into
		 * an open-until-exec fd 8
		 */

		dup2(2, 8);
		fcntl(8, F_SETFD, FD_CLOEXEC);
		fclose(stderr);

		/*
		 * Run sendmail with stdin < mailFile and stderr > /dev/null
		 */

		dup2(mailFd, 0);
		dup2(1, 2);
		close(mailFd);

		if (!SendMail) {
			/*
			 * If using standard sendmail, note in our log (now associated with fd 8)
			 * that we're trying to mail output
			 */
			fdprintlogf(LOG_INFO, 8, "mailing cron output for user %s %s\n",
					file->cf_UserName,
					line->cl_Description
				 );
			execl(SENDMAIL, SENDMAIL, SENDMAIL_ARGS, NULL);

			/* exec failed: pass through and log the error */
			SendMail = SENDMAIL;

		} else {
			/*
			 * If using custom mailer script, just try to exec it
			 */
			execl(SendMail, SendMail, NULL);
		}

		/*
		 * CHILD FAILED TO EXEC SENDMAIL
		 *
		 * Complain to our log (now associated with fd 8)
		 */

		fdprintlogf(LOG_WARNING, 8, "unable to exec %s: cron output for user %s %s to /dev/null\n",
				SendMail,
				file->cf_UserName,
				line->cl_Description
			   );
		exit(0);

	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 *
		 * Complain to our log (with regular fd 2)
		 */
		printlogf(LOG_WARNING, "unable to fork: cron output for user %s %s to /dev/null\n",
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

	close(mailFd);

}
