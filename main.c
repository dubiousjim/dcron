
/*
 * MAIN.C
 *
 * dcron -d[#] -c <crondir> [ -f | -b ]
 *
 * run as root, but NOT setuid root
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

Prototype short DebugOpt;
Prototype short LogLevel;
Prototype short ForegroundOpt;
Prototype const char *CDir;
Prototype const char *SCDir;
Prototype uid_t DaemonUid;
Prototype int InSyncFileRoot;

short DebugOpt;
short LogLevel = 8;
short ForegroundOpt;
const char  *CDir = CRONTABS;
const char  *SCDir = SCRONTABS;
uid_t DaemonUid;
int InSyncFileRoot;

int
main(int ac, char **av)
{
    int i;

    /*
     * parse options
     */

    DaemonUid = getuid();

    for (i = 1; i < ac; ++i) {
        char *ptr = av[i];

        if (*ptr == '-') {
	    ptr += 2;

	    switch(ptr[-1]) {
	    case 'l':
		LogLevel = (*ptr) ? strtol(ptr, NULL, 0) : 1;
		continue;
	    case 'd':
		DebugOpt = (*ptr) ? strtol(ptr, NULL, 0) : 1;
		LogLevel = 0;
		/* fall through */
	    case 'f':
		ForegroundOpt = 1;
		continue;
	    case 'b':
	        ForegroundOpt = 0;
	        continue;
	    case 'c':
		CDir = (*ptr) ? ptr : av[++i];
		continue;
	    case 's':
		SCDir = (*ptr) ? ptr : av[++i];
		continue;
	    default:
		break;
	    }
	}
	break;	/* error */
    }

    /*
     * check for parse error
     */

    if (i != ac) {
        if (i > ac)
            puts("expected argument for option");
	printf("dcron " VERSION "\n");
	printf("dcron -d[#] -l[#] -f -b -c dir -s dir\n");
	exit(1);
    }

    /*
     * close stdin and stdout (stderr normally redirected by caller).
     * close unused descriptors
     * optional detach from controlling terminal
     */

    fclose(stdin);
    fclose(stdout);

    i = open("/dev/null", O_RDWR);
    if (i < 0) {
        perror("open: /dev/null:");
        exit(1);
    }
    dup2(i, 0);
    dup2(i, 1);

    for (i = 3; i < OPEN_MAX; ++i) {
        close(i);
    }

    if (ForegroundOpt == 0) {
        int fd;
        int pid;

        if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
            ioctl(fd, TIOCNOTTY, 0);
            close(fd);
	}

        pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(1);
        }
        if (pid > 0)
            exit(0);
    }

    /* 
     * main loop - synchronize to 1 second after the minute, minimum sleep
     *             of 1 second.
     */

    log9("%s " VERSION " dillon, started\n", av[0]);
    SynchronizeDir(CDir, NULL, 1);
    SynchronizeDir(SCDir, "root", 1);

    {
    	time_t t1 = time(NULL);
    	time_t t2;
        long dt;
        short rescan = 60;
        short stime = 60;

	for (;;) {
	    sleep((stime + 1) - (short)(time(NULL) % stime));

	    t2 = time(NULL);
	    dt = t2 - t1;

	    /*
	     * The file 'cron.update' is checked to determine new cron
	     * jobs.  The directory is rescanned once an hour to deal
	     * with any screwups.
	     *
	     * check for disparity.  Disparities over an hour either way
	     * result in resynchronization.  A reverse-indexed disparity
	     * less then an hour causes us to effectively sleep until we
	     * match the original time (i.e. no re-execution of jobs that
	     * have just been run).  A forward-indexed disparity less then
	     * an hour causes intermediate jobs to be run, but only once
	     * in the worst case.
	     *
	     * when running jobs, the inequality used is greater but not
	     * equal to t1, and less then or equal to t2.
	     */

	    if (--rescan == 0) {
	        rescan = 60;
	        SynchronizeDir(CDir, NULL, 0);
	        SynchronizeDir(SCDir, "root", 0);
	    }
	    CheckUpdates(CDir, NULL);
	    CheckUpdates(SCDir, "root");
	    if (DebugOpt)
	        logn(5, "Wakeup dt=%d\n", dt);
	    if (dt < -60*60 || dt > 60*60) {
	        t1 = t2;
	        log9("time disparity of %d minutes detected\n", dt / 60);
	    } else if (dt > 0) {
	        TestJobs(t1, t2);
	        RunJobs();
	        sleep(5);
	        if (CheckJobs() > 0)
	           stime = 10;
	        else
	           stime = 60;
	        t1 = t2;
	    }
	}
    }
    /* not reached */
}

