
/*
 * DATABASE.C
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2011 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype void CheckUpdates(const char *dpath, STRING user_override, time_t t1, time_t t2);
Prototype void SynchronizeDir(const char *dpath, STRING user_override, int initial_scan);
Prototype void ReadTimestamps(STRING user);
Prototype int TestJobs(time_t t1, time_t t2);
Prototype int TestStartupJobs(void);
Prototype int ArmJob(CronFile *file, CronLine *line, time_t t1, time_t t2);
Prototype void RunJobs(void);
Prototype int CheckJobs(void);

static void SynchronizeFile(const char *dpath, const char *fileName, const char *userName);
static void DeleteFile(CronFile_p *pfile) /*@requires notnull *pfile@*/;
static /*@null@*/ char *ParseInterval(time_t *interval, /*@returned@*/ char *ptr);
static /*@null@*/ char *ParseField(char *user, short *ary, int modvalue, int off, int onvalue, /*@null@*/ const char **names, /*@null@*/ /*@returned@*/ char *ptr);
static void FixDayDow(CronLine *line);

static /*@null@*/ /*@owned@*/ CronFile *FileBase = NULL;

static /*@observer@*/ STRING DowAry[] = {
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

static /*@observer@*/ STRING MonAry[] = {
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

static /*@observer@*/ STRING FreqAry[] = {
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
 * Check the cron.update file in the specified directory.  If user_override
 * is NULL then the files in the directory belong to the user whose name is
 * the file, otherwise they belong to the user_override user.
 */
void
CheckUpdates(const char *dpath, STRING user_override, time_t t1, time_t t2)
{
	FILE *fi;
	char buf[SMALL_BUF];
	char *fname, *ptok, *job;
	char *path;

	path = stringdupmany(dpath, "/", CRONUPDATE, (char *)NULL);
	if ((fi = fopen(path, "r")) != NULL) {
		(void)remove(path);
		logger(LOG_INFO, "reading %s/%s\n", dpath, CRONUPDATE);
		while (fgets(buf, sizeof(buf), fi) != NULL) {
			/*
			 * if buf has only sep chars, return NULL and point ptok at buf's terminating 0
			 * else return pointer to first non-sep of buf and
			 * 		if there's a following sep, overwrite it to 0 and point ptok to next char
			 * 		else point ptok at buf's terminating 0
			 */
			/*@-unrecog@*/
			fname = strtok_r(buf, " \t\n", &ptok);
			/*@=unrecog@*/
			/* FIXME splint thinks ptok is still undefined */

			if (user_override)
				SynchronizeFile(dpath, fname, user_override);
			else if (!getpwnam(fname))
				logger(LOG_WARNING, "ignoring %s/%s: no such user\n", dpath, fname);
			else
				/*@-usedef@*/
				if (*ptok == '\0' || *ptok == '\n')
				/*@=usedef@*/
			{
				SynchronizeFile(dpath, fname, fname);
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
					logger(LOG_WARNING, "prodding user %s failed: no crontab\n", fname);
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
							(void)ArmJob(file, line, t1, force);
						else {
							logger(LOG_WARNING, "prodding user %s failed: unknown job %s\n", fname, job);
							/* we can continue parsing this line, we just don't install any CronWaiter for the requested job */
						}
					}
				}
			}
		}
		(void)fclose(fi);
	}
	free(path);
}

void
SynchronizeDir(const char *dpath, STRING user_override, int initial_scan)
{
	CronFile_p *pfile;
	CronFile *file;
	struct dirent *den;
	DIR *dir;
	char *path;

	if (DebugOpt)
		logger(LOG_DEBUG, "Synchronizing %s\n", dpath);

	/*
	 * Delete all database CronFiles for this directory.  DeleteFile() will
	 * free *pfile and relink the *pfile pointer, or in the alternative will
	 * mark it as deleted.
	 */
	pfile = &FileBase;
	while ((file = *pfile) != NULL) {
		if (!file->cf_Deleted && strcmp(file->cf_DPath, dpath) == 0) {
			DeleteFile(pfile);
		} else {
			pfile = &file->cf_Next;
		}
	}

	/*
	 * Since we are resynchronizing the entire directory, remove the
	 * the CRONUPDATE file.
	 */
	path = stringdupmany(dpath, "/", CRONUPDATE, (char *)NULL);
	(void)remove(path);
	free(path);

	/*
	 * Scan the specified directory
	 */
	if ((dir = opendir(dpath)) != NULL) {
		while ((den = readdir(dir)) != NULL) {
			assert(den->d_name != NULL);
			if (strchr(den->d_name, '.') != NULL)
				continue;
			if (strcmp(den->d_name, CRONUPDATE) == 0)
				continue;
			if (user_override) {
				SynchronizeFile(dpath, den->d_name, user_override);
			} else if (getpwnam(den->d_name)) {
				SynchronizeFile(dpath, den->d_name, den->d_name);
			} else {
				logger(LOG_WARNING, "ignoring %s/%s: no such user\n",
						dpath, den->d_name);
			}
		}
		(void)closedir(dir);
	} else {
		if (initial_scan)
			logger(LOG_ERR, "failed to scan directory %s\n", dpath);
			/* softerror, do not exit the program */
	}
}


void
ReadTimestamps(STRING user)
{
	CronFile *file;
	CronLine *line;
	FILE *fi;
	char buf[SMALL_BUF];
	char *ptr;
	/*@-fullinitblock@*/
	struct tm tm = {0};
	/*@=fullinitblock@*/
	time_t sec, freq;

	file = FileBase;
	while (file != NULL) {
		if (!file->cf_Deleted && (!user || strcmp(user, file->cf_UserName) == 0)) {
			line = file->cf_LineBase;
			while (line != NULL) {
				if (line->cl_Timestamp) {
					if ((fi = fopen(line->cl_Timestamp, "r")) != NULL) {
						if (fgets(buf, sizeof(buf), fi) != NULL) {
							int fake = 0;
							ptr = buf;
							if (strcmp(buf, "after ") == 0) {
								fake = 1;
								ptr += 6;
							}
							sec = (time_t)-1;
							/*@-unrecog@*/
							ptr = strptime(ptr, CRONSTAMP_FMT, &tm);
							/*@=unrecog@*/
							if (ptr && (*ptr == '\0' || *ptr == '\n'))
								/* strptime uses current seconds when seconds not specified? anyway, we don't get round minutes */
								tm.tm_sec = 0;
								sec = mktime(&tm);
							if (sec == (time_t)-1) {
								logger(LOG_ERR, "failed parsing timestamp for user %s job %s\n", file->cf_UserName, line->cl_JobName);
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
						(void)fclose(fi);
					} else {
						int succeeded = 0;
						logger(LOG_NOTICE, "no timestamp found for user %s job %s\n", file->cf_UserName, line->cl_JobName);
						/* write a fake timestamp file so our initial NotUntil doesn't keep being reset every hour when crond does a SynchronizeDir */
						if ((fi = fopen(line->cl_Timestamp, "w")) != NULL) {
							if (strftime(buf, sizeof(buf), CRONSTAMP_FMT, localtime(&line->cl_NotUntil)))
								if (fputs("after ", fi) >= 0)
									if (fputs(buf,fi) >= 0)
										succeeded = 1;
							(void)fclose(fi);
						}
						if (!succeeded)
							logger(LOG_WARNING, "failed writing timestamp to %s for user %s %s\n", line->cl_Timestamp, file->cf_UserName, line->cl_Description);
					}
				}
				line = line->cl_Next;
			}
		}
		file = file->cf_Next;
	}
}

static CronFile *
NewCronFile(const char *userName, const char *fileName, const char *dpath)
{
	CronFile *file = (CronFile *)xmalloc(sizeof(CronFile));
	assert(file!=NULL);
	file->cf_Next = NULL;
	file->cf_LineBase = NULL;
	file->cf_DPath = stringdup(dpath, PATH_MAX);
	file->cf_FileName = stringdup(fileName, NAME_MAX);
	file->cf_UserName = stringdup(userName, LOGIN_NAME_MAX);
	file->cf_Ready = FALSE;
	file->cf_Running = FALSE;
	file->cf_Deleted = FALSE;
	return file;
}

static void
ZeroCronLine(CronLine /*@out@*/ *line) /*@modifies *line@*/ /*@ensures isnull line->cl_Next,line->cl_Timestamp,line->cl_Waiters,line->cl_Notifs@*/
{
	memset(line, 0, sizeof(CronLine));
	/*@-mustfreeonly@*/
	line->cl_Next = NULL;
	/*
	//line->cl_Shell = shellCmd;
	//line->cl_Description = desc;
	//line->cl_JobName = NULL;
	*/
	line->cl_Timestamp = NULL;
	line->cl_Waiters = NULL;
	line->cl_Notifs = NULL;
	/*@=mustfreeonly@*/
}

void
SynchronizeFile(const char *dpath, const char *fileName, const char *userName)
{
	CronFile_p *pfile;
	CronFile *file;
	int maxEntries;
	int maxLines;
	char buf[LINE_BUF]; /* max length for crontab lines */
	char *path;
	FILE *fi;

	/*@-boundsread@*/
	assert(DowAry[0]!=NULL);
	assert(MonAry[0]!=NULL);
	assert(FreqAry[0]!=NULL);
	/*@=boundsread@*/

	/*
	 * Limit entries
	 */
	if (strcmp(userName, "root") == 0)
		maxEntries = 65535;
	else
		maxEntries = LINES_MAX;
	maxLines = maxEntries * 10;

	/*
	 * Delete any existing copy of this CronFile
	 */
	pfile = &FileBase;
	while ((file = *pfile) != NULL) {
		if (!file->cf_Deleted && strcmp(file->cf_DPath, dpath) == 0 &&
				strcmp(file->cf_FileName, fileName) == 0
		   ) {
			DeleteFile(pfile);
		} else {
			pfile = &file->cf_Next;
		}
	}

	path = stringdupmany(dpath, "/", fileName, (char *)NULL);
	if ((fi = fopen(path, "r")) != NULL) {
		struct stat sbuf;

		if (fstat(fileno(fi), &sbuf) == 0 && sbuf.st_uid == DaemonUid) {
			CronLine_p *pline;
			time_t tnow = time(NULL);
			tnow -= tnow % 60;

			file = NewCronFile(userName, fileName, dpath);
			pline = &file->cf_LineBase;

			/* fgets reads at most size-1 chars until \n or EOF, then adds a\0; \n if present is stored in buf */
			while (fgets(buf, sizeof(buf), fi) != NULL && --maxLines) {
				/*@owned@*/ CronLine line;
				char *ptr = buf;
				size_t len;

				while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
					++ptr;

				len = strlen(ptr);
				if (len && ptr[len-1] == '\n')
					ptr[--len] = '\0';

				if (*ptr == '\0' || *ptr == '#')
					continue;

				if (--maxEntries == 0)
					break;

				ZeroCronLine(&line);

				if (DebugOpt)
					logger(LOG_DEBUG, "User %s Entry %s\n", userName, buf);

				if (*ptr == '@') {
					/*
					 * parse @hourly, etc
					 */
					int	j;
					line.cl_Delay = -1;
					ptr += 1;
					for (j = 0; FreqAry[j]; ++j) {
						if (strcmp(ptr, FreqAry[j]) == 0) {
							break;
						}
					}
					if (FreqAry[j]) {
						ptr += strlen(FreqAry[j]);
						switch(j) {
							case 0:
								line.cl_Freq = FREQ_NOAUTO;
								line.cl_Delay = 0;
								break;
							case 1:
								line.cl_Freq = FREQ_REBOOT;
								line.cl_Delay = 0;
								break;
							case 2:
								line.cl_Freq = HOURLY_FREQ;
								break;
							case 3:
								line.cl_Freq = DAILY_FREQ;
								break;
							case 4:
								line.cl_Freq = WEEKLY_FREQ;
								break;
							case 5:
								line.cl_Freq = MONTHLY_FREQ;
								break;
							case 6:
								line.cl_Freq = YEARLY_FREQ;
								break;
							/* else line.cl_Freq will remain 0 */
						}
					}

					if (!line.cl_Freq || (*ptr != ' ' && *ptr != '\t')) {
						logger(LOG_WARNING, "failed parsing crontab for user %s: %s\n", userName, buf);
						continue;
					}

					if (line.cl_Delay < 0) {
						/*
						 * delays on @daily, @hourly, etc are 1/20 of the frequency
						 * so they don't all start at once
						 * this also affects how they behave when the job returns EAGAIN
						 */
						line.cl_Delay = line.cl_Freq / 20;
						line.cl_Delay -= line.cl_Delay % 60;
						if (line.cl_Delay == 0)
							line.cl_Delay = 60;
						/* all minutes are permitted */
						for (j=0; j<60; ++j)
							line.cl_Mins[j] = (short)1;
						for (j=0; j<24; ++j)
							line.cl_Hrs[j] = (short)1;
						for (j=1; j<32; ++j)
							/* days are numbered 1..31 */
							line.cl_Days[j] = (short)1;
						for (j=0; j<12; ++j)
							line.cl_Mons[j] = (short)1;
					}

					while (*ptr == ' ' || *ptr == '\t')
						++ptr;

				} else {
					/*
					 * parse date ranges
					 */

					ptr = ParseField(file->cf_UserName, line.cl_Mins, 60, 0, 1, NULL, ptr);
					ptr = ParseField(file->cf_UserName, line.cl_Hrs,  24, 0, 1, NULL, ptr);
					ptr = ParseField(file->cf_UserName, line.cl_Days, 32, 0, 1, NULL, ptr);
					ptr = ParseField(file->cf_UserName, line.cl_Mons, 12, -1, 1, MonAry, ptr);
					ptr = ParseField(file->cf_UserName, line.cl_Dow, 7, 0, 31, DowAry, ptr);
					/*
					 * check failure
					 */
					if (ptr == NULL)
						continue;

					/*
					 * fix days and dow - if one is not * and the other
					 * is *, the other is set to 0, and vise-versa
					 */

					/*@-compmempass@*/
					FixDayDow(&line);
					/*@=compmempass@*/
				}

				/* check for ID=... and AFTER=... and FREQ=... */
				do {
					if (strcmp(ptr, ID_TAG) == 0) {
						if (line.cl_JobName) {
							/* only assign ID_TAG once */
							logger(LOG_WARNING, "failed parsing crontab for user %s: repeated %s\n", userName, ptr);
							ptr = NULL;
						} else {
							ptr += strlen(ID_TAG);
							/*
							 * name = strsep(&ptr, seps):
							 * return name = ptr, and if ptr contains sep chars, overwrite first with 0 and point ptr to next char
							 *                    else set ptr=NULL
							 */
							line.cl_Description = stringdupmany("job ",
									/*@-unrecog@*/
								   strsep(&ptr, " \t"),
									/*@=unrecog@*/
								   (char *)NULL
							);
							line.cl_JobName = line.cl_Description + 4;
							if (!ptr)
								logger(LOG_WARNING, "failed parsing crontab for user %s: no command after %s%s\n", userName, ID_TAG, line.cl_JobName);
						}
					} else if (strcmp(ptr, FREQ_TAG) == 0) {
						if (line.cl_Freq) {
							/* only assign FREQ_TAG once */
							logger(LOG_WARNING, "failed parsing crontab for user %s: repeated %s\n", userName, ptr);
							ptr = NULL;
						} else {
							char *base = ptr;
							ptr += strlen(FREQ_TAG);
							ptr = ParseInterval(&line.cl_Freq, ptr);
							if (ptr && *ptr == '/')
								ptr = ParseInterval(&line.cl_Delay, ++ptr);
							else
								line.cl_Delay = line.cl_Freq;
							if (!ptr) {
								logger(LOG_WARNING, "failed parsing crontab for user %s: %s\n", userName, base);
							} else if (*ptr != ' ' && *ptr != '\t') {
								logger(LOG_WARNING, "failed parsing crontab for user %s: no command after %s\n", userName, base);
								ptr = NULL;
							}
						}
					} else if (strcmp(ptr, WAIT_TAG) == 0) {
						if (line.cl_Waiters) {
							/* only assign WAIT_TAG once */
							logger(LOG_WARNING, "failed parsing crontab for user %s: repeated %s\n", userName, ptr);
							ptr = NULL;
						} else {
							bool more = TRUE;
							char *name;
							ptr += strlen(WAIT_TAG);
							do {
								CronLine_p *pjob;
								CronLine *job;
								if (strcspn(ptr,",") < strcspn(ptr," \t"))
									name = strsep(&ptr, ",");
								else {
									more = FALSE;
									name = strsep(&ptr, " \t");
								}
								if (!ptr || *ptr == '\0') {
									/* unexpectedly this was the last token in buf; so abort */
									logger(LOG_WARNING, "failed parsing crontab for user %s: no command after %s%s\n", userName, WAIT_TAG, name);
									ptr = NULL;
								} else {
									time_t waitfor = 0;
									char *w, *wsave;
									if ((w = strchr(name, '/')) != NULL) {
										wsave = w++;
										w = ParseInterval(&waitfor, w);
										if (!w || *w != '\0') {
											logger(LOG_WARNING, "failed parsing crontab for user %s: %s%s\n", userName, WAIT_TAG, name);
											ptr = NULL;
										} else
											/* truncate name */
											*wsave = '\0';
									}
									if (ptr) {
										/* look for a matching CronLine */
										pjob = &file->cf_LineBase;
										while ((job = *pjob) != NULL) {
											if (job->cl_JobName && strcmp(job->cl_JobName, name) == 0) {
												CronWaiter *waiter = xmalloc(sizeof(CronWaiter));
												CronNotifier *notif = xmalloc(sizeof(CronNotifier));
												assert(waiter!=NULL && notif != NULL);
												waiter->cw_Flag = -1;
												waiter->cw_MaxWait = waitfor;
												waiter->cw_NotifLine = job;
												waiter->cw_Notifier = notif;
												waiter->cw_Next = line.cl_Waiters;	/* add to head of line.cl_Waiters */
												line.cl_Waiters = waiter;
												notif->cn_Waiter = waiter;
												notif->cn_Next = job->cl_Notifs;	/* add to head of job->cl_Notifs */
												job->cl_Notifs = notif;
												break;
											} else
												pjob = &job->cl_Next;
										}
										if (!job) {
											logger(LOG_WARNING, "failed parsing crontab for user %s: unknown job %s\n", userName, name);
											/* we can continue parsing this line, we just don't install any CronWaiter for the requested job */
										}
									}
								}
							} while (ptr && more);
						}
					} else
						break;
					if (!ptr)
						break;
					while (*ptr == ' ' || *ptr == '\t')
						++ptr;
				} while (!line.cl_JobName || !line.cl_Waiters || !line.cl_Freq);

				if (line.cl_JobName && (!ptr || *line.cl_JobName == '\0')) {
					/* we're aborting, or ID= was empty */
					free(line.cl_Description);
					line.cl_Description = NULL;
					line.cl_JobName = NULL;
				}
				if (ptr && line.cl_Delay > 0 && !line.cl_JobName) {
					logger(LOG_WARNING, "failed parsing crontab for user %s: writing timestamp requires job %s to be named\n", userName, ptr);
					ptr = NULL;
				}
				if (!ptr) {
					/* couldn't parse so we abort; free any cl_Waiters */
					if (line.cl_Waiters) {
						CronWaiter **pwaiters, *waiters;
						pwaiters = &line.cl_Waiters;
						while ((waiters = *pwaiters) != NULL) {
							*pwaiters = waiters->cw_Next;
							/* leave the Notifier allocated but disabled */
							waiters->cw_Notifier->cn_Waiter = NULL;
							free(waiters);
						}
					}
					continue;
				}
				/* now we've added any ID=... or AFTER=... */

				/*
				 * copy command string
				 */
				line.cl_Shell = stringdup(ptr, LINE_BUF);

				if (line.cl_Delay > 0) {
					line.cl_Timestamp = stringdupmany(TSDir, "/", userName, ".", line.cl_JobName, (char *)NULL);
					line.cl_NotUntil = tnow + line.cl_Delay;
				}

				if (line.cl_JobName) {
					if (DebugOpt)
						/*@-ownedtrans@*/
						logger(LOG_DEBUG, "    Command %s Job %s\n", line.cl_Shell, line.cl_JobName);
						/*@=ownedtrans@*/
				} else {
					/* when cl_JobName is NULL, we point cl_Description to cl_Shell */
					line.cl_Description = line.cl_Shell;
					if (DebugOpt)
						logger(LOG_DEBUG, "    Command %s\n", line.cl_Shell);
				}

				/*@-mustfreeonly@*/
				*pline = xmalloc(sizeof(CronLine));
				/*@=mustfreeonly@*/
				assert(*pline!=NULL);
				/* copy working CronLine to newly allocated one */
				/*@-nullret@*/
				**pline = line;
				/*@=nullret@*/

				pline = &((*pline)->cl_Next);
			}

			/*@-mustfreeonly@*/
			*pline = NULL;
			/*@=mustfreeonly@*/

			file->cf_Next = FileBase;
			FileBase = file;

			if (maxLines == 0 || maxEntries == 0)
				logger(LOG_WARNING, "maximum number of lines reached for user %s\n", userName);
		}
		(void)fclose(fi);
	}
	free(path);

	/*@-compmempass@*/
	return;
	/*@=compmempass@*/
}

char *
ParseInterval(time_t *interval, char *ptr)
{
	int n = 0;
	if (ptr && *ptr >= '0' && *ptr <= '9' && (n = (int)strtol(ptr, &ptr, 10)) > 0)
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
		assert(ptr!=NULL);
		*interval = n;
		return ptr+1;
	} else
		return NULL;
}

char *
ParseField(char *user, short *ary, int modvalue, int off, int onvalue, const char **names, char *ptr)
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
				n1 = (int)strtol(ptr, &ptr, 10) + off;
			else
				n2 = (int)strtol(ptr, &ptr, 10) + off;
			skip = 1;
		} else if (names) {
			int i;

			for (i = 0; names[i]; ++i) {
				if (strcmp(ptr, names[i]) == 0) {
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
			logger(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
			return NULL;
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

		n2 = n2 % modvalue;

		if (*ptr == '/')
			skip = (int)strtol(ptr + 1, &ptr, 10);

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
					ary[n1] = (short)onvalue;
					s0 = skip;
				}
			} while (n1 != n2 && --failsafe);

			if (failsafe == 0) {
				logger(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
				return NULL;
			}
		}
		if (*ptr != ',')
			break;
		++ptr;
		n1 = -1;
		n2 = -1;
	}

	if (*ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
		logger(LOG_WARNING, "failed parsing crontab for user %s: %s\n", user, base);
		return NULL;
	}

	while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')
		++ptr;

	if (DebugOpt) {
		int i;

		for (i = 0; i < modvalue; ++i)
			if (modvalue == 7)
				logger(LOG_DEBUG, "%2x ", ary[i]);
			else
				logger(LOG_DEBUG, "%d", ary[i]);
		logger(LOG_DEBUG, "\n");
	}

	return ptr;
}

void
FixDayDow(CronLine *line)
{
	unsigned short i,j;
	bool weekUsed = FALSE;
	bool daysUsed = FALSE;

	assert(line!=NULL);
	for (i = 0; i < arysize(line->cl_Dow); ++i) {
		if (line->cl_Dow[i] == 0) {
			weekUsed = TRUE;
			break;
		}
	}
	for (i = 0; i < arysize(line->cl_Days); ++i) {
		if (line->cl_Days[i] == 0) {
			if (weekUsed) {
				if (!daysUsed) {
					daysUsed = TRUE;
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
				daysUsed = TRUE;
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
DeleteFile(CronFile_p *pfile)
{
	CronFile *file = *pfile;
	CronLine_p *pline = &file->cf_LineBase;
	CronLine *line;
	CronWaiter **pwaiters, *waiters;
	CronNotifier **pnotifs, *notifs;

	file->cf_Running = FALSE;
	file->cf_Deleted = TRUE;

	/*@-branchstate@*/
	while ((line = *pline) != NULL) {
		if (line->cl_Pid > 0) {
			file->cf_Running = TRUE;
			pline = &line->cl_Next;
		} else {
			/*@-dependenttrans@*/
			*pline = line->cl_Next;
			/*@=dependenttrans@*/
			free(line->cl_Shell);

			if (line->cl_JobName)
				/*
				 * this frees both cl_Description and cl_JobName
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

			/*@-compdestroy@*/
			free(line);
			/*@=compdestroy@*/
		}
	}
	/*@=branchstate@*/
	if (!file->cf_Running) {
		/*@-dependenttrans@*/
		*pfile = file->cf_Next;
		/*@=dependenttrans@*/
		free(file->cf_DPath);
		free(file->cf_FileName);
		free(file->cf_UserName);
		free(file);
	}
	/*@-usereleased@*/
	/*@-compdef@*/
	return;
	/*@=compdef@*/
	/*@=usereleased@*/
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

	for (file = FileBase; file!=NULL; file = file->cf_Next) {
		if (file->cf_Deleted)
			continue;
		for (line = file->cf_LineBase; line!=NULL; line = line->cl_Next) {
			struct CronWaiter *waiter;

			if (line->cl_Pid == -2) {
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
						logger(LOG_DEBUG, "cancelled waiting for user %s %s\n", file->cf_UserName, line->cl_Description);
					line->cl_Pid = 0;
				} else if (ready) {
					if (DebugOpt)
						logger(LOG_DEBUG, "finished waiting for user %s %s\n", file->cf_UserName, line->cl_Description);
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
			int n_wday = (tp->tm_mday - 1)%7 + 1;
			if (n_wday >= 4) {
				struct tm tnext = *tp;
				tnext.tm_mday += 7;
				if (mktime(&tnext) != (time_t)-1 && tnext.tm_mon != tp->tm_mon)
					n_wday |= 16;	/* last dow in month is always recognized as 5th */
			}

			for (file = FileBase; file!=NULL; file = file->cf_Next) {
				if (file->cf_Deleted)
					continue;
				for (line = file->cf_LineBase; line!=NULL; line = line->cl_Next) {
					if ((line->cl_Pid == -2 || line->cl_Pid == 0) && (line->cl_Freq == 0 || (line->cl_Freq > 0 && t2 >= line->cl_NotUntil))) {
						/* (re)schedule job? */
						if (line->cl_Mins[tp->tm_min] &&
								line->cl_Hrs[tp->tm_hour] &&
								(line->cl_Days[tp->tm_mday] || (n_wday && line->cl_Dow[tp->tm_wday]) ) &&
								line->cl_Mons[tp->tm_mon]
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
	return nJobs;
}

/*
 * ArmJob: if t2 is (time_t)-1, we force-schedule the job without any waiting
 * else it will wait on any of its declared notifiers who will run <= t2 + cw_MaxWait
 */

int
ArmJob(CronFile *file, CronLine *line, time_t t1, time_t t2)
{
	struct CronWaiter *waiter;
	if (line->cl_Pid > 0) {
		logger(LOG_NOTICE, "process %d already running for user %s %s\n",
				line->cl_Pid,
				file->cf_UserName,
				line->cl_Description
			);
	} else if (t2 == -1 && line->cl_Pid != -1) {
		line->cl_Pid = -1;
		file->cf_Ready = TRUE;
		return 1;
	} else if (line->cl_Pid == 0) {
		/* arming a waiting job (cl_Pid == -2) without forcing has no effect */
		line->cl_Pid = -1;
		/* if we have any waiters, zero them and arm cl_Pid=-2 */
		waiter = line->cl_Waiters;
		while (waiter != NULL) {
			/* check if notifier will run <= t2 + cw_Max_Wait? */
			if (!waiter->cw_NotifLine)
				/* notifier deleted */
				waiter->cw_Flag = 0;
			else if (waiter->cw_NotifLine->cl_Pid != 0) {
				/* if notifier is armed, or waiting, or running, we wait for it */
				waiter->cw_Flag = -1;
				line->cl_Pid = -2;
			} else if (waiter->cw_NotifLine->cl_Freq < 0) {
				/* arm any @noauto or @reboot jobs we're waiting on */
				(void)ArmJob(file, waiter->cw_NotifLine, t1, t2);
				waiter->cw_Flag = -1;
				line->cl_Pid = -2;
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
							int n_wday = (tp->tm_mday - 1)%7 + 1;
							if (n_wday >= 4) {
								struct tm tnext = *tp;
								tnext.tm_mday += 7;
								if (mktime(&tnext) != (time_t)-1 && tnext.tm_mon != tp->tm_mon)
									n_wday |= 16;	/* last dow in month is always recognized as 5th */
							}
							if (line->cl_Mins[tp->tm_min] &&
									line->cl_Hrs[tp->tm_hour] &&
									(line->cl_Days[tp->tm_mday] || (n_wday && line->cl_Dow[tp->tm_wday]) ) &&
									line->cl_Mons[tp->tm_mon]
							   ) {
								/* notifier will run soon enough, we wait for it */
								waiter->cw_Flag = -1;
								line->cl_Pid = -2;
								break;
							}
						}
					}
				}
			}
			waiter = waiter->cw_Next;
		}
		if (line->cl_Pid == -1) {
			/* job is ready to run */
			file->cf_Ready = TRUE;
			if (DebugOpt)
				logger(LOG_DEBUG, "scheduled user %s %s\n",
						file->cf_UserName,
						line->cl_Description
					);
			return 1;
		} else if (DebugOpt)
			logger(LOG_DEBUG, "waiting for user %s %s\n",
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

	for (file = FileBase; file!=NULL; file = file->cf_Next) {
		if (DebugOpt)
			logger(LOG_DEBUG, "TestStartup for FILE %s/%s USER %s:\n",
				file->cf_DPath, file->cf_FileName, file->cf_UserName);
		for (line = file->cf_LineBase; line!=NULL; line = line->cl_Next) {
			struct CronWaiter *waiter;
			if (DebugOpt) {
				if (line->cl_JobName)
					logger(LOG_DEBUG, "    LINE %s JOB %s\n", line->cl_Shell, line->cl_JobName);
				else
					logger(LOG_DEBUG, "    LINE %s\n", line->cl_Shell);
			}

			if (line->cl_Freq == FREQ_REBOOT) {

				line->cl_Pid = -1;
				/* if we have any waiters, reset them and arm Pid = -2 */
				waiter = line->cl_Waiters;
				while (waiter != NULL) {
					waiter->cw_Flag = -1;
					line->cl_Pid = -2;
					/* we only arm @noauto jobs we're waiting on, not other @reboot jobs */
					if (waiter->cw_NotifLine && waiter->cw_NotifLine->cl_Freq == FREQ_NOAUTO)
						(void)ArmJob(file, waiter->cw_NotifLine, t1, t1+60);
					waiter = waiter->cw_Next;
				}
				if (line->cl_Pid == -1) {
					/* job is ready to run */
					file->cf_Ready = TRUE;
					++nJobs;
					if (DebugOpt)
						logger(LOG_DEBUG, "    scheduled %s\n", line->cl_Description);
				} else if (DebugOpt)
					logger(LOG_DEBUG, "    waiting for %s\n", line->cl_Description);

			}

		} /* for line */
	}
	return nJobs;
}

void
RunJobs(void)
{
	CronFile *file;
	CronLine *line;

	for (file = FileBase; file!=NULL; file = file->cf_Next) {
		if (file->cf_Ready) {
			file->cf_Ready = FALSE;

			for (line = file->cf_LineBase; line!=NULL; line = line->cl_Next) {
				if (line->cl_Pid == -1) {

					RunJob(file, line);

					logger(LOG_INFO, "FILE %s/%s USER %s PID %3d %s\n",
							file->cf_DPath,
							file->cf_FileName,
							file->cf_UserName,
							line->cl_Pid,
							line->cl_Description
						);
					if (line->cl_Pid < 0)
						/* QUESTION how could this happen? RunJob will leave cl_Pid set to 0 or the actual pid */
						file->cf_Ready = TRUE;
					else if (line->cl_Pid > 0)
						file->cf_Running = TRUE;
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

	for (file = FileBase; file!=NULL; file = file->cf_Next) {
		if (file->cf_Running) {
			file->cf_Running = FALSE;

			for (line = file->cf_LineBase; line!=NULL; line = line->cl_Next) {
				if (line->cl_Pid > 0) {
					int status;
					pid_t r = waitpid(line->cl_Pid, &status, WNOHANG);

					/* waitpid returns -1 for error, 0 if cl_Pid still running, cl_Pid if it's dead */

					if (r < 0 || r == line->cl_Pid) {
						if (r > 0 && WIFEXITED(status))
							status = WEXITSTATUS(status);
						else
							status = 1;
						EndJob(file, line, status);

					} else if (r == 0) {
						file->cf_Running = TRUE;
					}
				}
			}
		}
		nStillRunning += (int)file->cf_Running;
	}
	return nStillRunning;
}

