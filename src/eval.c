/*-
 * Copyright (c) 1993
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

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

/*
 * Evaluate a command.
 */

#include "init.h"
#include "main.h"
#include "shell.h"
#include "nodes.h"
#include "syntax.h"
#include "expand.h"
#include "parser.h"
#include "jobs.h"
#include "eval.h"
#include "builtins.h"
#include "options.h"
#include "exec.h"
#include "redir.h"
#include "input.h"
#include "output.h"
#include "trap.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "show.h"
#include "mystring.h"
#ifndef SMALL
#include "myhistedit.h"
#endif


int evalskip;			/* set if we are skipping commands */
MKINIT int loopnest;		/* current loop nesting level */

char *commandname;
int exitstatus;			/* exit status of last command */
int back_exitstatus;		/* exit status of backquoted command */
int savestatus = -1;		/* exit status of last command outside traps */

/* Prevent PS4 nesting. */
MKINIT int inps4;


#if !defined(__alpha__) || (defined(__GNUC__) && __GNUC__ >= 3)
STATIC
#endif
void evaltreenr(union node *, int) __attribute__ ((__noreturn__));


/*
 * Called to reset things after an exception.
 */

#ifdef mkinit
INCLUDE "eval.h"

EXITRESET {
	if (savestatus >= 0) {
		if (exception == EXEXIT || evalskip == SKIPFUNCDEF)
			exitstatus = savestatus;
		savestatus = -1;
	}
	evalskip = 0;
	loopnest = 0;
	inps4 = 0;
}
#endif

#include <stdio.h>

/*
 * Evaluate a parse tree.  The value is left in the global variable
 * exitstatus.
 */

int
evaltree(union node *n, int flags)
{
	printf("evaltree CALLED! todo: fix\n");
	return 0;
}


#if !defined(__alpha__) || (defined(__GNUC__) && __GNUC__ >= 3)
STATIC
#endif
void evaltreenr(union node *n, int flags)
#ifdef HAVE_ATTRIBUTE_ALIAS
	__attribute__ ((alias("evaltree")));
#else
{
	evaltree(n, flags);
	abort();
}
#endif

