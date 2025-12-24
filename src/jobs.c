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

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#include <sys/types.h>
#include <sys/param.h>
#ifdef BSD
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include <sys/ioctl.h>

#include "shell.h"
#if JOBS
#include <termios.h>
#undef CEOF			/* syntax.h redefines this */
#endif
#include "exec.h"
#include "eval.h"
#include "init.h"
#include "redir.h"
#include "show.h"
#include "main.h"
#include "parser.h"
#include "nodes.h"
#include "jobs.h"
#include "options.h"
#include "trap.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "system.h"

/* mode flags for set_curjob */
#define CUR_DELETE 2
#define CUR_RUNNING 1
#define CUR_STOPPED 0

/* mode flags for dowait */
#define DOWAIT_NONBLOCK 0
#define DOWAIT_BLOCK 1
#define DOWAIT_WAITCMD 2
#define DOWAIT_WAITCMD_ALL 4

/* array of jobs */
static struct job *jobtab;
/* size of array */
static unsigned njobs;
/* pid of last background process */
pid_t backgndpid;

#if JOBS
/* control terminal */
static int ttyfd = -1;
#endif

/* current job */
static struct job *curjob;

/* Set if we are in the vforked child */
int vforked;

STATIC void set_curjob(struct job *, unsigned);
STATIC int sprint_status(char *, int, int);
STATIC void freejob(struct job *);
STATIC struct job *growjobtab(void);
STATIC int dowait(int, struct job *);
#ifdef SYSV
STATIC int onsigchild(void);
#endif
STATIC int waitproc(int, int *);
STATIC void cmdtxt(union node *);
STATIC void cmdlist(union node *, int);
STATIC void cmdputs(const char *);
STATIC int getstatus(struct job *);

#if JOBS
static void xtcsetpgrp(int, pid_t);
#endif

STATIC void
set_curjob(struct job *jp, unsigned mode)
{
	struct job *jp1;
	struct job **jpp, **curp;

	/* first remove from list */
	jpp = curp = &curjob;
	do {
		jp1 = *jpp;
		if (jp1 == jp)
			break;
		jpp = &jp1->prev_job;
	} while (1);
	*jpp = jp1->prev_job;

	/* Then re-insert in correct position */
	jpp = curp;
	switch (mode) {
	default:
#ifdef DEBUG
		abort();
#endif
	case CUR_DELETE:
		/* job being deleted */
		break;
	case CUR_RUNNING:
		/* newly created job or backgrounded job,
		   put after all stopped jobs. */
		do {
			jp1 = *jpp;
			if (!JOBS || !jp1 || jp1->state != JOBSTOPPED)
				break;
			jpp = &jp1->prev_job;
		} while (1);
		/* FALLTHROUGH */
#if JOBS
	case CUR_STOPPED:
#endif
		/* newly stopped job - becomes curjob */
		jp->prev_job = *jpp;
		*jpp = jp;
		break;
	}
}

#if JOBS
/*
 * Turn job control on and off.
 *
 * Note:  This code assumes that the third arg to ioctl is a character
 * pointer, which is true on Berkeley systems but not System V.  Since
 * System V doesn't have job control yet, this isn't a problem now.
 *
 * Called with interrupts off.
 */

int jobctl;

#endif

#if JOBS

#endif

STATIC int
sprint_status(char *os, int status, int sigonly)
{
	char *s = os;
	int st;

	st = WEXITSTATUS(status);
	if (!WIFEXITED(status)) {
#if JOBS
		st = WSTOPSIG(status);
		if (!WIFSTOPPED(status))
#endif
			st = WTERMSIG(status);
		if (sigonly) {
			if (st == SIGINT || st == SIGPIPE)
				goto out;
#if JOBS
			if (WIFSTOPPED(status))
				goto out;
#endif
		}
		s = stpncpy(s, strsignal(st), 32);
#ifdef WCOREDUMP
		if (WCOREDUMP(status)) {
			s = stpcpy(s, " (core dumped)");
		}
#endif
	} else if (!sigonly) {
		if (st)
			s += fmtstr(s, 16, "Done(%d)", st);
		else
			s = stpcpy(s, "Done");
	}

out:
	return s - os;
}

/*
 * Mark a job structure as unused.
 */

STATIC void
freejob(struct job *jp)
{
	struct procstat *ps;
	int i;

	INTOFF;
	for (i = jp->nprocs, ps = jp->ps ; --i >= 0 ; ps++) {
		if (ps->cmd != nullstr)
			ckfree(ps->cmd);
	}
	if (jp->ps != &jp->ps0)
		ckfree(jp->ps);
	jp->used = 0;
	set_curjob(jp, CUR_DELETE);
	INTON;
}


/*
 * Return a new job structure.
 * Called with interrupts off.
 */

struct job *
makejob(union node *node, int nprocs)
{
	int i;
	struct job *jp;

	for (i = njobs, jp = jobtab ; ; jp++) {
		if (--i < 0) {
			jp = growjobtab();
			break;
		}
		if (jp->used == 0)
			break;
		if (jp->state != JOBDONE || !jp->waited)
			continue;
		if (jobctl)
			continue;
		freejob(jp);
		break;
	}
	memset(jp, 0, sizeof(*jp));
#if JOBS
	if (jobctl)
		jp->jobctl = 1;
#endif
	jp->prev_job = curjob;
	curjob = jp;
	jp->used = 1;
	jp->ps = &jp->ps0;
	if (nprocs > 1) {
		jp->ps = ckmalloc(nprocs * sizeof (struct procstat));
	}
	TRACE(("makejob(0x%lx, %d) returns %%%d\n", (long)node, nprocs,
	    jobno(jp)));
	return jp;
}

STATIC struct job *
growjobtab(void)
{
	size_t len;
	ptrdiff_t offset;
	struct job *jp, *jq;

	len = njobs * sizeof(*jp);
	jq = jobtab;
	jp = ckrealloc(jq, len + 4 * sizeof(*jp));

	offset = (char *)jp - (char *)jq;
	if (offset) {
		/* Relocate pointers */
		size_t l = len;

		jq = (struct job *)((char *)jq + l);
		while (l) {
			l -= sizeof(*jp);
			jq--;
#define joff(p) ((struct job *)((char *)(p) + l))
#define jmove(p) (p) = (void *)((char *)(p) + offset)
			if (likely(joff(jp)->ps == &jq->ps0))
				jmove(joff(jp)->ps);
			if (joff(jp)->prev_job)
				jmove(joff(jp)->prev_job);
		}
		if (curjob)
			jmove(curjob);
#undef joff
#undef jmove
	}

	njobs += 4;
	jobtab = jp;
	jp = (struct job *)((char *)jp + len);
	jq = jp + 3;
	do {
		jq->used = 0;
	} while (--jq >= jp);
	return jp;
}

/*
 * Wait for job to finish.
 *
 * Under job control we have the problem that while a child process is
 * running interrupts generated by the user are sent to the child but not
 * to the shell.  This means that an infinite loop started by an inter-
 * active user may be hard to kill.  With job control turned off, an
 * interactive user may place an interactive program inside a loop.  If
 * the interactive program catches interrupts, the user doesn't want
 * these interrupts to also abort the loop.  The approach we take here
 * is to have the shell ignore interrupt signals while waiting for a
 * forground process to terminate, and then send itself an interrupt
 * signal if the child process was terminated by an interrupt signal.
 * Unfortunately, some programs want to do a bit of cleanup and then
 * exit on interrupt; unless these processes terminate themselves by
 * sending a signal to themselves (instead of calling exit) they will
 * confuse this approach.
 *
 * Called with interrupts off.
 */

int
waitforjob(struct job *jp)
{
	int st;

	TRACE(("waitforjob(%%%d) called\n", jp ? jobno(jp) : 0));
	dowait(jp ? DOWAIT_BLOCK : DOWAIT_NONBLOCK, jp);
	if (!jp)
		return exitstatus;

	st = getstatus(jp);
#if JOBS
	if (jp->jobctl) {
		xtcsetpgrp(ttyfd, rootpid);
		/*
		 * This is truly gross.
		 * If we're doing job control, then we did a TIOCSPGRP which
		 * caused us (the shell) to no longer be in the controlling
		 * session -- so we wouldn't have seen any ^C/SIGINT.  So, we
		 * intuit from the subprocess exit status whether a SIGINT
		 * occurred, and if so interrupt ourselves.  Yuck.  - mycroft
		 */
		if (jp->sigint)
			raise(SIGINT);
	}
#endif
	if (! JOBS || jp->state == JOBDONE)
		freejob(jp);
	return st;
}



/*
 * Wait for a process to terminate.
 */

static int waitone(int block, struct job *job)
{
	int pid;
	int status;
	struct job *jp;
	struct job *thisjob = NULL;
	int state;

	INTOFF;
	TRACE(("dowait(%d) called\n", block));
	pid = waitproc(block, &status);
	TRACE(("wait returns pid %d, status=%d\n", pid, status));
	if (pid <= 0)
		goto out;

	for (jp = curjob; jp; jp = jp->prev_job) {
		struct procstat *sp;
		struct procstat *spend;
		if (jp->state == JOBDONE)
			continue;
		state = JOBDONE;
		spend = jp->ps + jp->nprocs;
		sp = jp->ps;
		do {
			if (sp->pid == pid) {
				TRACE(("Job %d: changing status of proc %d from 0x%x to 0x%x\n", jobno(jp), pid, sp->status, status));
				sp->status = status;
				thisjob = jp;
			}
			if (sp->status == -1)
				state = JOBRUNNING;
#if JOBS
			if (state == JOBRUNNING)
				continue;
			if (WIFSTOPPED(sp->status)) {
				jp->stopstatus = sp->status;
				state = JOBSTOPPED;
			}
#endif
		} while (++sp < spend);
		if (thisjob)
			goto gotjob;
	}
	goto out;

gotjob:
	if (state != JOBRUNNING) {
		thisjob->changed = 1;

		if (thisjob->state != state) {
			TRACE(("Job %d: changing state from %d to %d\n", jobno(thisjob), thisjob->state, state));
			thisjob->state = state;
#if JOBS
			if (state == JOBSTOPPED) {
				set_curjob(thisjob, CUR_STOPPED);
			}
#endif
		}
	}

out:
	INTON;

	if (thisjob && thisjob == job) {
		char s[48 + 1];
		int len;

		len = sprint_status(s, status, 1);
		if (len) {
			s[len] = '\n';
			s[len + 1] = 0;
			outstr(s, out2);
		}
	}
	return pid;
}

static int dowait(int block, struct job *jp)
{
	int gotchld = *(volatile int *)&gotsigchld;
	int rpid;
	int pid;

	if (jp && jp->state != JOBRUNNING)
		block = DOWAIT_NONBLOCK;

	if (block == DOWAIT_NONBLOCK && !gotchld)
		return 1;

	rpid = 1;

	do {
		pid = waitone(block, jp);
		rpid &= !!pid;

		block &= ~DOWAIT_WAITCMD_ALL;
		if (!pid || (jp && jp->state != JOBRUNNING))
			block = DOWAIT_NONBLOCK;
	} while (pid >= 0);

	return rpid;
}

/*
 * Do a wait system call.  If block is zero, we return -1 rather than
 * blocking.  If block is DOWAIT_WAITCMD, we return 0 when a signal
 * other than SIGCHLD interrupted the wait.
 *
 * We use sigsuspend in conjunction with a non-blocking wait3 in
 * order to ensure that waitcmd exits promptly upon the reception
 * of a signal.
 *
 * For code paths other than waitcmd we either use a blocking wait3
 * or a non-blocking wait3.  For the latter case the caller of dowait
 * must ensure that it is called over and over again until all dead
 * children have been reaped.  Otherwise zombies may linger.
 */


STATIC int
waitproc(int block, int *status)
{
	sigset_t oldmask;
	int flags = block == DOWAIT_BLOCK ? 0 : WNOHANG;
	int err;

#if JOBS
	if (jobctl)
		flags |= WUNTRACED;
#endif

	do {
		gotsigchld = 0;
		do
			err = wait3(status, flags, NULL);
		while (err < 0 && errno == EINTR);

		if (err || (err = -!block))
			break;

		sigblockall(&oldmask);

		while (!gotsigchld && !pending_sig)
			sigsuspend(&oldmask);

		sigclearmask();
	} while (gotsigchld);

	return err;
}

/*
 * return 1 if there are stopped jobs, otherwise 0
 */
int job_warning;
int
stoppedjobs(void)
{
	struct job *jp;
	int retval;

	retval = 0;
	if (job_warning)
		goto out;
	jp = curjob;
	if (jp && jp->state == JOBSTOPPED) {
		out2str("You have stopped jobs.\n");
		job_warning = 2;
		retval++;
	}

out:
	return retval;
}


STATIC void
cmdtxt(union node *n)
{
	union node *np;
	struct nodelist *lp;
	const char *p;
	char s[2];

	if (!n)
		return;
	switch (n->type) {
	default:
#if DEBUG
		abort();
#endif
	case NPIPE:
		lp = n->npipe.cmdlist;
		for (;;) {
			cmdtxt(lp->n);
			lp = lp->next;
			if (!lp)
				break;
			cmdputs(" | ");
		}
		break;
	case NSEMI:
		p = "; ";
		goto binop;
	case NAND:
		p = " && ";
		goto binop;
	case NOR:
		p = " || ";
binop:
		cmdtxt(n->nbinary.ch1);
		cmdputs(p);
		n = n->nbinary.ch2;
		goto donode;
	case NREDIR:
	case NBACKGND:
		n = n->nredir.n;
		goto donode;
	case NNOT:
		cmdputs("!");
		n = n->nnot.com;
donode:
		cmdtxt(n);
		break;
	case NIF:
		cmdputs("if ");
		cmdtxt(n->nif.test);
		cmdputs("; then ");
		if (n->nif.elsepart) {
			cmdtxt(n->nif.ifpart);
			cmdputs("; else ");
			n = n->nif.elsepart;
		} else {
			n = n->nif.ifpart;
		}
		p = "; fi";
		goto dotail;
	case NSUBSHELL:
		cmdputs("(");
		n = n->nredir.n;
		p = ")";
		goto dotail;
	case NWHILE:
		p = "while ";
		goto until;
	case NUNTIL:
		p = "until ";
until:
		cmdputs(p);
		cmdtxt(n->nbinary.ch1);
		n = n->nbinary.ch2;
		p = "; done";
dodo:
		cmdputs("; do ");
dotail:
		cmdtxt(n);
		goto dotail2;
	case NFOR:
		cmdputs("for ");
		cmdputs(n->nfor.var);
		cmdputs(" in ");
		cmdlist(n->nfor.args, 1);
		n = n->nfor.body;
		p = "; done";
		goto dodo;
	case NDEFUN:
		cmdputs(n->ndefun.text);
		p = "() { ... }";
		goto dotail2;
	case NCMD:
		cmdlist(n->ncmd.args, 1);
		cmdlist(n->ncmd.redirect, 0);
		break;
	case NARG:
		p = n->narg.text;
dotail2:
		cmdputs(p);
		break;
	case NHERE:
	case NXHERE:
		p = "<<...";
		goto dotail2;
	case NCASE:
		cmdputs("case ");
		cmdputs(n->ncase.expr->narg.text);
		cmdputs(" in ");
		for (np = n->ncase.cases; np; np = np->nclist.next) {
			cmdtxt(np->nclist.pattern);
			cmdputs(") ");
			cmdtxt(np->nclist.body);
			cmdputs(";; ");
		}
		p = "esac";
		goto dotail2;
	case NTO:
		p = ">";
		goto redir;
	case NCLOBBER:
		p = ">|";
		goto redir;
	case NAPPEND:
		p = ">>";
		goto redir;
	case NTOFD:
		p = ">&";
		goto redir;
	case NFROM:
		p = "<";
		goto redir;
	case NFROMFD:
		p = "<&";
		goto redir;
	case NFROMTO:
		p = "<>";
redir:
		s[0] = n->nfile.fd + '0';
		s[1] = '\0';
		cmdputs(s);
		cmdputs(p);
		if (n->type == NTOFD || n->type == NFROMFD) {
			s[0] = n->ndup.dupfd + '0';
			p = s;
			goto dotail2;
		} else {
			n = n->nfile.fname;
			goto donode;
		}
	}
}

STATIC void
cmdlist(union node *np, int sep)
{
	for (; np; np = np->narg.next) {
		if (!sep)
			cmdputs(spcstr);
		cmdtxt(np);
		if (sep && np->narg.next)
			cmdputs(spcstr);
	}
}

STATIC char *cmdnextc;

STATIC void
cmdputs(const char *s)
{
	const char *p, *str;
	char cc[2] = " ";
	char *nextc;
	signed char c;
	int subtype = 0;
	int quoted = 0;
	static const char vstype[VSTYPE + 1][4] = {
		"", "}", "-", "+", "?", "=",
		"%", "%%", "#", "##",
	};

	nextc = makestrspace((strlen(s) + 1) * 8, cmdnextc);
	p = s;
	while ((c = *p++) != 0) {
		str = 0;
		switch (c) {
		case CTLESC:
			c = *p++;
			break;
		case CTLVAR:
			subtype = *p++;
			if ((subtype & VSTYPE) == VSLENGTH)
				str = "${#";
			else
				str = "${";
			goto dostr;
		case CTLENDVAR:
			str = "\"}";
			str += !(quoted & 1);
			quoted >>= 1;
			subtype = 0;
			goto dostr;
		case CTLBACKQ:
			str = "$(...)";
			goto dostr;
		case CTLARI:
			str = "$((";
			goto dostr;
		case CTLENDARI:
			str = "))";
			goto dostr;
		case CTLQUOTEMARK:
			quoted ^= 1;
			c = '"';
			break;
		case '=':
			if (subtype == 0)
				break;
			if ((subtype & VSTYPE) != VSNORMAL)
				quoted <<= 1;
			str = vstype[subtype & VSTYPE];
			if (subtype & VSNUL)
				c = ':';
			else
				goto checkstr;
			break;
		case '\'':
		case '\\':
		case '"':
		case '$':
			/* These can only happen inside quotes */
			cc[0] = c;
			str = cc;
			c = '\\';
			break;
		default:
			break;
		}
		USTPUTC(c, nextc);
checkstr:
		if (!str)
			continue;
dostr:
		while ((c = *str++)) {
			USTPUTC(c, nextc);
		}
	}
	if (quoted & 1) {
		USTPUTC('"', nextc);
	}
	*nextc = 0;
	cmdnextc = nextc;
}


#if JOBS
STATIC void
xtcsetpgrp(int fd, pid_t pgrp)
{
	int err;

	sigblockall(NULL);
	err = tcsetpgrp(fd, pgrp);
	sigclearmask();

	if (err)
		sh_error("Cannot set tty process group (%s)", strerror(errno));
}
#endif


STATIC int
getstatus(struct job *job) {
	int status;
	int retval;

	status = job->ps[job->nprocs - 1].status;
	retval = WEXITSTATUS(status);
	if (!WIFEXITED(status)) {
#if JOBS
		retval = WSTOPSIG(status);
		if (!WIFSTOPPED(status))
#endif
		{
			/* XXX: limits number of signals */
			retval = WTERMSIG(status);
#if JOBS
			if (retval == SIGINT)
				job->sigint = 1;
#endif
		}
		retval += 128;
	}
	TRACE(("getstatus: job %d, nproc %d, status %x, retval %x\n",
		jobno(job), job->nprocs, status, retval));
	return retval;
}
