
/*
 * UTILS.C
 *
 */

#include "defs.h"


Prototype int dprintf(int fd, const char *fmt, ...);
Prototype int vdprintf(int fd, const char *fmt, va_list va);
Prototype /*@noreturn@*/ void fatal(/*@observer@*/ const char *fmt, ...) __attribute__((noreturn));

Prototype /*@only@*/ /*@out@*/ /*@null@*/ void *xmalloc(size_t size) __attribute__((malloc));
Prototype /*@only@*/ /*@null@*/ void *xcalloc(size_t n, size_t size) __attribute__((malloc));
Prototype /*@only@*/ /*@out@*/ /*@null@*/ void *xrealloc(/*@only@*/ void *ptr, size_t size) __attribute__((malloc));

Prototype /*@maynotreturn@*/ /*@only@*/ char *stringdup(const char *src, size_t maxlen);
Prototype /*@maynotreturn@*/ /*@only@*/ char *stringdupmany(const char *first, ...) __attribute__((sentinel));
Prototype size_t stringcpy(/*@unique@*/ /*@out@*/ char *dst, const char *src, size_t dstsize) /*@modifies *dst@*/;
Prototype size_t stringcat(/*@unique@*/ /*@out@*/ char *dst, const char *src, size_t dstsize, size_t dstlen) /*@modifies *dst@*/;

Prototype size_t vstringf(/*@unique@*/ /*@out@*/ char *dst, size_t dstsize, const char *fmt, va_list va) /*@modifies *dst@*/;
Prototype size_t stringf(/*@unique@*/ /*@out@*/ char *dst, size_t dstsize, const char *fmt, ...) /*@modifies *dst@*/;

Prototype hash_t hash(const unsigned char *key, unsigned short extra);

Prototype /*@observer@*/ const char progname[];




int dprintf(int fd, const char *fmt, ...) {
	va_list va;
	int n;
	va_start(va, fmt);
	n = vdprintf(fd, fmt, va);
	va_end(va);
	return n;
}

int vdprintf(int fd, const char *fmt, va_list va) {
	char buf[LINE_BUF];
	size_t k;
	if ((k=vstringf(buf, sizeof(buf), fmt, va)) >= sizeof(buf))
		/* output was truncated */
		k = sizeof(buf) - 1;
	return (int)write(fd, buf, k);
}


/*
 * Write "progname: ...\n" > stderr, and exit(1).
 * Does not rely on [v]snprintf or [v]dprintf.
 * Writes will fail if stderr has been fclosed(), even if fd 2 is open.
 */
void
fatal(const char *fmt, ...)
{
	va_list va;
	size_t k;
	int eoln;

	/* flush stdout, ignoring errors */
	(void)fflush(stdout);

	k = strlen(fmt);
	eoln = (int)(k > 0 && fmt[k - 1] == '\n');

	/* write "progname: " to stderr */
	if (fprintf(stderr, "%s: ", progname) > 0) {
		/* write formatted message, appending \n if necessary */
		va_start(va, fmt);
		if (vfprintf(stderr, fmt, va) >= 0 && !eoln)
			(void)fputc('\n', stderr);
		va_end(va);
	}

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
stringdupmany(const char *first, ...)
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
 * Copy src into dst, truncating and terminating if necessary.
 * Return actual strlen(src). We needed to truncate when strlen(src) >= dstsize.
 * Improves upon strncpy by returning needed strlen if dst too small (rather than doing unterminated copy); and by not filling rest of dst with additional \0s.
 * Equivalent to stringf(dst, dstsize, "%s", src).
 */
/*@-incondefs@*/
size_t
stringcpy(char *dst, const char *src, size_t dstsize) /*@requires maxSet(dst) >= ( dstsize - 1 ); @*/ /*@ensures maxRead (dst) <= maxRead(src) /\ maxRead (dst) <= dstsize; @*/ /*@modifies *dst@*/
/*@=incondefs@*/
{
	return stringcat(dst, src, dstsize, 0);
}

/*
 * Copy src into dst after existing dstlen chars, truncating and terminating if necessary
 * Return actual strlen needed to hold previous plus new strings.
 * We needed to truncate when returned strlen >= dstdize.
 * Unlike strncat, src must be \0 terminated; and we return the needed strlen if dst is too small, rather than another ptr to dst.
 */
/*@-incondefs@*/
size_t
stringcat(char *dst, const char *src, size_t dstsize, size_t dstlen) /*@requires maxSet(dst) >= ( dstsize - 1 ); @*/ /*@ensures maxRead (dst) <= ( dstlen + maxRead(src) ) /\ maxRead (dst) <= dstsize; @*/ /*@modifies *dst@*/
/*@=incondefs@*/
{
	size_t k = strlen(src);
	if (dstlen < dstsize) {
		/*@-mayaliasunique@*/
		if (dstlen + k < dstsize) {
			memcpy(dst + dstlen, src, k + 1);
		} else {
			memcpy(dst + dstlen, src, dstsize - dstlen - 1);
			dst[dstsize - 1] = '\0';
		}
		/*@=mayaliasunique@*/
	}
	/*@-mustdefine@*/
	return dstlen + k;
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


/*
 * sdbm hash function = hash(i-1) * 65599 + str[i]
 */
hash_t
hash(const unsigned char *key, unsigned short extra)
{
	hash_t h = extra;
	/*
	if (extra) {
		hash_t hi = (hash_t)extra & 0xff;
		h = (hi<<8) + (hi>>2) - (hi>>8) + extra - hi;
	}
	*/
	while ((int)*key)
		h = (h<<16) + (h<<6) - h + (int)*key++;
	return h;
}

/*
 * bernstein hash function = hash(i - 1)*33 xor str[i]
 *
hash_t
bernstein_hash(const unsigned char *key)
{
	hash_t h=0;
	while ((int)*key)
		h = h*33 ^ (int)*key++;
	return h;
}
 *
 */

