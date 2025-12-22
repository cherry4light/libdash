/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
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

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int	 conv_escape_str(char *, char **);
static char	*conv_escape(char *, int *);
static int	 getchr(void);
static double	 getdouble(void);
static uintmax_t getuintmax(int);
static char	*getstr(void);
static char	*mklong(const char *, const char *);
static void      check_conversion(const char *, const char *);

static int	rval;
static char  **gargv;

#define isodigit(c)	((c) >= '0' && (c) <= '7')
#define octtobin(c)	((c) - '0')

#include "bltin.h"
#include "system.h"

#define PF(f, func) { \
	switch ((char *)param - (char *)array) { \
	default: \
		(void)printf(f, array[0], array[1], func); \
		break; \
	case sizeof(*param): \
		(void)printf(f, array[0], func); \
		break; \
	case 0: \
		(void)printf(f, func); \
		break; \
	} \
}

#define ASPF(sp, f, func) ({ \
	int ret; \
	switch ((char *)param - (char *)array) { \
	default: \
		ret = xasprintf(sp, f, array[0], array[1], func); \
		break; \
	case sizeof(*param): \
		ret = xasprintf(sp, f, array[0], func); \
		break; \
	case 0: \
		ret = xasprintf(sp, f, func); \
		break; \
	} \
	ret; \
})


static int print_escape_str(const char *f, int *param, int *array, char *s)
{
	struct stackmark smark;
	char *p, *q;
	int done;
	int len;
	int total;

	setstackmark(&smark);
	done = conv_escape_str(s, &q);
	p = stackblock();
	len = q - p;
	total = len - 1;

	q[-1] = (!!((f[1] - 's') | done) - 1) & f[2];
	total += !!q[-1];
	if (f[1] == 's')
		goto easy;

	p = makestrspace(len, q);
	memset(p, 'X', total);
	p[total] = 0;

	q = stackblock();
	total = ASPF(&p, f, p);

	len = strchrnul(p, 'X') - p;
	memcpy(p + len, q, strspn(p + len, "X"));

easy:
	out1mem(p, total);

	popstackmark(&smark);
	return done;
}


/*
 * Print SysV echo(1) style escape string 
 *	Halts processing string if a \c escape is encountered.
 */
static int
conv_escape_str(char *str, char **sp)
{
	int c;
	int ch;
	char *cp;

	/* convert string into a temporary buffer... */
	STARTSTACKSTR(cp);

	do {
		c = ch = *str++;
		if (ch != '\\')
			continue;

		c = *str++;
		if (c == 'c') {
			/* \c as in SYSV echo - abort all processing.... */
			c = ch = 0x100;
			continue;
		}

		/* 
		 * %b string octal constants are not like those in C.
		 * They start with a \0, and are followed by 0, 1, 2, 
		 * or 3 octal digits. 
		 */
		if (c == '0' && isodigit(*str))
			str++;

		/* Finally test for sequences valid in the format string */
		str = conv_escape(str - 1, &c);
	} while (STPUTC(c, cp), (char)ch);

	*sp = cp;

	return ch;
}

/*
 * Print "standard" escape characters 
 */
static char *
conv_escape(char *str, int *conv_ch)
{
	int value;
	int ch;

	ch = *str;

	switch (ch) {
	default:
		if (!isodigit(*str)) {
			value = '\\';
			goto out;
		}

		ch = 3;
		value = 0;
		do {
			value <<= 3;
			value += octtobin(*str++);
		} while (isodigit(*str) && --ch);
		goto out;

	case '\\':	value = '\\';	break;	/* backslash */
	case 'a':	value = '\a';	break;	/* alert */
	case 'b':	value = '\b';	break;	/* backspace */
	case 'f':	value = '\f';	break;	/* form-feed */
	case 'n':	value = '\n';	break;	/* newline */
	case 'r':	value = '\r';	break;	/* carriage-return */
	case 't':	value = '\t';	break;	/* tab */
	case 'v':	value = '\v';	break;	/* vertical-tab */
	}

	str++;
out:
	*conv_ch = value;
	return str;
}

static char *
mklong(const char *str, const char *ch)
{
	/*
	 * Replace a string like "%92.3u" with "%92.3"PRIuMAX.
	 *
	 * Although C99 does not guarantee it, we assume PRIiMAX,
	 * PRIoMAX, PRIuMAX, PRIxMAX, and PRIXMAX are all the same
	 * as PRIdMAX with the final 'd' replaced by the corresponding
	 * character.
	 */

	char *copy;
	size_t len;	

	len = ch - str + sizeof(PRIdMAX);
	STARTSTACKSTR(copy);
	copy = makestrspace(len, copy);
	memcpy(copy, str, len - sizeof(PRIdMAX));
	memcpy(copy + len - sizeof(PRIdMAX), PRIdMAX, sizeof(PRIdMAX));
	copy[len - 2] = *ch;
	return (copy);	
}

static int
getchr(void)
{
	int val = 0;

	if (*gargv)
		val = **gargv++;
	return val;
}

static char *
getstr(void)
{
	char *val = nullstr;

	if (*gargv)
		val = *gargv++;
	return val;
}

static uintmax_t
getuintmax(int sign)
{
	uintmax_t val = 0;
	char *cp, *ep;

	cp = *gargv;
	if (cp == NULL)
		goto out;
	gargv++;

	val = (unsigned char) cp[1];
	if (*cp == '\"' || *cp == '\'')
		goto out;

	errno = 0;
	val = sign ? strtoimax(cp, &ep, 0) : strtoumax(cp, &ep, 0);
	check_conversion(cp, ep);
out:
	return val;
}

static double
getdouble(void)
{
	double val;
	char *cp, *ep;

	cp = *gargv;
	if (cp == NULL)
		return 0;
	gargv++;

	if (*cp == '\"' || *cp == '\'')
		return (unsigned char) cp[1];

	errno = 0;
	val = strtod(cp, &ep);
	check_conversion(cp, ep);
	return val;
}

static void
check_conversion(const char *s, const char *ep)
{
	if (*ep) {
		if (ep == s)
			warnx("%s: expected numeric value", s);
		else
			warnx("%s: not completely converted", s);
		rval = 1;
	} else if (errno == ERANGE) {
		warnx("%s: %s", s, strerror(ERANGE));
		rval = 1;
	}
}
