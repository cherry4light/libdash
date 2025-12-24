/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>	/* PIPE_BUF */
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Code for dealing with input/output redirection.
 */

#include "main.h"
#include "shell.h"
#include "nodes.h"
#include "jobs.h"
#include "options.h"
#include "expand.h"
#include "redir.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "trap.h"


#define EMPTY -2		/* marks an unused slot in redirtab */
#define CLOSED -1		/* fd opened for redir needs to be closed */

#ifndef PIPE_BUF
# define PIPESIZE 4096		/* amount of buffering in a pipe */
#else
# define PIPESIZE PIPE_BUF
#endif


MKINIT
struct redirtab {
        struct redirtab *next;
        int renamed[10];
};


MKINIT struct redirtab *redirlist;

/* Bit map of currently closed file descriptors. */
static unsigned closed_redirs;

static unsigned update_closed_redirs(int fd, int nfd)
{
	unsigned val = closed_redirs;
	unsigned bit = 1 << fd;

	if (nfd >= 0)
		closed_redirs &= ~bit;
	else
		closed_redirs |= bit;

	return val & bit;
}

static int sh_open_fail(const char *, int, int) __attribute__((__noreturn__));
static int sh_open_fail(const char *pathname, int flags, int e)
{
	const char *word;
	int action;

	word = "open";
	action = E_OPEN;
	if (flags & O_CREAT) {
		word = "create";
		action = E_CREAT;
	}

	sh_error("cannot %s %s: %s", word, pathname, errmsg(e, action));
}


int sh_open(const char *pathname, int flags, int mayfail)
{
	int fd;
	int e;

	do {
		fd = open64(pathname, flags, 0666);
		e = errno;
	} while (fd < 0 && e == EINTR && !pending_sig);

	if (mayfail || fd >= 0)
		return fd;

	sh_open_fail(pathname, flags, e);
}


/*
 * Undo the effects of the last redirection.
 */

void
popredir(int drop)
{
	struct redirtab *rp;
	int i;

	INTOFF;
	rp = redirlist;
	for (i = 0 ; i < 10 ; i++) {
		int closed;

		if (rp->renamed[i] == EMPTY)
			continue;

		closed = drop ? 1 : update_closed_redirs(i, rp->renamed[i]);

		switch (rp->renamed[i]) {
		case CLOSED:
			if (!closed)
				close(i);
			break;
		default:
			if (!drop)
				dup2(rp->renamed[i], i);
			close(rp->renamed[i]);
			break;
		}
	}
	redirlist = rp->next;
	ckfree(rp);
	INTON;
}

/*
 * Undo all redirections.  Called on error or interrupt.
 */

#ifdef mkinit

INCLUDE "redir.h"

EXITRESET {
	/*
	 * Discard all saved file descriptors.
	 */
	unwindredir(0);
}

FORKRESET {
	redirlist = NULL;
}

#endif

/*
 * Move a file descriptor to > 10.  Invokes sh_error on error unless
 * the original file dscriptor is not open.
 */

int
savefd(int from, int ofd)
{
        int newfd;
        int err;

        newfd = fcntl(from, F_DUPFD, 10);
        err = newfd < 0 ? errno : 0;
        if (err != EBADF) {
                close(ofd);
                if (err)
                        sh_error("%d: %s", from, strerror(err));
                else
                        fcntl(newfd, F_SETFD, FD_CLOEXEC);
        }

        return newfd;
}


void unwindredir(struct redirtab *stop)
{
        while (redirlist != stop)
                popredir(0);
}
