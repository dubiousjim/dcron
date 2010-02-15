
/*
 * UTILS.C
 *
 */

#include "defs.h"


Prototype /*@noreturn@*/ void fatal(/*@observer@*/ const char *msg);

Prototype /*@only@*/ /*@out@*/ /*@null@*/ void *xmalloc(size_t size);
Prototype /*@only@*/ /*@null@*/ void *xcalloc(size_t n, size_t size);
Prototype /*@only@*/ /*@out@*/ /*@null@*/ void *xrealloc(/*@only@*/ void *ptr, size_t size);

Prototype /*@maynotreturn@*/ /*@only@*/ char *stringdup(const char *src, size_t maxlen);
Prototype /*@maynotreturn@*/ /*@only@*/ char *stringcat(const char *first, ...);
Prototype size_t stringcpy(/*@unique@*/ /*@out@*/ char *dst, const char *src, size_t dstsize) /*@modifies *dst@*/;
Prototype size_t vstringf(/*@unique@*/ /*@out@*/ char *dst, size_t dstsize, const char *fmt, va_list va) /*@modifies *dst@*/;
Prototype size_t stringf(/*@unique@*/ /*@out@*/ char *dst, size_t dstsize, const char *fmt, ...) /*@modifies *dst@*/;


/*
 * Write "dcron: ${msg}\n" >&2, and exit(1)
 */
void
fatal(const char *msg)
{
	/* in general, we should first flush stdout */
	const char progname[8] = "dcron: ";
	size_t k = strlen(msg);
	/* FIXME perhaps write to syslog? */
	(void) write(2, progname, sizeof(progname)-1);
	(void) write(2, msg, k);
	if (k > 0 && msg[k-1] != '\n')
		(void) write(2, "\n", 1);
	exit(EXIT_FAILURE);
}


/*@-incondefs@*/
void *
xmalloc(size_t size) /*@ensures maxSet(result) == (size-1); @*/
/*@=incondefs@*/
{
	register void *result = malloc(size);
	if (size > 0 && result==NULL)
		fatal(strerror(ENOMEM)); /* Cannot allocate memory */
	return result;
}

/*@-incondefs@*/
/* can't do multiplication within splint's ensures clauses: assume size=1 */
void *
xcalloc(size_t n, size_t size) /*@ensures maxSet(result) >= (n-1); @*/
/*@=incondefs@*/
{
	register void *result = calloc(n, size);
	if (n > 0 && size > 0 && result==NULL)
		fatal(strerror(ENOMEM)); /* Cannot allocate memory */
	return result;
}

/*@-incondefs@*/
void *
xrealloc(void *ptr, size_t size) /*@ensures maxSet(ptr) == (size-1); @*/
/*@=incondefs@*/
{
	register void *result = realloc(ptr, size);
	if (size > 0 && result==NULL)
		fatal(strerror(ENOMEM)); /* Cannot allocate memory */
	return result;
}

/*
 * Returned ptr may be up to maxlen+1 bytes, will always be terminated.
 * Improves upon strndup by fail()ing if out of memory, instead of returning NULL;
 * also strndup requires _GNU_SOURCE.
 */
char *
stringdup(const char *src, size_t maxlen)
{
	register char *dst = (char *)xmalloc(maxlen + 1);
	assert(dst!=NULL);
	/*@-boundswrite@*/
	*dst = '\0';
	return strncat(dst, src, maxlen);
	/*@=boundswrite@*/
}

/*
 * Concatenates a variable number of strings.  The argument list must be
 * terminated with a NULL.  Returns a pointer to malloc(3)'ed memory with
 * the concatenated string, or NULL on error.
 *
 * This code deals gracefully with potential integer overflows (perhaps when
 * input strings are maliciously long), as well as with input strings changing
 * from under it (perhaps because of misbehavior of another thread). It does
 * not depend on non-portable functions such as snprintf() and asprintf().
 *
 * Written by Solar Designer <solar at openwall.com>  and placed in the
 * public domain.
 *
 * Retrieved from http://cvsweb.openwall.com/cgi/cvsweb.cgi/Owl/packages/popa3d/popa3d/misc.c
 * See also http://seclists.org/bugtraq/2006/Nov/594
 *
 */

char *
stringcat(const char *first, ...)
{
	va_list va;
	char *s, *p;
	register char *dst;
	size_t k, m, n;

	m = n = strlen(first);
	va_start(va, first);
	while ((s = va_arg(va, char *))) {
		k = strlen(s);
		if ((m += k) < k) break;
	}
	va_end(va);
	/* if (s || m >= INT_MAX) return NULL; */
	if (s || m >= (size_t)INT_MAX)
		fatal(strerror(ENOMEM));

	/* if (!(dst = malloc(m + 1))) return NULL; */
	dst = xmalloc(m + 1);
	assert(dst!=NULL);

	/* annotation shouldn't be necessary, but... */
	/*@-mayaliasunique@*/
	memcpy(p = dst, first, n);
	/*@=mayaliasunique@*/
	p += n;
	va_start(va, first);
	while ((s = va_arg(va, char *))) {
		/*
		 * somewhat inefficient: calculates each strlen twice
		 * but not as bad as using strcat here, as C FAQ 15.4 does
		 */
		k = strlen(s);
		if ((n += k) < k || n > m) break;
		/*@-boundswrite@*/
		memcpy(p, s, k);
		/*@=boundswrite@*/
		p += k;
	}
	va_end(va);
	if (s || m != n || (size_t)(p - dst) != n) {
		free(dst);
		/* return NULL; */
		fatal(strerror(ENOMEM)); /* Cannot allocate memory */
	}
	/*@-boundswrite@*/
	*p = '\0';
	/*@=boundswrite@*/
	return dst;
}

/*
 * if src and its terminating \0 fit into dstsize, copy them to dst and return strlen(src)
 * if too long to fit, copy nothing and return strlen that would be needed
 * improves upon strncpy by returning needed strlen if dst too small (rather than doing unterminated copy); and by not filling rest of dst with additional \0s
 * equivalent to stringf(dst, dstsize, "%s", src);
 */
/*@-incondefs@*/
size_t
stringcpy(char *dst, const char *src, size_t dstsize) /*@requires maxSet(dst) >= ( dstsize - 1 ); @*/ /*@ensures maxRead (dst) <= maxRead(src) /\ maxRead (dst) <= dstsize; @*/ /*@modifies *dst@*/
/*@=incondefs@*/
{
	/*@-mustdefine@*/
	size_t k = strlen(src);
	if (k < dstsize) {
		/*@-boundswrite@*/
		strcpy(dst, src);
		/*@=boundswrite@*/
	}
	return k;
	/*@=mustdefine@*/
}

/*
 * portability wrapper around [v]sprintf to ensure C99-ish behavior
 * if dstsize > 0 and you don't care about truncation, can just use plain [v]sprintf
 *
 * if result string and its terminating \0 fit into dstsize, return result's strlen (excluding \0)
 * C99-ish behavior: if needed to truncate, returns a strlen >= dstsize (which you can rely on to be long enough when > dstsize)
 *                   if dstsize == 0, dst may be NULL; returns a strlen >= 0 (which you can rely on to be long enough when > 0)
 */
/*@-incondefs@*/
size_t
vstringf(char *dst, size_t dstsize, const char *fmt, va_list va) /*@requires maxSet(dst) >= ( dstsize - 1); @*/ /*@modifies *dst@*/
/*@=incondefs@*/
{
	int k;
	if (dstsize > 0) {
		/*
		 * [v]snprintf always terminates, and writes at most dstsize including \0
		 * on some systems including glibc < 2.0.6, return value will be -1 if needs to truncate
		 * on C99 and glibc >= 2.1, return value will be the strlen needed (excluding \0)
		 */
		/*@-mods@*/
		k = vsnprintf(dst, dstsize, fmt, va);
		/*@=mods@*/
		if (k >= 0) {
			return (size_t)k;
		} else {
			/* hack: this tells the caller we truncated, but an even longer strlen may be required */
			return dstsize;
		}
	} else {
		/*
		 * C99 permits dst to be NULL when dstsize == 0, and result will still be strlen needed
		 * SUSv2 stipulates an unspecified return value < 1 (and does not allow dst == NULL?)
		 */
		char dst2[2];
		k = vsnprintf(dst2, 2, fmt, va);
		/*@-mustdefine@*/
		if (k < 1) {
			/*
			 * hack: a longer strlen may be required, but we may not be able to determine how long except by doing unbounded sprintf
			 */
			return 0;
		} else {
			return (size_t)k;
		}
		/*@=mustdefine@*/
	}
}

/*@-incondefs@*/
size_t
stringf(char *dst, size_t dstsize, const char *fmt, ...) /*@requires maxSet(dst) >= ( dstsize - 1); @*/ /*@modifies *dst@*/
/*@=incondefs@*/
{
	size_t k;
	va_list va;
	va_start(va, fmt);
	k = vstringf(dst, dstsize, fmt, va);
	va_end(va);
	return k;
}
