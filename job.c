
/*
 * JOB.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void RunJob(CronFile *file, CronLine *line);
Prototype void EndJob(CronFile *file, CronLine *line, int exit_status);
Prototype const char *SendMail;

void
RunJob(CronFile *file, CronLine *line)
{
	char mailFile[128];
	int mailFd;
	const char *value = Mailto;

	line->cl_Pid = 0;
	line->cl_MailFlag = 0;

	/*
	 * open mail file - owner root so nobody can screw with it.
	 */

	snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, (int)getpid());
	mailFd = open(mailFile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL|O_APPEND, 0600);

	if (mailFd >= 0) {
		line->cl_MailFlag = 1;
		if (!value)
			value = file->cf_UserName;
		fdprintf(mailFd, "To: %s\nSubject: cron for user %s %s\n\n",
				value,
				file->cf_UserName,
				line->cl_Description
				);
		line->cl_MailPos = lseek(mailFd, 0, 1);
	}
	/* else we issue warning later */

	/*
	 * Fork as the user in question and run program
	 */

	if ((line->cl_Pid = fork()) == 0) {
		/*
		 * CHILD, FORK OK
		 */

		/*
		 * Change running state to the user in question
		 */

		if (ChangeUser(file->cf_UserName, 1) < 0) {
			logn(LOG_ERR, "ChangeUser failed (user %s %s)\n",
					file->cf_UserName,
					line->cl_Description
					);
			exit(0);
		}

		if (DebugOpt)
			logn(LOG_DEBUG, "child running: %s\n", line->cl_Description);

	/* Setup close-on-exec descriptor in case exec fails */
	dup2(2, 8);
	fcntl(8, F_SETFD, 1);
	fclose(stderr);

	/* stdin is already /dev/null, setup stdout and stderr */

		if (mailFd >= 0) {
			dup2(mailFd, 1);
			dup2(mailFd, 2);
			close(mailFd);
		} else {
			logfd(LOG_WARNING, 8, "unable to create mail file %s: cron output for user %s %s to /dev/null\n",
					mailFile,
					file->cf_UserName,
					line->cl_Description
				   );
		}
		execl("/bin/sh", "/bin/sh", "-c", line->cl_Shell, NULL, NULL);
		logfd(LOG_ERR, 8, "unable to exec (user %s cmd /bin/sh -c %s)\n",
				file->cf_UserName,
				line->cl_Shell
			   );
		/* we also write error to the mailed cron output */
		fdprintf(1, "unable to exec: /bin/sh -c %s\n", line->cl_Shell);
		exit(0);
	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 */
		logn(LOG_ERR, "unable to fork (user %s %s)\n",
				file->cf_UserName,
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
		char mailFile2[128];

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
 * EndJob - called when job terminates and when mail terminates
 */

void
EndJob(CronFile *file, CronLine *line, int exit_status)
{
	int mailFd;
	char mailFile[128];
	struct stat sbuf;
	struct	CronNotifier *notif;

	if (line->cl_Pid <= 0) {
		/*
		 * No job
		 */
		line->cl_Pid = 0;
		return;
	}


	/*
	 * check return status?
	 */
	if (line->cl_Delay > 0) {
		if (exit_status == EAGAIN) {
			/* returned EAGAIN, wait cl_Delay then retry */
			/*
			line->cl_NotUntil = time(NULL) + line->cl_Delay;
			 */
			line->cl_NotUntil += line->cl_Delay; // we base off the time the job was scheduled/started waiting, not the time it finished
		} else {
			/* process finished without returning EAGAIN (it may have returned some other error)
			 * mark as having run and update timestamp
			 */
			FILE *fi;
			char buf[64];
			/*
			line->cl_LastRan = time(NULL);
			 */
			line->cl_LastRan = line->cl_NotUntil; // we base off the time the job was scheduled/started waiting, not the time it finished
			if ((fi = fopen(line->cl_Timestamp, "w")) != NULL) {
				if (strftime(buf, sizeof(buf), TIMESTAMP_FMT, localtime(&line->cl_LastRan))) {
					fputs(buf, fi);
				} else
					logn(LOG_NOTICE, "unable to format timestamp (user %s %s)\n", file->cf_UserName, line->cl_Description);
				fclose(fi);
			} else {
				logn(LOG_NOTICE, "unable to write timestamp to %s (user %s %s)\n", line->cl_Timestamp, file->cf_UserName, line->cl_Description);
			}
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
			logn(LOG_INFO, "exit status %d from %s %s\n",
					exit_status,
					file->cf_UserName,
					line->cl_Description
				);

			}
	}

	snprintf(mailFile, sizeof(mailFile), TempFileFmt,
			file->cf_UserName, line->cl_Pid);

	line->cl_Pid = 0;

	if (line->cl_MailFlag != 1)
	/*
	 * End of job and no mail file
	 */
		return;

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
		 * CHILD, FORK OK
		 */

		/*
		 * change user id - no way in hell security can be compromised
		 * by the mailing and we already verified the mail file.
		 */

		if (ChangeUser(file->cf_UserName, 1) < 0) {
			logn(LOG_ERR, "ChangeUser %s failed; unable to send mail\n",
					file->cf_UserName
					);
			exit(0);
		}

		/* create close-on-exec log descriptor in case exec fails */
		dup2(2, 8);
		fcntl(8, F_SETFD, 1);
		fclose(stderr);

		/*
		 * run sendmail with mail file as standard input, only if
		 * mail file exists!
		 */

		dup2(mailFd, 0);
		dup2(1, 2);
		close(mailFd);

		if (!SendMail) {
			logfd(LOG_INFO, 8, "mailing cron output for user %s %s\n",
					file->cf_UserName,
					line->cl_Description
				 );
			execl(SENDMAIL, SENDMAIL, SENDMAIL_ARGS, NULL, NULL);
		} else
			execl(SendMail, SendMail, NULL, NULL);

			logfd(LOG_WARNING, 8, "unable to exec %s %s: cron output for user %s %s to /dev/null\n",
					SendMail,
					SENDMAIL_ARGS,
					file->cf_UserName,
					line->cl_Description
				   );
		exit(0);
	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 */
		logn(LOG_ERR, "unable to fork: cron output for user %s %s to /dev/null\n",
				file->cf_UserName,
				line->cl_Description
			);
	} else {
		/*
		 * PARENT, FORK OK
		 */
	}
	line->cl_Pid = 0;
	close(mailFd);
}

