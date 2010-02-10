
/*
 * CRONTAB.C
 *
 * crontab [-u user] [-c dir] [-l|-e|-d|file|-]
 * usually run as setuid root
 * -u and -c options only work if getuid() == geteuid()
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009-2010 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

/* needed by chuser.c */
Prototype void printlogf(/*@unused@*/ int level, const char *fmt, ...);

static void Usage(void);
static int GetReplaceStream(const char *user, const char *file);
static void EditFile(const char *user, const char *file);

const char *CDir = CRONTABS;
static uid_t   UserId;


int
main(int ac, char **av)
{
	enum { NONE, EDIT, LIST, REPLACE, DELETE } option = NONE;
	struct passwd *pas;
	char *repFile = NULL;
	int repFd = 0;
	int i;
	char caller[SMALL_BUFFER];		/* user that ran program */

	UserId = getuid();
	if ((pas = getpwuid(UserId)) == NULL) {
		perror("getpwuid");
		exit(EXIT_FAILURE);
	}

	/* FIXME */
	/* [v]snprintf write at most size including \0; they'll null-terminate, even when they truncate */
	/* return value >= size means result was truncated */
	if (snprintf(caller, sizeof(caller), "%s", pas->pw_name) >= sizeof(caller)) {
		printlogf(0, "username '%s' too long\n", caller);
		exit(EXIT_FAILURE);
	}
	/*
	char *caller;		// user that ran program
	if (!(caller = strdup(pas->pw_name))) {
		errno = ENOMEM;
		perror("caller");
		exit(EXIT_FAILURE);
	}
	*/

	opterr = 0;
	while ((i=getopt(ac,av,"ledu:c:")) != -1) {
		switch(i) {
			case 'l':
				if (option != NONE)
					Usage();
				else
					option = LIST;
				break;
			case 'e':
				if (option != NONE)
					Usage();
				else
					option = EDIT;
				break;
			case 'd':
				if (option != NONE)
					Usage();
				else
					option = DELETE;
				break;
			case 'u':
				/* getopt guarantees optarg != 0 here */
				if (*optarg != '\0' && getuid() == geteuid()) {
					if ((pas = getpwnam(optarg))) {
						UserId = pas->pw_uid;
						/* paranoia */
						if ((pas = getpwuid(UserId)) == NULL) {
							perror("getpwuid");
							exit(EXIT_FAILURE);
						}
					} else {
						printlogf(0, "failed to get uid for %s\n", optarg);
						exit(EXIT_FAILURE);
					}
				} else {
					printlogf(0, "-u option: superuser only\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'c':
				/* getopt guarantees optarg != 0 here */
				if (*optarg != '\0' && getuid() == geteuid()) {
					CDir = optarg;
				} else {
					printlogf(0, "-c option: superuser only\n");
					exit(EXIT_FAILURE);
				}
				break;
			default:
				/* unrecognized -X */
				option = NONE;
		}
	}

	if (option == NONE && optind == ac - 1) {
		if (av[optind][0] != '-') {
			option = REPLACE;
			repFile = av[optind];
			optind++;
		} else if (av[optind][1] == '\0') {
			option = REPLACE;
			optind++;
		}
	}
	if (option == NONE || optind != ac) {
		Usage();
	}

	/*
	 * If there is a replacement file, obtain a secure descriptor to it.
	 */

	if (repFile) {
		repFd = GetReplaceStream(caller, repFile);
		if (repFd < 0) {
			printlogf(0, "failed reading replacement file %s\n", repFile);
			exit(EXIT_FAILURE);
		}
	}

	/*
	 * Change directory to our crontab directory
	 */

	if (chdir(CDir) < 0) {
		printlogf(0, "chdir to %s failed: %s\n", CDir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*
	 * Handle options as appropriate
	 */

	switch(option) {
		case LIST:
			{
				FILE *fi;
				char buf[RW_BUFFER];

				if ((fi = fopen(pas->pw_name, "r"))) {
					while (fgets(buf, sizeof(buf), fi) != NULL)
						(void)fputs(buf, stdout);
					(void)fclose(fi);
				} else {
					printlogf(0, "no crontab for %s\n", pas->pw_name);
					/* no error code */
				}
			}
			break;
		case EDIT:
			{
				FILE *fi;
				int fd;
				size_t n;
				char tmp[] = TMPDIR "/crontab.XXXXXX";
				char buf[RW_BUFFER];

				/*
				 * Create temp file with perm 0600 and O_EXCL flag, ensuring that this call creates the file
				 * Read from fi for "$CDir/$USER", write to fd for temp file
				 * EditFile changes user if necessary, and runs editor on temp file
				 * Then we delete the temp file, keeping its fd as repFd
				 */
				if ((fd = mkstemp(tmp)) >= 0) {
					(void)chown(tmp, getuid(), getgid());
					if ((fi = fopen(pas->pw_name, "r"))) {
						while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
							(void)write(fd, buf, n);
					}
					EditFile(caller, tmp);
					(void)remove(tmp);
					(void)lseek(fd, 0L, 0);
					repFd = fd;
				} else {
					printlogf(0, "failed creating %s: %s\n", tmp, strerror(errno));
					exit(EXIT_FAILURE);
				}

			}
			option = REPLACE;
			/*@fallthrough@*/
		case REPLACE:
			{
				char buf[RW_BUFFER];
				char path[PATH_MAX];
				size_t k;
				ssize_t n;
				int fd;
				int saverr = 0;

				/*
				 * Read from repFd, write to fd for "$CDir/$USER.new"
				 */
				if ((k = stringcpy(path, pas->pw_name, sizeof(path)-4)) >= sizeof(path)-4) {
					saverr = ENAMETOOLONG;
				} else {
					(void)strcat(path + k, ".new");
					if ((fd = open(path, O_CREAT|O_TRUNC|O_EXCL|O_APPEND|O_WRONLY, 0600)) >= 0) {
						while ((n = read(repFd, buf, sizeof(buf))) > 0) {
							(void)write(fd, buf, (size_t)n);
						}
						(void)close(fd);
						(void)rename(path, pas->pw_name);
					} else {
						saverr = errno;
					}
				}
				if (saverr) {
					printlogf(0, "failed creating %s/%s: %s\n",
							CDir,
							pas->pw_name,
							strerror(saverr)
						   );
				}
				(void)close(repFd);
			}
			break;
		case DELETE:
			(void)remove(pas->pw_name);
			break;
		case NONE:
		default:
			break;
	}

	/*
	 *  Bump notification file.  Handle window where crond picks file up
	 *  before we can write our entry out.
	 */

	if (option == REPLACE || option == DELETE) {
		FILE *fo;
		struct stat st;

		while ((fo = fopen(CRONUPDATE, "a"))) {
			fprintf(fo, "%s\n", pas->pw_name);
			(void)fflush(fo);
			if (fstat(fileno(fo), &st) != 0 || st.st_nlink != 0) {
				(void)fclose(fo);
				break;
			}
			(void)fclose(fo);
			/* loop */
		}
		if (fo == NULL) {
			printlogf(0, "failed appending to %s/%s\n", CDir, CRONUPDATE);
		}
	}
	exit(EXIT_SUCCESS);
	/* not reached */
}

void
printlogf(/*@unused@*/ int level, const char *fmt, ...)
{
	va_list va;
	/* char buf[LOG_BUFFER]; */

	va_start(va, fmt);
	/*
	(void)vsnprintf(buf, sizeof(buf), fmt, va);
	(void)write(2, buf, strlen(buf));
	*/
	(void)vfprintf(stderr, fmt, va);
	va_end(va);
}

void
Usage(void)
{
	/*
	 * parse error
	 */
	printf("crontab " VERSION "\n");
	printf("crontab file [-u user]  replace crontab from file\n");
	printf("crontab -  [-u user]    replace crontab from stdin\n");
	printf("crontab -l [-u user]    list crontab\n");
	printf("crontab -e [-u user]    edit crontab\n");
	printf("crontab -d [-u user]    delete crontab\n");
	printf("crontab -c dir <opts>   specify crontab directory\n");
	exit(EXIT_FAILURE);
}

int
GetReplaceStream(const char *user, const char *file)
{
	int filedes[2];
	pid_t pid;
	int fd;
	ssize_t n;
	char buf[RW_BUFFER];

	if (pipe(filedes) < 0) {
		perror("pipe");
		return -1;
	}
	if ((pid = fork()) < 0) {
		perror("fork");
		return -1;
	}
	if (pid > 0) {
		/*
		 * PARENT
		 * Read from pipe[0], return it (or -1 if it's empty)
		 */

		(void)close(filedes[1]);
		if (read(filedes[0], buf, 1) != 1) {
			(void)close(filedes[0]);
			filedes[0] = -1;
		}
		return filedes[0];
	}

	/*
	 * CHILD
	 * Read from fd for "$file", write to pipe[1]
	 */

	(void)close(filedes[0]);

	if (ChangeUser(user, NULL) < 0)
		exit(EXIT_SUCCESS);

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		printlogf(0, "failed opening %s: %s\n", file, strerror(errno));
		exit(EXIT_FAILURE);
	}
	buf[0] = '\0';
	(void)write(filedes[1], buf, 1);
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		(void)write(filedes[1], buf, (size_t)n);
	}
	exit(EXIT_SUCCESS);
}

void
EditFile(const char *user, const char *file)
{
	pid_t pid;

	if ((pid = fork()) == 0) {
		/*
		 * CHILD - change user and run editor on "$file"
		 */
		const char *ptr;
		char *visual;

		if (ChangeUser(user, TMPDIR) < 0)
			exit(EXIT_SUCCESS);
		if ((ptr = getenv("EDITOR")) == NULL)
			if ((ptr = getenv("VISUAL")) == NULL)
				ptr = PATH_VI;

		visual = stringcat(ptr, " ", file, (char *)NULL);
		(void)execl("/bin/sh", "/bin/sh", "-c", visual, NULL);

		printlogf(0, "exec /bin/sh -c '%s' failed\n", visual);
		exit(EXIT_FAILURE);
	}
	if (pid < 0) {
		/*
		 * PARENT - failure
		 */
		perror("fork");
		exit(EXIT_FAILURE);
	}
	(void)waitpid(pid, NULL, 0);
}

