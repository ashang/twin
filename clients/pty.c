/*
 *  parts of code taken from:
 *
 *  rxvt-2.4.7 sources (various authors)
 *  available at ftp://ftp.math.fu-berlin.de/pub/rxvt/
 *  and usual Linux mirrors
 *
 *
 * other parts from Twin autor:
 *
 *  Copyright (C) 1993-1999  Massimiliano Ghilardi
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "libTw.h"
#include "term.h"

/* pseudo-teletype connections handling functions */

/* 0. variables */
static char *ptydev, *ttydev;
static int ptyfd, ttyfd;

/* 1. Acquire a pseudo-teletype from the system. */
/*
 * On failure, returns FALSE.
 * On success, fills ttydev and ptydev with the names of the
 * master and slave parts and sets ptyfd to the pty file descriptor
 */
static byte get_pty(void)
{
    int fd = -1, sfd = -1;
#ifdef USE_DEVPTMX
    extern char *ptsname();

    /* open master pty /dev/ptmx */
    if ((fd = open("/dev/ptmx", O_RDWR)) >= 0) {
	grantpt(fd);
	unlockpt(fd);
	ptydev = ttydev = ptsname(fd);
	goto Found:
    }
#else
    static char     pty_name[] = "/dev/pty??";
    static char     tty_name[] = "/dev/tty??";
    int             len = strlen(tty_name);
    char           *c1, *c2;

    ptydev = pty_name;
    ttydev = tty_name;

# define PTYCHAR1	"pqrstuvwxyzabcde"
# define PTYCHAR2	"0123456789abcdef"
    for (c1 = PTYCHAR1; *c1; c1++) {
	ptydev[len - 2] = ttydev[len - 2] = *c1;
	for (c2 = PTYCHAR2; *c2; c2++) {
	    ptydev[len - 1] = ttydev[len - 1] = *c2;
	    if ((fd = open(ptydev, O_RDWR)) >= 0) {
		if ((sfd = open(ttydev, O_RDWR)) >= 0)
		    /* access(ttydev, R_OK|W_OK) won't do as it checks against REAL uid */
		    goto Found;
		close(fd);
	    }
	}
    }
#endif
    return FALSE;

Found:
    fcntl(fd, F_SETFL, O_NDELAY);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    ttyfd = sfd;
    ptyfd = fd;
    return TRUE;
}

/* 2. Fixup permission for pty master/slave pairs */
static byte fixup_pty(void) {
    uid_t id = getuid();
    static gid_t grgid = 0;

    /*
    if (!grgid) {
	struct group *gr;
	if ((gr = getgrnam("tty")))
	    grgid = gr->gr_gid;
    }
     */
    if (chown(ptydev, id, 0) == 0 && chmod(ptydev, 0600) == 0 &&
	chown(ttydev, id, grgid) == 0 && chmod(ttydev, 0620) == 0)
	return TRUE;
    return FALSE;
}

/* 3. Establish ttyfd as controlling teletype for new session and switch to it */
static byte switchto_tty(void)
{
    int i;
    pid_t pid;

    pid = setsid();
    if (pid < 0)
	return FALSE;

    /*
     * Hope all other file descriptors are set to fcntl(fd, F_SETFD, FD_CLOEXEC)
     */
    for (i=0; i<=2; i++) {
	if (i != ttyfd) {
	    close(i);
	    dup2(ttyfd, i);
	}
    }
    if (ttyfd > 2)
	close(ttyfd);

    ioctl(0, TIOCSCTTY, 0);

/* set process group */
#if defined (_POSIX_VERSION) || defined (__svr4__)
    tcsetpgrp(0, pid);
#elif defined (TIOCSPGRP)
    ioctl(0, TIOCSPGRP, &pid);
#endif

    return TRUE;
}

/* 5. fork() a program in a pseudo-teletype */
uldat Spawn(twindow Window, pid_t *ppid, char *args[]) {

    TwGetPrivileges();
    
    if (!get_pty()) {
	TwDropPrivileges();
	return FALSE;
    }
    (void)fixup_pty();
    
    TwDropPrivileges();

    switch ((*ppid = fork())) {
      case -1:
	/* failed */
	close(ptyfd);
	ptyfd = -1;
	break;
      case 0:
	/* child */
	if (!switchto_tty())
	    exit(1);
	execvp(args[0], args+1);
	exit(1);
	break;
      default:
	/* father */
	break;
    }
    close(ttyfd);
    
    return ptyfd;
}
