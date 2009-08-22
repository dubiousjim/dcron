
/*
 * DATABASE.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include <limits.h>
#include "defs.h"

Prototype void CheckUpdates(const char *dpath, const char *user_override);
Prototype void SynchronizeDir(const char *dpath, const char *user_override, int initial_scan);
Prototype int TestJobs(time_t t1, time_t t2);
Prototype int ArmJob(CronFile *file, CronLine *line);
Prototype void RunJobs(void);
Prototype int CheckJobs(void);

void SynchronizeFile(const char *dpath, const char *fname, const char *uname);
void DeleteFile(CronFile **pfile);
char *ParseField(char *userName, char *ary, int modvalue, int off, int onvalue, const char **names, char *ptr);
void FixDayDow(CronLine *line);

CronFile *FileBase;

const char *DowAry[] = {
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",

	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
	NULL
};

const char *MonAry[] = {
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",

	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec",
	NULL
};

/*
 * Check the cron.update file in the specified directory.  If user_override
 * is NULL then the files in the directory belong to the user whose name is
 * the file, otherwise they belong to the user_override user.
 */
void
CheckUpdates(const char *dpath, const char *user_override)
{
	FILE *fi;
	char buf[256];
	char *ptr;
	char *path;

	if (DebugOpt)
		logn(LOG_DEBUG, "CheckUpdates on %s/%s\n", dpath, CRONUPDATE);

	asprintf(&path, "%s/%s", dpath, CRONUPDATE);
	if ((fi = fopen(path, "r")) != NULL) {
		remove(path);
		while (fgets(buf, sizeof(buf), fi) != NULL) {
			/* 
			 * if buf has only sep chars, return NULL and point ptok at buf's terminating 0
			 * else return pointer to first non-sep of buf and
			 * 		if there's a following sep, overwrite it to 0 and point ptok to next char
			 * 		else point ptok at buf's terminating 0
			 */                                                
			ptr = strtok(buf, " \t\r\n");
			if (user_override)
				SynchronizeFile(dpath, ptr, user_override);
			else if (getpwnam(ptr))
				SynchronizeFile(dpath, ptr, ptr);
			else
				logn(LOG_WARNING, "ignoring %s/%s (non-existent user)\n", dpath, ptr);
		}
		fclose(fi);
	}
	free(path);
}

void
SynchronizeDir(const char *dpath, const char *user_override, int initial_scan)
{
	CronFile **pfile;
	CronFile *file;
	struct dirent *den;
	DIR *dir;
	char *path;

	/*
	 * Delete all database CronFiles for this directory.  DeleteFile() will
	 * free *pfile and relink the *pfile pointer, or in the alternative will
	 * mark it as deleted.
	 */
	pfile = &FileBase;
	while ((file = *pfile) != NULL) {
		if (file->cf_Deleted == 0 && strcmp(file->cf_DPath, dpath) == 0) {
			DeleteFile(pfile);
		} else {
			pfile = &file->cf_Next;
		}
	}

	/*
	 * Since we are resynchronizing the entire directory, remove the
	 * the CRONUPDATE file.
	 */
	asprintf(&path, "%s/%s", dpath, CRONUPDATE);
	remove(path);
	free(path);

	/*
	 * Scan the specified directory
	 */
	if ((dir = opendir(dpath)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			if (strchr(den->d_name, '.') != NULL)
				continue;
			if (strcmp(den->d_name, CRONUPDATE) == 0)
				continue;
			if (user_override) {
				SynchronizeFile(dpath, den->d_name, user_override);
			} else if (getpwnam(den->d_name)) {
				SynchronizeFile(dpath, den->d_name, den->d_name);
			} else {
				logn(LOG_WARNING, "ignoring %s/%s (non-existent user)\n",
						dpath, den->d_name);
			}
		}
		closedir(dir);
	} else {
		if (initial_scan)
			logn(LOG_ERR, "unable to scan directory %s\n", dpath);
		/* softerror, do not exit the program */
	}
}

void
SynchronizeFile(const char *dpath, const char *fileName, const char *userName)
{
	CronFile **pfile;
	CronFile *file;
	int maxEntries;
	int maxLines;
	char buf[1024];
	char *path;
	FILE *fi;

	/*
	 * Limit entries
	 */
	if (strcmp(userName, "root") == 0)
		maxEntries = 65535;
	else
		maxEntries = MAXLINES;
	maxLines = maxEntries * 10;

	/*
	 * Delete any existing copy of this CronFile
	 */
	pfile = &FileBase;
	while ((file = *pfile) != NULL) {
		if (file->cf_Deleted == 0 && strcmp(file->cf_DPath, dpath) == 0 &&
				strcmp(file->cf_FileName, fileName) == 0
		   ) {
			DeleteFile(pfile);
		} else {
			pfile = &file->cf_Next;
		}
	}

	asprintf(&path, "%s/%s", dpath, fileName);
	if ((fi = fopen(path, "r")) != NULL) {
		struct stat sbuf;

		if (fstat(fileno(fi), &sbuf) == 0 && sbuf.st_uid == DaemonUid) {
			CronFile *file = calloc(1, sizeof(CronFile));
			CronLine **pline;

			file->cf_UserName = strdup(userName);
			file->cf_FileName = strdup(fileName);
			file->cf_DPath = strdup(dpath);
			pline = &file->cf_LineBase;

			while (fgets(buf, sizeof(buf), fi) != NULL && --maxLines) {
				CronLine line;
				char *ptr = buf;
				int len;

				while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
					++ptr;

				len = strlen(ptr);
				if (len && ptr[len-1] == '\n')
					ptr[--len] = 0;

				if (*ptr == 0 || *ptr == '#')
					continue;

				if (--maxEntries == 0)
					break;

				bzero(&line, sizeof(line));

				if (DebugOpt)
					logn(LOG_DEBUG, "User %s Entry %s\n", userName, buf);

				/*
				 * parse date ranges
				 */

				ptr = ParseField(file->cf_UserName, line.cl_Mins, 60, 0, 1,
						NULL, ptr);
				ptr = ParseField(file->cf_UserName, line.cl_Hrs,  24, 0, 1,
						NULL, ptr);
				ptr = ParseField(file->cf_UserName, line.cl_Days, 32, 0, 1,
						NULL, ptr);
				ptr = ParseField(file->cf_UserName, line.cl_Mons, 12, -1, 1,
						MonAry, ptr);
				ptr = ParseField(file->cf_UserName, line.cl_Dow, 7, 0, 31,
						DowAry, ptr);
				/*
				 * check failure
				 */

				if (ptr == NULL)
					continue;

				/*
				 * fix days and dow - if one is not * and the other
				 * is *, the other is set to 0, and vise-versa
				 */

				FixDayDow(&line);

				/*
				 * copy command string
				 */
				line.cl_Shell = strdup(ptr);

				line.cl_Description = line.cl_Shell;
				if (DebugOpt) {
					logn(LOG_DEBUG, "    Command %s\n", line.cl_Shell);
				}

				*pline = calloc(1, sizeof(CronLine));
				/* copy working CronLine to newly allocated one */
				**pline = line;

				pline = &((*pline)->cl_Next);
			}

			*pline = NULL;

			file->cf_Next = FileBase;
			FileBase = file;

			if (maxLines == 0 || maxEntries == 0)
				logn(LOG_WARNING, "maximum number of lines reached for user %s\n", userName);
		}
		fclose(fi);
	}
	free(path);
}

char *
ParseField(char *user, char *ary, int modvalue, int off, int onvalue, const char **names, char *ptr)
{
	char *base = ptr;
	int n1 = -1;
	int n2 = -1;

	if (base == NULL)
		return(NULL);

	while (*ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
		int skip = 0;

		/*
		 * Handle numeric digit or symbol or '*'
		 */

		if (*ptr == '*') {
			n1 = 0;			/* everything will be filled */
			n2 = modvalue - 1;
			skip = 1;
			++ptr;
		} else if (*ptr >= '0' && *ptr <= '9') {
			if (n1 < 0)
				n1 = strtol(ptr, &ptr, 10) + off;
			else
				n2 = strtol(ptr, &ptr, 10) + off;
			skip = 1;
		} else if (names) {
			int i;

			for (i = 0; names[i]; ++i) {
				if (strncmp(ptr, names[i], strlen(names[i])) == 0) {
					break;
				}
			}
			if (names[i]) {
				ptr += strlen(names[i]);
				if (n1 < 0)
					n1 = i;
				else
					n2 = i;
				skip = 1;
			}
		}

		/*
		 * handle optional range '-'
		 */

		if (skip == 0) {
			logn(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
			return(NULL);
		}
		if (*ptr == '-' && n2 < 0) {
			++ptr;
			continue;
		}

		/*
		 * collapse single-value ranges, handle skipmark, and fill
		 * in the character array appropriately.
		 */

		if (n2 < 0)
			n2 = n1;

		if (*ptr == '/')
			skip = strtol(ptr + 1, &ptr, 10);

		/*
		 * fill array, using a failsafe is the easiest way to prevent
		 * an endless loop
		 */

		{
			int s0 = 1;
			int failsafe = 1024;

			--n1;
			do {
				n1 = (n1 + 1) % modvalue;

				if (--s0 == 0) {
					ary[n1] = onvalue;
					s0 = skip;
				}
			} while (n1 != n2 && --failsafe);

			if (failsafe == 0) {
				logn(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
				return(NULL);
			}
		}
		if (*ptr != ',')
			break;
		++ptr;
		n1 = -1;
		n2 = -1;
	}

	if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
		logn(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
		return(NULL);
	}

	while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
		++ptr;

	if (DebugOpt) {
		int i;

		for (i = 0; i < modvalue; ++i)
			logn(LOG_DEBUG, "%2x ", ary[i]);
		logn(LOG_DEBUG, "\n");
	}

	return(ptr);
}

void
FixDayDow(CronLine *line)
{
	unsigned short i,j;
	short weekUsed = 0;
	short daysUsed = 0;

	for (i = 0; i < arysize(line->cl_Dow); ++i) {
		if (line->cl_Dow[i] == 0) {
			weekUsed = 1;
			break;
		}
	}
	for (i = 0; i < arysize(line->cl_Days); ++i) {
		if (line->cl_Days[i] == 0) {
			if (weekUsed) {
				if (!daysUsed) {
					daysUsed = 1;
					/* change from "every Mon" to "ith Mon"
					 * 6th,7th... Dow are treated as 1st,2nd... */
					for (j = 0; j < arysize(line->cl_Dow); ++j) {
						line->cl_Dow[j] &= 1 << (i-1)%5;
					}
				} else {
					/* change from "nth Mon" to "nth or ith Mon" */
					for (j = 0; j < arysize(line->cl_Dow); ++j) {
						if (line->cl_Dow[j])
							line->cl_Dow[j] |= 1 << (i-1)%5;
					}
				}
				/* continue cycling through cl_Days */
			}
			else {
				daysUsed = 1;
				break;
			}
		}
	}
	if (weekUsed) {
		memset(line->cl_Days, 0, sizeof(line->cl_Days));
	}
	if (daysUsed && !weekUsed) {
		memset(line->cl_Dow, 0, sizeof(line->cl_Dow));
	}
}

/*
 *  DeleteFile() - destroy a CronFile.
 *
 *  The CronFile (*pfile) is destroyed if possible, and marked cf_Deleted
 *  if there are still active processes running on it.  *pfile is relinked
 *  on success.
 */
void
DeleteFile(CronFile **pfile)
{
	CronFile *file = *pfile;
	CronLine **pline = &file->cf_LineBase;
	CronLine *line;

	file->cf_Running = 0;
	file->cf_Deleted = 1;

	while ((line = *pline) != NULL) {
		if (line->cl_Pid > 0) {
			file->cf_Running = 1;
			pline = &line->cl_Next;
		} else {
			*pline = line->cl_Next;
			free(line->cl_Shell);
			/*
		 	 * if cl_JobName is NULL, Description pointed to ch_Shell, which was already freed
			 */
			free(line);
		}
	}
	if (file->cf_Running == 0) {
		*pfile = file->cf_Next;
		free(file->cf_DPath);
		free(file->cf_FileName);
		free(file->cf_UserName);
		free(file);
	}
}

/*
 * TestJobs()
 *
 * determine which jobs need to be run.  Under normal conditions, the
 * period is about a minute (one scan).  Worst case it will be one
 * hour (60 scans).
 */

int
TestJobs(time_t t1, time_t t2)
{
	short nJobs = 0;
	time_t t;

	/*
	 * Find jobs > t1 and <= t2
	 */

	for (t = t1 - t1 % 60; t <= t2; t += 60) {
		if (t > t1) {
			struct tm *tp = localtime(&t);
			CronFile *file;
			CronLine *line;

			unsigned short n_wday = (tp->tm_mday - 1)%7 + 1;
			if (n_wday >= 4) {
				struct tm tnext = *tp;
				tnext.tm_mday += 7;
				if (mktime(&tnext) != (time_t)-1 && tnext.tm_mon != tp->tm_mon)
					n_wday |= 16;	/* last dow in month is always recognized as 5th */
			}

			for (file = FileBase; file; file = file->cf_Next) {
				if (DebugOpt)
					logn(LOG_DEBUG, "FILE %s/%s USER %s:\n",
							file->cf_DPath, file->cf_FileName, file->cf_UserName);
				if (file->cf_Deleted)
					continue;
				for (line = file->cf_LineBase; line; line = line->cl_Next) {
					if (DebugOpt)
						logn(LOG_DEBUG, "    LINE %s\n", line->cl_Shell);

					if (!(line->cl_Mins[tp->tm_min] &&
							line->cl_Hrs[tp->tm_hour] &&
							(line->cl_Days[tp->tm_mday] || (n_wday && line->cl_Dow[tp->tm_wday]) ) &&
							line->cl_Mons[tp->tm_mon]
					   ))
						continue;
					nJobs += ArmJob(file, line);
				} /* for line */
			}
		}
	}
	return(nJobs);
}

int
ArmJob(CronFile *file, CronLine *line)
{
	if (line->cl_Pid > 0) {
		logn(LOG_NOTICE, "    process already running (%d): %s\n",
				line->cl_Pid,
				line->cl_Description
			);
	} else if (line->cl_Pid == 0) {
		line->cl_Pid = -1;
		file->cf_Ready = 1;
		if (DebugOpt)
			logn(LOG_DEBUG, "    scheduled: %s\n", line->cl_Description);
		return 1;
	}
	return 0;
}


void
RunJobs(void)
{
	CronFile *file;
	CronLine *line;

	for (file = FileBase; file; file = file->cf_Next) {
		if (file->cf_Ready) {
			file->cf_Ready = 0;

			for (line = file->cf_LineBase; line; line = line->cl_Next) {
				if (line->cl_Pid == -1) {

					RunJob(file, line);

					logn(LOG_INFO, "FILE %s/%s USER %s PID %3d %s\n",
							file->cf_DPath,
							file->cf_FileName,
							file->cf_UserName,
							line->cl_Pid,
							line->cl_Description
						);
					if (line->cl_Pid < 0)
						/* how could this happen? RunJob will leave cl_Pid set to 0 or the actual pid */
						file->cf_Ready = 1;
					else if (line->cl_Pid > 0)
						file->cf_Running = 1;
				}
			}
		}
	}
}

/*
 * CheckJobs() - check for job completion
 *
 * Check for job completion, return number of CronFiles still running after
 * all done.
 */

int
CheckJobs(void)
{
	CronFile *file;
	CronLine *line;
	int nStillRunning = 0;

	for (file = FileBase; file; file = file->cf_Next) {
		if (file->cf_Running) {
			file->cf_Running = 0;

			for (line = file->cf_LineBase; line; line = line->cl_Next) {
				if (line->cl_Pid > 0) {
					int status;
					int r = waitpid(line->cl_Pid, &status, WNOHANG);

					if (r < 0 || r == line->cl_Pid) {
						if (r > 0 && WIFEXITED(status))
							status = WEXITSTATUS(status);
						else
							status = 1;
						EndJob(file, line, status);
						if (line->cl_Pid)
							file->cf_Running = 1;
					} else if (r == 0) {
						file->cf_Running = 1;
					}
				}
			}
		}
		nStillRunning += file->cf_Running;
	}
	return(nStillRunning);
}

