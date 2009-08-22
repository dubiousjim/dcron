
/*
 * JOB.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void RunJob(CronFile *file, CronLine *line);
Prototype void EndJob(CronFile *file, CronLine *line);

void
RunJob(CronFile *file, CronLine *line)
{
	char mailFile[128];
	int mailFd;

	line->cl_Pid = 0;
	line->cl_MailFlag = 0;

	/*
	 * open mail file - owner root so nobody can screw with it.
	 */

	snprintf(mailFile, sizeof(mailFile), TMPDIR "/cron.%s.%d", 
			file->cf_UserName, (int)getpid());
	mailFd = open(mailFile, O_CREAT|O_TRUNC|O_WRONLY|O_EXCL|O_APPEND, 0600);

	if (mailFd >= 0) {
		line->cl_MailFlag = 1;


		fdprintf(mailFd, "To: %s\nSubject: cron for user %s %s\n\n",
				file->cf_UserName,
				file->cf_UserName,
				line->cl_Shell
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
					line->cl_Shell
					);
			exit(0);
		}

		if (DebugOpt)
			logn(LOG_DEBUG, "child running: %s\n", line->cl_Shell);

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
					line->cl_Shell
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
				line->cl_Shell
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

		snprintf(mailFile2, sizeof(mailFile2), TMPDIR "/cron.%s.%d", 
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
EndJob(CronFile *file, CronLine *line)
{
	int mailFd;
	char mailFile[128];
	struct stat sbuf;

	if (line->cl_Pid <= 0) {
		/*
		 * No job
		 */
		line->cl_Pid = 0;
		return;
	}


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

	snprintf(mailFile, sizeof(mailFile), TMPDIR "/cron.%s.%d",
			file->cf_UserName, line->cl_Pid);

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

		execl(SENDMAIL, SENDMAIL, SENDMAIL_ARGS, NULL, NULL);
		logfd(LOG_WARNING, 8, "unable to exec %s %s: cron output for user %s %s to /dev/null\n",
				SENDMAIL,
				SENDMAIL_ARGS,
				file->cf_UserName,
				line->cl_Shell
			   );
		exit(0);
	} else if (line->cl_Pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 */
		logn(LOG_ERR, "unable to fork: cron output for user %s %s to /dev/null\n",
				file->cf_UserName,
				line->cl_Shell
			);
		line->cl_Pid = 0;
	} else {
		/*
		 * PARENT, FORK OK
		 */
	}
	close(mailFd);
}

