
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
Prototype void logger(/*@unused@*/ int level, const char *fmt, ...);
Prototype const char progname[];

static void Usage(void);
static int GetReplaceStream(const char *user, const char *pathrep);
static void EditFile(const char *user, const char *pathtmp);

/*@observer@*/ const char progname[] = "crontab";
const char *CDir = CRONTABS;
static uid_t   UserId;


int
main(int ac, char **av)
{
	enum { NONE, EDIT, LIST, REPLACE, DELETE } option = NONE;
	struct passwd *pas;
	char *pathrep = NULL;
	int frep = 0;
	int i;
	char caller[LOGIN_NAME_MAX];		/* user that ran program */

	UserId = getuid();
	if ((pas = getpwuid(UserId)) == NULL)
		fatal("could not get passwd entry for uid %d (%s)", UserId, strerror(errno));

	/* FIXME
	 * ---------------------------------
	 * */
	/* [v]snprintf write at most size including \0; they'll null-terminate, even when they truncate */
	/* return value >= size means result was truncated */
	if (snprintf(caller, sizeof(caller), "%s", pas->pw_name) >= sizeof(caller)) {
		logger(0, "username '%s' too long\n", caller);
		exit(EXIT_FAILURE);
	}
	/*
	char *caller;		// user that ran program
	if (!(caller = strdup(pas->pw_name))) {
		errno = ENOMEM;
		perror("caller");
		exit(EXIT_FAILURE);
	}
	----------------------------------
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
				/* getopt guarantees optarg != NULL here */
				if (*optarg == '\0' || getuid() != geteuid())
					fatal("-u option for superuser only");
				else if ((pas = getpwnam(optarg))==NULL)
					fatal("unknown user %s", optarg);
				UserId = pas->pw_uid;
				/* paranoia */
				if ((pas = getpwuid(UserId)) == NULL)
					fatal("could not get passwd entry for uid %d (%s)", UserId, strerror(errno));
				break;
			case 'c':
				/* getopt guarantees optarg != NULL here */
				if (*optarg == '\0' || getuid() != geteuid())
					fatal("-c option for superuser only");
				CDir = optarg;
				break;
			default:
				/* unrecognized -X */
				option = NONE;
		}
	}

	if (option == NONE && optind == ac - 1) {
		if (av[optind][0] != '-') {
			option = REPLACE;
			pathrep = av[optind];
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

	if (pathrep)
		frep = GetReplaceStream(caller, pathrep);

	/*
	 * Change directory to our crontab directory
	 */

	if (chdir(CDir) < 0)
		fatal("could not chdir to %s (%s)", CDir, strerror(errno));

	/*
	 * Handle options as appropriate
	 */

	switch(option) {
		case LIST:
			{
				int fin;
				int saverr;
				char buf[PIPE_BUF];
				ssize_t n;

				if ((fin = open(pas->pw_name, O_RDONLY)) < 0)
					fatal("no crontab for %s", pas->pw_name);

				while ((n = read(fin, buf, sizeof(buf))) > 0) {
					if (write(1, buf, (size_t)n) < n)
						/*@loopbreak@*/
						break;
				}
				saverr = errno;
				(void)close(fin);
				if (n != 0)
					fatal("%s", strerror(saverr));
			}
			break;
		case EDIT:
			{
				int fin, ftmp;
				int saverr;
				char buf[PIPE_BUF];
				ssize_t n;
				char pathtmp[] = TMPDIR "/crontab.XXXXXX";

				/*
				 * Create temp file with perm 0600 and O_EXCL flag, ensuring that this call creates the file
				 * Read from fin for "$CDir/$USER", write to ftmp for temp file
				 * EditFile changes user if necessary, and runs editor on temp file
				 * Then we delete the temp file, keeping its ftmp as frep
				 */
				if ((ftmp = mkstemp(pathtmp)) < 0)
					fatal("could not create %s (%s)", pathtmp, strerror(errno));

				if (chown(pathtmp, getuid(), getgid()))
					fatal("could not set ownership of %s", pathtmp);

				if ((fin = open(pas->pw_name, O_RDONLY)) >= 0) {
					while ((n = read(fin, buf, sizeof(buf))) > 0) {
						if (write(ftmp, buf, (size_t)n) < n)
							/*@loopbreak@*/
							break;
					}
					saverr = errno;
					(void)close(fin);
					if (n != 0)
						fatal("could not copy %s/%s to %s (%s)", CDir, pas->pw_name, pathtmp, strerror(saverr));
				}

				EditFile(caller, pathtmp);
				(void)remove(pathtmp);
				(void)lseek(ftmp, 0L, 0);
				frep = ftmp;

			}
			option = REPLACE;
			/*@fallthrough@*/
		case REPLACE:
			{
				int fnew;
				char buf[PIPE_BUF];
				char pathnew[NAME_MAX];
				ssize_t n;
				size_t k;
				int saverr = 0;

				/*
				 * Read from frep, write to fnew for "$CDir/$USER.new"
				 */

				k = stringcpy(pathnew, pas->pw_name, sizeof(pathnew));
				if (stringcat(pathnew, ".new", sizeof(pathnew), k) >= sizeof(pathnew)) {
					saverr = ENAMETOOLONG;
				} else if ((fnew = open(pathnew, O_CREAT|O_TRUNC|O_EXCL|O_APPEND|O_WRONLY, 0600)) < 0) {
					saverr = errno;
				} else {
					while ((n = read(frep, buf, sizeof(buf))) > 0) {
						if (write(fnew, buf, (size_t)n) < n) {
							saverr = errno;
							/*@loopbreak@*/
							break;
						}
					}
					(void)close(fnew);
					if (!saverr)
						(void)rename(pathnew, pas->pw_name);
				}
				(void)close(frep);
				if (saverr)
					fatal("could not write to %s/%s (%s)", CDir, pas->pw_name, strerror(saverr));
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
		int fup;
		struct stat st;
		size_t k = strlen(pas->pw_name);
		int j;

		for (j=0; j < 2 && (fup = open(CRONUPDATE, O_WRONLY | O_APPEND)) >= 0; j++) {
			if (write(fup, pas->pw_name, k) == k
				&& write(fup, "\n", 1) == 1
				&& fsync(fup) == 0
				&& fstat(fup, &st) == 0
				&& st.st_nlink != 0
				&& close(fup) == 0
			) break;
			(void)close(fup);
			/* loop */
		}
		if (j == 2 || fup < 0)
			fatal("could not append to %s/%s (%s)", CDir, CRONUPDATE, strerror(errno));
	}
	exit(EXIT_SUCCESS);
	/* not reached */
}

void
logger(/*@unused@*/ int level, const char *fmt, ...)
{
	va_list va;
	size_t k;
	bool eoln;

	k = strlen(fmt);
	eoln = (k > 0 && fmt[k - 1] == '\n');

	/* write "progname: " to stderr */
	if (fprintf(stderr, "%s: ", progname) > 0) {
		/* write formatted message, appending \n if necessary */
		va_start(va, fmt);
		if (vfprintf(stderr, fmt, va) >= 0 && !eoln)
			(void)fputc('\n', stderr);
		va_end(va);
	}

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
GetReplaceStream(const char *user, const char *pathrep)
{
	int filedes[2];
	pid_t pid;
	char buf[PIPE_BUF];
	int saverr = 0;

	if (pipe(filedes) < 0)
		fatal("could not create pipe");

	if ((pid = fork()) == 0) {
		/*
		 * CHILD, FORK OK
		 * Read from fin for "$pathrep", write to pipe[1]
		 */
		int fin;
		ssize_t n;

		(void)close(filedes[0]);

		(void)ChangeUser(user, NULL, "", "");

		n = 1;
		if ((fin = open(pathrep, O_RDONLY)) >= 0) {
			buf[0] = '\0';
			do {
				if (write(filedes[1], buf, (size_t)n) < n)
					break;
			} while ((n = read(fin, buf, sizeof(buf))) > 0);
			(void)close(fin);
		}

		if (n != 0)
			exit(EXIT_FAILURE);
		else
			exit(EXIT_SUCCESS);

	} else if (pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 */
		saverr = errno;
	} else {
		/*
		 * PARENT, FORK SUCCESS
		 * Read from pipe[0], return it (or -1 if it's empty)
		 */
		(void)close(filedes[1]);
		if (read(filedes[0], buf, 1) == 1)
			return filedes[0];

		saverr = errno;
		(void)close(filedes[0]);
	}
	fatal("could not read %s (%s)", pathrep, strerror(saverr));
	/*@-unreachable@*/
	return -1; /* to stop gcc warning */
	/*@=unreachable@*/
}

void
EditFile(const char *user, const char *pathtmp)
{
	pid_t pid;

	if ((pid = fork()) == 0) {
		/*
		 * CHILD - change user and run editor on "$pathtmp"
		 */
		const char *pathvi;
		const char *cmdvi;

		(void)ChangeUser(user, TMPDIR, "", "");

		if ((pathvi = getenv("EDITOR")) == NULL)
			if ((pathvi = getenv("VISUAL")) == NULL)
				pathvi = PATH_VI;

		cmdvi = stringdupmany(pathvi, " ", pathtmp, (char *)NULL);
		(void)execl("/bin/sh", "/bin/sh", "-c", cmdvi, NULL);

		fatal("could not exec '%s'", cmdvi);
	} else if (pid < 0) {
		/*
		 * PARENT, FORK FAILED
		 */
		fatal("could not edit crontab (%s)", strerror(errno));
	} else {
		/*
		 * PARENT, FORK SUCCESS
		 */
		(void)waitpid(pid, NULL, 0);
	}
}

