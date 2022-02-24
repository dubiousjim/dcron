
/*
 * DATABASE.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2019 James Pryor <dubiousjim@gmail.com>
 * Copyright 2020 Mark Hills <mark@xwax.org>
 * May be distributed under the GNU General Public License version 2 or any later version.
 */

#include "defs.h"

#define FIRST_DOW  (1 << 0)
#define SECOND_DOW (1 << 1)
#define THIRD_DOW  (1 << 2)
#define FOURTH_DOW (1 << 3)
#define FIFTH_DOW  (1 << 4)
#define LAST_DOW   (1 << 5)
#define ALL_DOW    (FIRST_DOW|SECOND_DOW|THIRD_DOW|FOURTH_DOW|FIFTH_DOW|LAST_DOW)

Prototype void CheckUpdates(const char *dpath, int is_system, time_t t1, time_t t2);
Prototype void SynchronizeDir(const char *dpath, int is_system, int initial_scan);
Prototype void ReadTimestamps(const char *user);
Prototype int TestJobs(time_t t1, time_t t2);
Prototype int TestStartupJobs(void);
Prototype int ArmJob(CronFile *file, CronLine *line, time_t t1, time_t t2);
Prototype void RunJobs(void);
Prototype int CheckJobs(void);

void SynchronizeFile(const char *dpath, const char *fname, const char *uname, int parseUser);
void DeleteFile(CronFile **pfile);
void DeleteLineContent(CronLine *line);
char *ParseInterval(int *interval, char *ptr);
char *ParseField(char *ary, int modvalue, int offset, int onvalue, const char **names, char *ptr);
void FixDayDow(CronLine *line);
void PrintLine(CronLine *line);
void PrintFile(CronFile *file, char* loc, char* fname, int line);

CronFile *FileBase = NULL;

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

const char *FreqAry[] = {
	"noauto",
	"reboot",
	"hourly",
	"daily",
	"weekly",
	"monthly",
	"yearly",
	NULL
};

/*
 * Check the cron.update file in the specified directory.  System crontabs
 * are 'owned' by root but parse a username to run each job as.
 */
void
CheckUpdates(const char *dpath, int is_system, time_t t1, time_t t2)
{
	FILE *fi;
	char buf[SMALL_BUFFER];
	char *fname, *ptok, *job;
	char *path;

	if (!(path = concat(dpath, "/", CRONUPDATE, NULL))) {
		errno = ENOMEM;
		perror("CheckUpdates");
		exit(1);
	}
	if ((fi = fopen(path, "r")) != NULL) {
		remove(path);
		printlogf(LOG_INFO, "reading %s/%s\n", dpath, CRONUPDATE);
		while (fgets(buf, sizeof(buf), fi) != NULL) {
			/*
			 * if buf has only sep chars, return NULL and point ptok at buf's terminating 0
			 * else return pointer to first non-sep of buf and
			 * 		if there's a following sep, overwrite it to 0 and point ptok to next char
			 * 		else point ptok at buf's terminating 0
			 */
			fname = strtok_r(buf, " \t\n", &ptok);

			if (is_system)
				SynchronizeFile(dpath, fname, "root", 1);
			else if (!getpwnam(fname))
				printlogf(LOG_WARNING, "ignoring %s/%s (non-existent user)\n", dpath, fname);
			else if (*ptok == 0 || *ptok == '\n') {
				SynchronizeFile(dpath, fname, fname, 0);
				ReadTimestamps(fname);
			} else {
				/* if fname is followed by whitespace, we prod any following jobs */
				CronFile *file = FileBase;
				while (file) {
					if (strcmp(file->cf_UserName, fname) == 0)
						break;
					file = file->cf_Next;
				}
				if (!file)
					printlogf(LOG_WARNING, "unable to prod for user %s: no crontab\n", fname);
				else {
					CronLine *line;
					/* calling strtok(ptok...) then strtok(NULL) is equiv to calling strtok_r(NULL,..&ptok) */
					while ((job = strtok(ptok, " \t\n")) != NULL) {
						time_t force = t2;
						ptok = NULL;
						if (*job == '!') {
							force = (time_t)-1;
							++job;
						}
						line = file->cf_LineBase;
						while (line) {
							if (line->cl_JobName && strcmp(line->cl_JobName, job) == 0)
								break;
							line = line->cl_Next;
						}
						if (line)
							ArmJob(file, line, t1, force);
						else {
							printlogf(LOG_WARNING, "unable to prod for user %s: unknown job %s\n", fname, job);
							/* we can continue parsing this line, we just don't install any CronWaiter for the requested job */
						}
					}
				}
			}
		}
		fclose(fi);
	}
	free(path);
}

void
SynchronizeDir(const char *dpath, int is_system, int initial_scan)
{
	CronFile **pfile;
	CronFile *file;
	struct dirent *den;
	DIR *dir;
	char *path;

	if (DebugOpt)
		printlogf(LOG_DEBUG, "Synchronizing %s\n", dpath);

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
	if (!(path = concat(dpath, "/", CRONUPDATE, NULL))) {
		errno = ENOMEM;
		perror("SynchronizeDir");
		exit(1);
	}
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
			if (is_system) {
				SynchronizeFile(dpath, den->d_name, "root", 1);
			} else if (getpwnam(den->d_name)) {
				SynchronizeFile(dpath, den->d_name, den->d_name, 0);
			} else {
				printlogf(LOG_WARNING, "ignoring %s/%s (non-existent user)\n",
						dpath, den->d_name);
			}
		}
		closedir(dir);
	} else {
		if (initial_scan)
			printlogf(LOG_ERR, "unable to scan directory %s\n", dpath);
			/* softerror, do not exit the program */
	}
}


void
ReadTimestamps(const char *user)
{
	CronFile *file;
	CronLine *line;
	FILE *fi;
	char buf[SMALL_BUFFER];
	char *ptr;
	struct tm tm = {0};
	time_t sec, freq;

	file = FileBase;
	while (file != NULL) {
		if (file->cf_Deleted == 0 && (!user || strcmp(user, file->cf_UserName) == 0)) {
			line = file->cf_LineBase;
			while (line != NULL) {
				if (line->cl_Timestamp) {
					if ((fi = fopen(line->cl_Timestamp, "r")) != NULL) {
						if (fgets(buf, sizeof(buf), fi) != NULL) {
							int fake = 0;
							ptr = buf;
							if (strncmp(buf, "after ", 6) == 0) {
								fake = 1;
								ptr += 6;
							}
							sec = (time_t)-1;
							ptr = strptime(ptr, CRONSTAMP_FMT, &tm);
							if (ptr && (*ptr == 0 || *ptr == '\n')) {
								/* strptime uses current seconds when seconds not specified? anyway, we don't get round minutes */
								tm.tm_sec = 0;
								tm.tm_isdst = -1;
								sec = mktime(&tm);
							}
							if (sec == (time_t)-1) {
								printlogf(LOG_ERR, "unable to parse timestamp (user %s job %s)\n", file->cf_UserName, line->cl_JobName);
								/* we continue checking other timestamps in this CronFile */
							} else {
								/* sec -= sec % 60; */
								if (fake) {
									line->cl_NotUntil = sec;
								} else {
									line->cl_LastRan = sec;
									freq = (line->cl_Freq > 0) ? line->cl_Freq : line->cl_Delay;
									/* if (line->cl_NotUntil < line->cl_LastRan + freq) */
									line->cl_NotUntil = line->cl_LastRan + freq;
								}
							}
						}
						fclose(fi);
					} else {
						int succeeded = 0;
						printlogf(LOG_NOTICE, "no timestamp found (user %s job %s)\n", file->cf_UserName, line->cl_JobName);
						/* write a fake timestamp file so our initial NotUntil doesn't keep being reset every hour when crond does a SynchronizeDir */
						if ((fi = fopen(line->cl_Timestamp, "w")) != NULL) {
							if (strftime(buf, sizeof(buf), CRONSTAMP_FMT, localtime(&line->cl_NotUntil)))
								if (fputs("after ", fi) >= 0)
									if (fputs(buf,fi) >= 0)
										succeeded = 1;
							fclose(fi);
						}
						if (!succeeded)
							printlogf(LOG_WARNING, "unable to write timestamp to %s (user %s %s)\n", line->cl_Timestamp, file->cf_UserName, line->cl_Description);
					}
				}
				line = line->cl_Next;
			}
		}
		file = file->cf_Next;
	}
}

/*
 * parse @hourly, etc
 *
 * Return: NULL on parse error
 */
static char*
ParseTimeInterval(CronLine *line, char *ptr)
{
	int	j;
	line->cl_Delay = -1;
	ptr += 1;
	for (j = 0; FreqAry[j]; ++j) {
		if (strncmp(ptr, FreqAry[j], strlen(FreqAry[j])) == 0) {
			break;
		}
	}
	if (FreqAry[j]) {
		ptr += strlen(FreqAry[j]);
		switch(j) {
		case 0:
			/* noauto */
			line->cl_Freq = -2;
			line->cl_Delay = 0;
			break;
		case 1:
			/* reboot */
			line->cl_Freq = -1;
			line->cl_Delay = 0;
			break;
		case 2:
			line->cl_Freq = HOURLY_FREQ;
			break;
		case 3:
			line->cl_Freq = DAILY_FREQ;
			break;
		case 4:
			line->cl_Freq = WEEKLY_FREQ;
			break;
		case 5:
			line->cl_Freq = MONTHLY_FREQ;
			break;
		case 6:
			line->cl_Freq = YEARLY_FREQ;
			break;
			/* else line->cl_Freq will remain 0 */
		}
	}

	if (!line->cl_Freq || (*ptr != ' ' && *ptr != '\t'))
		return NULL;

	if (line->cl_Delay < 0) {
		/*
		 * delays on @daily, @hourly, etc are 1/20 of the frequency
		 * so they don't all start at once
		 * this also affects how they behave when the job returns EAGAIN
		 */
		line->cl_Delay = line->cl_Freq / 20;
		line->cl_Delay -= line->cl_Delay % 60;
		if (line->cl_Delay == 0)
			line->cl_Delay = 60;
		/* all minutes are permitted */
		for (j=0; j<60; ++j)
			line->cl_Mins[j] = 1;
		for (j=0; j<24; ++j)
			line->cl_Hrs[j] = 1;
		for (j=1; j<32; ++j)
			/* days are numbered 1..31 */
			line->cl_Days[j] = 1;
		for (j=0; j<12; ++j)
			line->cl_Mons[j] = 1;
	}

	while (*ptr == ' ' || *ptr == '\t')
		++ptr;

	return ptr;
}

/*
 * parse time and date fields
 */
static char*
ParseTimeSpec(CronLine *line, char *ptr)
{
	ptr = ParseField(line->cl_Mins, FIELD_MINUTES, 0, 1, NULL, ptr);
	ptr = ParseField(line->cl_Hrs,  FIELD_HOURS, 0, 1, NULL, ptr);
	ptr = ParseField(line->cl_Days, FIELD_M_DAYS, 0, 1, NULL, ptr);
	ptr = ParseField(line->cl_Mons, FIELD_MONTHS, -1, 1, MonAry, ptr);
	ptr = ParseField(line->cl_Dow,  FIELD_W_DAYS, 0, ALL_DOW, DowAry, ptr);

	if (!ptr)
		return NULL;

	/*
	 * fix days and dow - if one is not * and the other
	 * is *, the other is set to 0, and vise-versa
	 */
	FixDayDow(line);

	return ptr;
}

static char*
ParseOneAttribute(CronFile *file, CronLine *line, char *ptr,
		const char *path, const char *buf)
{
	if (strncmp(ptr, ID_TAG, strlen(ID_TAG)) == 0) {
		if (line->cl_JobName) {
			/* only assign ID_TAG once */
			printlogf(LOG_WARNING, "%s: Multiple use of " ID_TAG " is invalid: %s\n", path, buf);
			ptr = NULL;
		} else {
			ptr += strlen(ID_TAG);
			/*
			 * name = strsep(&ptr, seps):
			 * return name = ptr, and if ptr contains sep chars, overwrite first with 0 and point ptr to next char
			 *                    else set ptr=NULL
			 */
			if (!(line->cl_Description = concat("job ", strsep(&ptr, " \t"), NULL))) {
				errno = ENOMEM;
				perror("SynchronizeFile");
				exit(1);
			}
			line->cl_JobName = line->cl_Description + 4;
			if (!ptr)
				printlogf(LOG_WARNING, "%s: Entry ends unexpectedly after " ID_TAG "%s: %s\n", path, line->cl_JobName, buf);
		}
	} else if (strncmp(ptr, FREQ_TAG, strlen(FREQ_TAG)) == 0) {
		if (line->cl_Freq) {
			/* only assign FREQ_TAG once */
			printlogf(LOG_WARNING, "%s: Multiple use of " FREQ_TAG " is invalid: %s\n", path, buf);
			ptr = NULL;
		} else {
			ptr += strlen(FREQ_TAG);
			ptr = ParseInterval(&line->cl_Freq, ptr);
			if (ptr && *ptr == '/')
				ptr = ParseInterval(&line->cl_Delay, ++ptr);
			else
				line->cl_Delay = line->cl_Freq;
			if (!ptr)
				printlogf(LOG_WARNING, "%s: Entry ends unexpectedly after " FREQ_TAG ": %s", path, buf);
		}
	} else if (strncmp(ptr, WAIT_TAG, strlen(WAIT_TAG)) == 0) {
		if (line->cl_Waiters) {
			/* only assign WAIT_TAG once */
			printlogf(LOG_WARNING, "%s: Multiple use of " WAIT_TAG " is invalid: %s\n", path, buf);
			ptr = NULL;
		} else {
			short more = 1;
			char *name;
			ptr += strlen(WAIT_TAG);
			do {
				CronLine *job, **pjob;
				if (strcspn(ptr,",") < strcspn(ptr," \t"))
					name = strsep(&ptr, ",");
				else {
					more = 0;
					name = strsep(&ptr, " \t");
				}
				if (!ptr || *ptr == 0) {
					/* unexpectedly this was the last token in buf; so abort */
					printlogf(LOG_WARNING, "%s: Entry ends unexpectedly after " WAIT_TAG "%s: %s", path, name, buf);
					ptr = NULL;
				} else {
					int waitfor = 0;
					char *w, *wsave;
					if ((w = strchr(name, '/')) != NULL) {
						wsave = w++;
						w = ParseInterval(&waitfor, w);
						if (!w || *w != 0) {
							printlogf(LOG_WARNING, "%s: Could not parse interval for " WAIT_TAG "%s: %s\n", path, name, buf);
							ptr = NULL;
						} else
							/* truncate name */
							*wsave = 0;
					}
					if (ptr) {
						/* look for a matching CronLine */
						pjob = &file->cf_LineBase;
						while ((job = *pjob) != NULL) {
							if (job->cl_JobName && strcmp(job->cl_JobName, name) == 0) {
								CronWaiter *waiter = malloc(sizeof(CronWaiter));
								CronNotifier *notif = malloc(sizeof(CronNotifier));
								waiter->cw_Flag = -1;
								waiter->cw_MaxWait = waitfor;
								waiter->cw_NotifLine = job;
								waiter->cw_Notifier = notif;
								waiter->cw_Next = line->cl_Waiters;	/* add to head of line->cl_Waiters */
								line->cl_Waiters = waiter;
								notif->cn_Waiter = waiter;
								notif->cn_Next = job->cl_Notifs;	/* add to head of job->cl_Notifs */
								job->cl_Notifs = notif;
								break;
							} else
								pjob = &job->cl_Next;
						}
						if (!job) {
							printlogf(LOG_WARNING, "%s: Ignoring unknown job: " WAIT_TAG "%s: %s\n", path, name, buf);
							/* we can continue parsing this line, we just don't install any CronWaiter for the requested job */
						}
					}
				}
			} while (ptr && more);
		}
	}

	return ptr;
}

/*
 * Parse as many things as we can that look like attributes
 *
 * Parsing stops when we encounter something that isn't identifiable
 * and its assumed to be the next field.
 *
 * Return NULL on error
 */
static char*
ParseAttributes(CronFile *file, CronLine *line, char *ptr,
		const char *path, const char *buf)
{
	for (;;) {
		char *prev = ptr;

		ptr = ParseOneAttribute(file, line, ptr, path, buf);
		if (!ptr)
			return NULL; /* error */
		if (ptr == prev)
			return ptr; /* nothing consumed; not understood */

		while (*ptr == ' ' || *ptr == '\t')
			++ptr;
	}
}

/*
 * Parse a single line in the file
 *
 * On completion the caller is always responsible for deallocating the
 * content of "line", whether this function fully populated the line
 * or not.
 *
 * Return: 1 if "line" was fully populated, otherwise 0
 */
int
ParseLine(CronFile *file, const char *userName, int parseUser, CronLine *line, char *buf, time_t tnow, const char *path)
{
	char *ptr = buf;
	int len;

	/* Always return with the parsed line in a defined state */
	memset(line, 0, sizeof(*line));

	while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
		++ptr;

	len = strlen(ptr);
	if (len && ptr[len-1] == '\n')
		ptr[--len] = 0;

	if (*ptr == 0 || *ptr == '#')
		return 0;

	if (DebugOpt)
		printlogf(LOG_DEBUG, "%s as %s: %s\n", path, userName, buf);

	if (*ptr == '@') {
		ptr = ParseTimeInterval(line, ptr);
		if (!ptr) {
			printlogf(LOG_WARNING, "%s: Failed parsing '@' interval: %s\n", path, buf);
			return 0;
		}
	} else {
		ptr = ParseTimeSpec(line, ptr);
		if (!ptr) {
			printlogf(LOG_WARNING, "%s: Failed to parse date/time specification: %s\n", path, buf);
			return 0;
		}
	}

	/* check for ID=... and AFTER=... and FREQ=... */
	ptr = ParseAttributes(file, line, ptr, path, buf);
	if (!ptr)
		return 0;  /* error already logged */

	/*
	 * Filter out a job name which is the empty string
	 */
	if (line->cl_JobName && line->cl_JobName[0] == '\0') {
		free(line->cl_Description);
		line->cl_Description = NULL;
		line->cl_JobName = NULL;
	}

	if (line->cl_Delay > 0 && !line->cl_JobName) {
		printlogf(LOG_WARNING, "%s: Writing timestamp requries job to be named: %s\n", path, buf);
		return 0;
	}

	/*
	 * system crontabs (cron.d) have an extra field for username
	 */
	if (parseUser)
		line->cl_UserName = strdup(strsep(&ptr, " \t"));
	else
		line->cl_UserName = strdup(userName);

	if (!ptr) {
		printlogf(LOG_WARNING, "%s: Could not parse; a username and command is expected in a system crontab: %s\n", path, buf);
		return 0;
	}

	/*
	 * copy command string
	 */
	line->cl_Shell = strdup(ptr);

	if (line->cl_Delay > 0) {
		if (!(line->cl_Timestamp = concat(TSDir, "/", userName, ".", line->cl_JobName, NULL))) {
			errno = ENOMEM;
			perror("SynchronizeFile");
			exit(1);
		}
		line->cl_NotUntil = tnow + line->cl_Delay;
	}

	if (line->cl_JobName) {
		if (DebugOpt)
			printlogf(LOG_DEBUG, "    Command %s Job %s\n\n", line->cl_Shell, line->cl_JobName);
	} else {
		/* when cl_JobName is NULL, we point cl_Description to cl_Shell */
		line->cl_Description = line->cl_Shell;
		if (DebugOpt)
			printlogf(LOG_DEBUG, "    Command %s\n\n", line->cl_Shell);
	}

	return 1;
}

void
SynchronizeFile(const char *dpath, const char *fileName, const char *userName, int parseUser)
{
	CronFile **pfile;
	CronFile *file;
	int maxEntries;
	int maxLines;
	char buf[RW_BUFFER]; /* max length for crontab lines */
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

	if (!(path = concat(dpath, "/", fileName, NULL))) {
		errno = ENOMEM;
		perror("SynchronizeFile");
		exit(1);
	}
	if ((fi = fopen(path, "r")) != NULL) {
		struct stat sbuf;

		if (fstat(fileno(fi), &sbuf) == 0 && sbuf.st_uid == DaemonUid) {
			CronFile *file = calloc(1, sizeof(CronFile));
			CronLine **pline;
			time_t tnow = time(NULL);
			tnow -= tnow % 60;

			file->cf_UserName = strdup(userName);
			file->cf_FileName = strdup(fileName);
			file->cf_DPath = strdup(dpath);
			pline = &file->cf_LineBase;

			/* fgets reads at most size-1 chars until \n or EOF, then adds a\0; \n if present is stored in buf */
			while (fgets(buf, sizeof(buf), fi) != NULL && --maxLines) {
				CronLine line;

				if (--maxEntries == 0)
					break;

				if (!ParseLine(file, userName, parseUser, &line, buf, tnow, path)) {
					DeleteLineContent(&line);
					continue;
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
				printlogf(LOG_WARNING, "%s: Limit on number of entries has been reached\n", path);
		}
		fclose(fi);
	}
	free(path);
}

char *
ParseInterval(int *interval, char *ptr)
{
	int n = 0;
	if (ptr && *ptr >= '0' && *ptr <= '9' && (n = strtol(ptr, &ptr, 10)) > 0)
		switch (*ptr) {
			case 'm':
				n *= 60;
				break;
			case 'h':
				n *= HOURLY_FREQ;
				break;
			case 'd':
				n *= DAILY_FREQ;
				break;
			case 'w':
				n *= WEEKLY_FREQ;
				break;
			default:
				n = 0;
		}
	if (n > 0) {
		*interval = n;
		return (ptr+1);
	} else
		return (NULL);
}

char *
ParseField(char *ary, int modvalue, int offset, int onvalue, const char **names, char *ptr)
{
	char *base = ptr;
	int n1 = -1;
	int n2 = -1;

	if (base == NULL)
		return (NULL);

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
				n1 = strtol(ptr, &ptr, 10) + offset;
			else
				n2 = strtol(ptr, &ptr, 10) + offset;
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

		if (skip == 0)
			return(NULL);
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

		n2 = n2 % modvalue;

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

			if (failsafe == 0)
				return(NULL);
		}
		if (*ptr != ',')
			break;
		++ptr;
		n1 = -1;
		n2 = -1;
	}

	if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n')
		return(NULL);

	while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
		++ptr;

	if (DebugOpt) {
		int i;

		for (i = 0; i < modvalue; ++i)
			if (modvalue == FIELD_W_DAYS)
				printlogf(LOG_DEBUG, "%2x ", ary[i]);
			else
				printlogf(LOG_DEBUG, "%d", ary[i]);
		printlogf(LOG_DEBUG, "\n");
	}

	return(ptr);
}

/* Reconcile Days of Month with Days of Week.
 * There are four cases to cover:
 * 1) DoM and DoW are both specified as *; the task may run on any day
 * 2) DoM is * and DoW is specific; the task runs weekly on the specified DoW(s)
 * 3) DoM is specific and DoW is *; the task runs on the specified DoM, regardless
 *    of which day of the week they fall
 * 4) DoM is in the range [1..5] and DoW is specific; the task runs on the Nth
 *    specified DoW. DoM > 5 means the last such DoW in that month
 */
void
FixDayDow(CronLine *line)
{
	unsigned short i;
	short DowStar = 1;
	short DomStar = 1;
	char mask = 0;

	for (i = 0; i < arysize(line->cl_Dow); ++i) {
		if (line->cl_Dow[i] == 0) {
			/* '*' was NOT specified in the DoW field on this CronLine */
			DowStar = 0;
			break;
		}
	}

	for (i = 0; i < arysize(line->cl_Days); ++i) {
		if (line->cl_Days[i] == 0) {
			/* '*' was NOT specified in the Date field on this CronLine */
			DomStar = 0;
			break;
		}
	}

	/* When cases 1, 2 or 3 there is nothing left to do */
	if (DowStar || DomStar)
		return;

	/* Set individual bits within the DoW mask... */
	for (i = 0; i < arysize(line->cl_Days); ++i) {
		if (line->cl_Days[i]) {
			if (i < 6)
				mask |= 1 << (i - 1);
			else
				mask |= LAST_DOW;
		}
	}

	/* and apply the mask to each DoW element */
	for (i = 0; i < arysize(line->cl_Dow); ++i) {
		if (line->cl_Dow[i])
			line->cl_Dow[i] = mask;
		else
			line->cl_Dow[i] = 0;
	}

	/* case 4 relies on the DoW value to guard the date instead of using the
	 * cl_Days field for this purpose; so we must set each element of cl_Days
	 * to 1 to allow the DoW bitmask test to be made
	 */
	memset(line->cl_Days, 1, sizeof(line->cl_Days));
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
		if (line->cl_Pid > JOB_NONE) {
			file->cf_Running = 1;
			pline = &line->cl_Next;
		} else {
			*pline = line->cl_Next;
			DeleteLineContent(line);
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
 * Free resources associated with the given line, which may be incomplete.
 */
void DeleteLineContent(CronLine *line)
{
	CronWaiter **pwaiters, *waiters;
	CronNotifier **pnotifs, *notifs;

	free(line->cl_Shell);
	free(line->cl_UserName);

	if (line->cl_JobName)
		/* this frees both cl_Description and cl_JobName
		 * if cl_JobName is NULL, Description pointed to ch_Shell, which was already freed
		 */
		free(line->cl_Description);
	if (line->cl_Timestamp)
		free(line->cl_Timestamp);

	pnotifs = &line->cl_Notifs;
	while ((notifs = *pnotifs) != NULL) {
		*pnotifs = notifs->cn_Next;
		if (notifs->cn_Waiter) {
			notifs->cn_Waiter->cw_NotifLine = NULL;
			notifs->cn_Waiter->cw_Notifier = NULL;
		}
		free(notifs);
	}
	pwaiters = &line->cl_Waiters;
	while ((waiters = *pwaiters) != NULL) {
		*pwaiters = waiters->cw_Next;
		if (waiters->cw_Notifier)
			waiters->cw_Notifier->cn_Waiter = NULL;
		free(waiters);
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
	CronFile *file;
	CronLine *line;

	PrintFile(FileBase, "TestJobs()", __FILE__, __LINE__);
	for (file = FileBase; file; file = file->cf_Next) {
		if (file->cf_Deleted)
			continue;
		for (line = file->cf_LineBase; line; line = line->cl_Next) {
			struct CronWaiter *waiter;

			if (line->cl_Pid == JOB_WAITING) {
				/* can job stop waiting? */
				int ready = 1;
				waiter = line->cl_Waiters;
				while (waiter != NULL) {
					if (waiter->cw_Flag > 0) {
						/* notifier exited unsuccessfully */
						ready = 2;
						break;
					} else if (waiter->cw_Flag < 0)
						/* still waiting, notifier hasn't run to completion */
						ready = 0;
					waiter = waiter->cw_Next;
				}
				if (ready == 2) {
					if (DebugOpt)
						printlogf(LOG_DEBUG, "cancelled waiting: user %s %s\n", file->cf_UserName, line->cl_Description);
					line->cl_Pid = JOB_NONE;
				} else if (ready) {
					if (DebugOpt)
						printlogf(LOG_DEBUG, "finished waiting: user %s %s\n", file->cf_UserName, line->cl_Description);
					nJobs += ArmJob(file, line, 0, -1);
					/*
					 if (line->cl_NotUntil)
						 line->cl_NotUntil = t2;
					*/
				}
			}
		}
	}

	/*
	 * Find jobs > t1 and <= t2
	 */

	for (t = t1 - t1 % 60; t <= t2; t += 60) {
		if (t > t1) {
			struct tm *tp = localtime(&t);

			char n_wday = 1 << ((tp->tm_mday - 1) / 7);
			if (n_wday >= FOURTH_DOW) {
				struct tm tnext = *tp;
				tnext.tm_mday += 7;
				if (mktime(&tnext) != (time_t)-1 && tnext.tm_mon != tp->tm_mon)
					n_wday |= LAST_DOW;	/* last dow in month is always recognized as 6th bit */
			}

			for (file = FileBase; file; file = file->cf_Next) {
				if (file->cf_Deleted)
					continue;
				for (line = file->cf_LineBase; line; line = line->cl_Next) {
					if ((line->cl_Pid == JOB_WAITING || line->cl_Pid == JOB_NONE) && (line->cl_Freq == 0 || (line->cl_Freq > 0 && t2 >= line->cl_NotUntil))) {
						/* (re)schedule job? */
						if (line->cl_Mins[tp->tm_min] &&
								line->cl_Hrs[tp->tm_hour] &&
								(line->cl_Days[tp->tm_mday] && n_wday & line->cl_Dow[tp->tm_wday])
						   ) {
							if (line->cl_NotUntil)
								line->cl_NotUntil = t2 - t2 % 60 + line->cl_Delay; /* save what minute this job was scheduled/started waiting, plus cl_Delay */
							nJobs += ArmJob(file, line, t1, t2);
						}
					}
				}
			}
		}
	}
	return(nJobs);
}

/*
 * ArmJob: if t2 is (time_t)-1, we force-schedule the job without any waiting
 * else it will wait on any of its declared notifiers who will run <= t2 + cw_MaxWait
 */

int
ArmJob(CronFile *file, CronLine *line, time_t t1, time_t t2)
{
	struct CronWaiter *waiter;
	if (line->cl_Pid > JOB_NONE) {
		printlogf(LOG_NOTICE, "process already running (%d): user %s %s\n",
				line->cl_Pid,
				file->cf_UserName,
				line->cl_Description
			);
	} else if (t2 == -1 && line->cl_Pid != JOB_ARMED) {
		line->cl_Pid = JOB_ARMED;
		file->cf_Ready = 1;
		return 1;
	} else if (line->cl_Pid == JOB_NONE) {
		/* arming a waiting job (cl_Pid == -2) without forcing has no effect */
		line->cl_Pid = JOB_ARMED;
		/* if we have any waiters, zero them and arm cl_Pid=-2 */
		waiter = line->cl_Waiters;
		while (waiter != NULL) {
			/* check if notifier will run <= t2 + cw_Max_Wait? */
			if (!waiter->cw_NotifLine)
				/* notifier deleted */
				waiter->cw_Flag = 0;
			else if (waiter->cw_NotifLine->cl_Pid != JOB_NONE) {
				/* if notifier is armed, or waiting, or running, we wait for it */
				waiter->cw_Flag = -1;
				line->cl_Pid = JOB_WAITING;
			} else if (waiter->cw_NotifLine->cl_Freq < 0) {
				/* arm any @noauto or @reboot jobs we're waiting on */
				ArmJob(file, waiter->cw_NotifLine, t1, t2);
				waiter->cw_Flag = -1;
				line->cl_Pid = JOB_WAITING;
			} else {
				time_t t;
				if (waiter->cw_MaxWait == 0)
					/* when no MaxWait interval specified, we always wait */
					waiter->cw_Flag = -1;
				else if (waiter->cw_NotifLine->cl_Freq == 0 || (waiter->cw_NotifLine->cl_Freq > 0 && t2 + waiter->cw_MaxWait >= waiter->cw_NotifLine->cl_NotUntil)) {
					/* default is don't wait */
					waiter->cw_Flag = 0;
					for (t = t1 - t1 % 60; t <= t2; t += 60) {
						if (t > t1) {
							struct tm *tp = localtime(&t);

							char n_wday = 1 << ((tp->tm_mday - 1) / 7);
							if (n_wday >= FOURTH_DOW) {
								struct tm tnext = *tp;
								tnext.tm_mday += 7;
								if (mktime(&tnext) != (time_t)-1 && tnext.tm_mon != tp->tm_mon)
									n_wday |= LAST_DOW;	/* last dow in month is always recognized as 6th */
							}
							if (line->cl_Mins[tp->tm_min] &&
									line->cl_Hrs[tp->tm_hour] &&
									(line->cl_Days[tp->tm_mday] && n_wday & line->cl_Dow[tp->tm_wday])
							   ) {
								/* notifier will run soon enough, we wait for it */
								waiter->cw_Flag = -1;
								line->cl_Pid = JOB_WAITING;
								break;
							}
						}
					}
				}
			}
			waiter = waiter->cw_Next;
		}
		if (line->cl_Pid == JOB_ARMED) {
			/* job is ready to run */
			file->cf_Ready = 1;
			if (DebugOpt)
				printlogf(LOG_DEBUG, "scheduled: user %s %s\n",
						file->cf_UserName,
						line->cl_Description
					);
			return 1;
		} else if (DebugOpt)
			printlogf(LOG_DEBUG, "waiting: user %s %s\n",
					file->cf_UserName,
					line->cl_Description
				);
	}
	return 0;
}

int
TestStartupJobs(void)
{
	short nJobs = 0;
	time_t t1 = time(NULL);
	CronFile *file;
	CronLine *line;

	t1 = t1 - t1 % 60 + 60;

	for (file = FileBase; file; file = file->cf_Next) {
		if (DebugOpt)
			printlogf(LOG_DEBUG, "TestStartup for FILE %s/%s USER %s:\n",
				file->cf_DPath, file->cf_FileName, file->cf_UserName);
		for (line = file->cf_LineBase; line; line = line->cl_Next) {
			struct CronWaiter *waiter;
			if (DebugOpt) {
				if (line->cl_JobName)
					printlogf(LOG_DEBUG, "    LINE %s JOB %s\n", line->cl_Shell, line->cl_JobName);
				else
					printlogf(LOG_DEBUG, "    LINE %s\n", line->cl_Shell);
			}

			if (line->cl_Freq == -1) {
				/* freq is @reboot */

				line->cl_Pid = JOB_ARMED;
				/* if we have any waiters, reset them and arm Pid = -2 */
				waiter = line->cl_Waiters;
				while (waiter != NULL) {
					waiter->cw_Flag = -1;
					line->cl_Pid = JOB_WAITING;
					/* we only arm @noauto jobs we're waiting on, not other @reboot jobs */
					if (waiter->cw_NotifLine && waiter->cw_NotifLine->cl_Freq == -2)
						ArmJob(file, waiter->cw_NotifLine, t1, t1+60);
					waiter = waiter->cw_Next;
				}
				if (line->cl_Pid == JOB_ARMED) {
					/* job is ready to run */
					file->cf_Ready = 1;
					++nJobs;
					if (DebugOpt)
						printlogf(LOG_DEBUG, "    scheduled: %s\n", line->cl_Description);
				} else if (DebugOpt)
					printlogf(LOG_DEBUG, "    waiting: %s\n", line->cl_Description);

			}

		} /* for line */
	}
	return(nJobs);
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
				if (line->cl_Pid == JOB_ARMED) {

					RunJob(file, line);

					printlogf(LOG_INFO, "FILE %s/%s USER %s PID %3d %s\n",
							file->cf_DPath,
							file->cf_FileName,
							file->cf_UserName,
							line->cl_Pid,
							line->cl_Description
						);
					if (line->cl_Pid < JOB_NONE)
						/* QUESTION how could this happen? RunJob will leave cl_Pid set to 0 or the actual pid */
						file->cf_Ready = 1;
					else if (line->cl_Pid > JOB_NONE)
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
				if (line->cl_Pid > JOB_NONE) {
					int status;
					int r = waitpid(line->cl_Pid, &status, WNOHANG);

					/* waitpid returns -1 for error, 0 if cl_Pid still running, cl_Pid if it's dead */

					if (r < 0 || r == line->cl_Pid) {
						if (r > 0 && WIFEXITED(status))
							status = WEXITSTATUS(status);
						else
							status = 1;
						EndJob(file, line, status);

					} else if (r == 0) {
						file->cf_Running = 1;
					}
				}
			}
			nStillRunning += file->cf_Running;
		}
		/* For the purposes of this check, increase the "still running" counter if a file has lines that are waiting */
		if (file->cf_Running == 0) {
			for (line = file->cf_LineBase; line; line = line->cl_Next) {
				if (line->cl_Pid == JOB_WAITING) {
					nStillRunning += 1;
					break;
				}
			}
		}
	}
	return(nStillRunning);
}

void
PrintLine(CronLine *line)
{
	int i;
	if (!line)
		return;

	printlogf(LOG_DEBUG, "CronLine:\n------------\n");
	printlogf(LOG_DEBUG, "  Command: %s\n", line->cl_Shell);
	printlogf(LOG_DEBUG, "  User: %s\n", line->cl_UserName);
	//printlogf(LOG_DEBUG, "  Desc:    %s\n", line->cl_Description);
	printlogf(LOG_DEBUG, "  Freq:    %s\n", (line->cl_Freq ?
				(line->cl_Freq == -1 ? "(noauto)" : "(startup") : "(use arrays)"));
	printlogf(LOG_DEBUG, "  PID:     %d\n", line->cl_Pid);

	printlogf(LOG_DEBUG, "  Mins:    ");
	for (i = 0; i < 60; ++i)
		printlogf(LOG_DEBUG, "%d", line->cl_Mins[i]);

	printlogf(LOG_DEBUG, "\n  Hrs:     ");
	for (i = 0; i < 24; ++i)
		printlogf(LOG_DEBUG, "%d", line->cl_Hrs[i]);

	printlogf(LOG_DEBUG, "\n  Days:    ");
	for (i = 0; i < 32; ++i)
		printlogf(LOG_DEBUG, "%d", line->cl_Days[i]);

	printlogf(LOG_DEBUG, "\n  Mons:    ");
	for (i = 0; i < 12; ++i)
		printlogf(LOG_DEBUG, "%d", line->cl_Mons[i]);

	printlogf(LOG_DEBUG, "\n  Dow:     ");
	for (i = 0; i < 7; ++i)
		printlogf(LOG_DEBUG, "%02x ", line->cl_Dow[i]);
	printlogf(LOG_DEBUG, "\n\n");
}

void
PrintFile(CronFile *file, char* loc, char* fname, int line)
{
	CronFile *f;
	CronLine *l;

	printlogf(LOG_DEBUG, "%s %s:%d\n", loc, fname, line);

	if (!file)
		return;

	f = file;
	while (f) {

		if (strncmp(file->cf_UserName, "root", 4)) {
			printlogf(LOG_DEBUG, "FILE %s/%s USER %s\n=============================\n",
					file->cf_DPath,
					file->cf_FileName,
					file->cf_UserName);
			l = f->cf_LineBase;

			while (l) {
				PrintLine(l);
				l = l->cl_Next;
			}
		}
		f = f->cf_Next;
	}

}
