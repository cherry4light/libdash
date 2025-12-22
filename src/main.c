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

#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>


#include "shell.h"
#include "main.h"
#include "mail.h"
#include "options.h"
#include "output.h"
#include "parser.h"
#include "nodes.h"
#include "expand.h"
#include "eval.h"
#include "jobs.h"
#include "input.h"
#include "trap.h"
#include "var.h"
#include "show.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "mystring.h"
#include "exec.h"
#include "cd.h"

#define PROFILE 0

int rootpid;
int shlvl;
#ifdef __GLIBC__
int *dash_errno;
#endif
#if PROFILE
short profile_buf[16384];
extern int etext();
#endif
MKINIT struct jmploc main_handler;

static int cmdloop(int);

//libdash
void
initialize_dash_errno() 
{
#ifdef __GLIBC__
	dash_errno = __errno_location();
#endif
}

#ifdef MAIN // libdash
int main(int, char **);
#endif //MAIN // libdash

/*
 * Main routine.  We initialize things, parse the arguments, execute
 * profiles if we're a login shell, and then call cmdloop to execute
 * commands.  The setjmp call sets up the location to jump to when an
 * exception occurs.  When an exception occurs the variable "state"
 * is used to figure out how far we had gotten.
 */

#ifdef MAIN //libdash
int
main(int argc, char **argv)
{
	return 1;
}

#endif // MAIN // libdash

/*
 * Read and execute commands.  "Top" is nonzero for the top level command
 * loop; it turns on prompting if the shell is interactive.
 */

static int
cmdloop(int top)
{
	union node *n;
	struct stackmark smark;
	int inter;
	int status = 0;
	int numeof = 0;

	TRACE(("cmdloop(%d) called\n", top));
	for (;;) {
		int skip;

		setstackmark(&smark);
		if (jobctl)
			showjobs(out2, SHOW_CHANGED);
		inter = 0;
		if (iflag && top) {
			inter++;
		}
		n = parsecmd(inter);
		/* showtree(n); DEBUG */
		if (n == NEOF) {
			if (!top || numeof >= 50)
				break;
			if (!stoppedjobs()) {
				if (!Iflag) {
					if (iflag) {
						out2c('\n');
#ifdef FLUSHERR
						flushout(out2);
#endif
					}
					break;
				}
				out2str("\nUse \"exit\" to leave shell.\n");
			}
			numeof++;
		} else {
			int i;

			job_warning = (job_warning == 2) ? 1 : 0;
			numeof = 0;
			i = evaltree(n, 0);
			if (n)
				status = i;
		}
		popstackmark(&smark);

		skip = evalskip;
		if (skip) {
			evalskip &= ~(SKIPFUNC | SKIPFUNCDEF);
			break;
		}
	}

	return status;
}


/*
 * Read a file containing shell functions.
 */

void
readcmdfile(char *name)
{
	setinputfile(name, INPUT_PUSH_FILE);
	cmdloop(0);
	popfile();
}

#ifdef mkinit
INCLUDE "error.h"

FORKRESET {
	handler = &main_handler;
}
#endif
