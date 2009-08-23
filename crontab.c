
/*
 * CRONTAB.C
 *
 * CRONTAB
 *
 * usually setuid root, -c option only works if getuid() == geteuid()
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * Copyright 2009 James Pryor <profjim@jimpryor.net>
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

const char *CDir = CRONTABS;
int   UserId;
/* not used in this program, but subs.c needs */
short LogLevel = LOG_NOTICE;
short ForegroundOpt = 0;
short LoggerOpt = 0;
char  *LogFile = LOG_FILE;

void EditFile(const char *user, const char *file);
int GetReplaceStream(const char *user, const char *file);

int
main(int ac, char **av)
{
	enum { NONE, EDIT, LIST, REPLACE, DELETE } option = NONE;
	struct passwd *pas;
	char *repFile = NULL;
	int repFd = 0;
	int i;
	char caller[256];		/* user that ran program */

	UserId = getuid();
	if ((pas = getpwuid(UserId)) == NULL) {
		perror("getpwuid");
		exit(1);
	}
	snprintf(caller, sizeof(caller), "%s", pas->pw_name);

	i = 1;
	if (ac > 1) {
		if (av[1][0] == '-' && av[1][1] == 0) {
			option = REPLACE;
			++i;
		} else if (av[1][0] != '-') {
			option = REPLACE;
			++i;
			repFile = av[1];
		}
	}

	opterr = 0;

	while ((i=getopt(ac,av,"ledu:c:")) != EOF) {
		switch(i) {
			case 'l':
					option = LIST;
				break;
			case 'e':
					option = EDIT;
				break;
			case 'd':
					option = DELETE;
				break;
			case 'u':
				if (*optarg != 0 && getuid() == geteuid()) {
					pas = getpwnam(optarg);
					if (pas) {
						UserId = pas->pw_uid;
					} else {
						errx(1, "user %s unknown", optarg);
					}
				} else {
					errx(1, "only the superuser may specify a user");
				}
				break;
			case 'c':
				if (*optarg != 0 && getuid() == geteuid()) {
					CDir = optarg;
				} else {
					errx(1, "-c option: superuser only");
				}
				break;
			default:
				option = NONE;
		}
	}

	if (option == NONE) {
		/*
		 * parse error
		 */
		printf("crontab " VERSION "\n");
		printf("crontab file <opts>  replace crontab from file\n");
		printf("crontab -    <opts>  replace crontab from stdin\n");
		printf("crontab -u user      specify user\n");
		printf("crontab -l [user]    list crontab for user\n");
		printf("crontab -e [user]    edit crontab for user\n");
		printf("crontab -d [user]    delete crontab for user\n");
		printf("crontab -c dir       specify crontab directory\n");
		exit(2);
	}

	/*
	 * Get password entry
	 */

	if ((pas = getpwuid(UserId)) == NULL) {
		perror("getpwuid");
		exit(1);
	}

	/*
	 * If there is a replacement file, obtain a secure descriptor to it.
	 */

	if (repFile) {
		repFd = GetReplaceStream(caller, repFile);
		if (repFd < 0) {
			errx(1, "unable to read replacement file");
		}
	}

	/*
	 * Change directory to our crontab directory
	 */

	if (chdir(CDir) < 0) {
		errx(1, "cannot change dir to %s: %s", CDir, strerror(errno));
	}

	/*
	 * Handle options as appropriate
	 */

	switch(option) {
		case LIST:
			{
				FILE *fi;
				char buf[1024];

				if ((fi = fopen(pas->pw_name, "r"))) {
					while (fgets(buf, sizeof(buf), fi) != NULL)
						fputs(buf, stdout);
					fclose(fi);
				} else {
					fprintf(stderr, "no crontab for %s\n", pas->pw_name);
				}
			}
			break;
		case EDIT:
			{
				FILE *fi;
				int fd;
				int n;
				char tmp[] = TMPDIR "/crontab.XXXXXX";
				char buf[1024];

				if ((fd = mkstemp(tmp)) >= 0) {
					chown(tmp, getuid(), getgid());
					if ((fi = fopen(pas->pw_name, "r"))) {
						while ((n = fread(buf, 1, sizeof(buf), fi)) > 0)
							write(fd, buf, n);
					}
					EditFile(caller, tmp);
					remove(tmp);
					lseek(fd, 0L, 0);
					repFd = fd;
				} else {
					errx(1, "unable to create %s", tmp);
				}

			}
			option = REPLACE;
			/* fall through */
		case REPLACE:
			{
				char buf[1024];
				char path[1024];
				int fd;
				int n;

				snprintf(path, sizeof(path), "%s.new", pas->pw_name);
				if ((fd = open(path, O_CREAT|O_TRUNC|O_EXCL|O_APPEND|O_WRONLY, 0600)) >= 0) {
					while ((n = read(repFd, buf, sizeof(buf))) > 0) {
						write(fd, buf, n);
					}
					close(fd);
					rename(path, pas->pw_name);
				} else {
					fprintf(stderr, "unable to create %s/%s: %s\n",
							CDir,
							path,
							strerror(errno)
						   );
				}
				close(repFd);
			}
			break;
		case DELETE:
			remove(pas->pw_name);
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
			fflush(fo);
			if (fstat(fileno(fo), &st) != 0 || st.st_nlink != 0) {
				fclose(fo);
				break;
			}
			fclose(fo);
			/* loop */
		}
		if (fo == NULL) {
			fprintf(stderr, "unable to append to %s/%s\n", CDir, CRONUPDATE);
		}
	}
	(volatile void)exit(0);
	/* not reached */
}

int
GetReplaceStream(const char *user, const char *file)
{
	int filedes[2];
	int pid;
	int fd;
	int n;
	char buf[1024];

	if (pipe(filedes) < 0) {
		perror("pipe");
		return(-1);
	}
	if ((pid = fork()) < 0) {
		perror("fork");
		return(-1);
	}
	if (pid > 0) {
		/*
		 * PARENT
		 */

		close(filedes[1]);
		if (read(filedes[0], buf, 1) != 1) {
			close(filedes[0]);
			filedes[0] = -1;
		}
		return(filedes[0]);
	}

	/*
	 * CHILD
	 */

	close(filedes[0]);

	if (ChangeUser(user, 0) < 0)
		exit(0);

	fd = open(file, O_RDONLY);
	if (fd < 0)
		errx(0, "unable to open %s", file);
	buf[0] = 0;
	write(filedes[1], buf, 1);
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		write(filedes[1], buf, n);
	}
	exit(0);
}

void
EditFile(const char *user, const char *file)
{
	int pid;

	if ((pid = fork()) == 0) {
		/*
		 * CHILD - change user and run editor
		 */
		const char *ptr;
		char visual[1024];

		if (ChangeUser(user, 1) < 0)
			exit(0);
		if ((ptr = getenv("VISUAL")) == NULL || strlen(ptr) > 256)
			ptr = PATH_VI;

		snprintf(visual, sizeof(visual), "%s %s", ptr, file);
		execl("/bin/sh", "/bin/sh", "-c", visual, NULL);
		perror("exec");
		exit(0);
	}
	if (pid < 0) {
		/*
		 * PARENT - failure
		 */
		perror("fork");
		exit(1);
	}
	wait4(pid, NULL, 0, NULL);
}

