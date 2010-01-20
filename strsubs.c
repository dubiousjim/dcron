
/*
 * STRSUBS.C
 *
 */

#include "defs.h"

Prototype int try_strcpy(char *dest, const char *src, size_t destsize);
Prototype int try_sprintf(char *dest, size_t destsize, const char *fmt, ...);
Prototype int try_vsprintf(char *dest, size_t destsize, const char *fmt, va_list va);
Prototype char *concat(const char *s1, ...);


/*
 * if src and its terminating \0 fit into destsize, copy them to dest and return strlen (excluding \0)
 * if too long to fit, copy nothing and return strlen that would be needed
 * equivalent to try_sprintf(dest, destsize, "%s", src);
 */
int
try_strcpy(char *dest, const char *src, size_t destsize)
{
	size_t srclen;
	srclen = strlen(src);
	if (srclen < destsize) {
		strcpy(dest, src);
	}
	return srclen;
}

int
try_sprintf(char *dest, size_t destsize, const char *fmt, ...)
{
	int n;
	va_list va;
	va_start(va, fmt);
	n = try_vsprintf(dest, destsize, fmt, va);
	va_end(va);
	return n;
}

/*
 * if result string and its terminating \0 fit into destsize, return result's strlen (excluding \0)
 * C99-ish behavior: if need to truncate, return strlen >= destsize, will be long enough if > destsize
 *                   if destsize == 0, dest may be NULL; return strlen >= 0, will be long enough if > 0
 * if you don't care about truncation, and destsize > 0, can just use plain [v]sprintf
 */
int
try_vsprintf(char *dest, size_t destsize, const char *fmt, va_list va)
{
	int n;
	if (destsize > 0) {
		/*
		 * [v]snprintf always \0-terminate, and write at most size including \0
		 * on some systems (including glibc < 2.0.6) return value will be -1 if need to truncate
		 * C99 and glibc >= 2.1: return value will be strlen needed (excluding \0)
		 */
		n = vsnprintf(dest, destsize, fmt, va);
		if (n >= 0) {
			return n;
		} else {
			/* hack: caller will see this as indicating truncation, but we may need a longer strlen */
			return destsize;
		}
	} else {
		/*
		 * C99 permits dest to be NULL when destsize == 0, and result will still be strlen needed
		 * SUSv2 stipulates an unspecified return value < 1 (and does not allow dest == NULL?)
		 */
		char dest2[2];
		n = vsnprintf(dest2, 2, fmt, va);
		if (n < 1) {
			/*
			 * hack: we may need a longer strlen, but may not be able to determine how long except by doing unbounded sprintf
			 */
			return 0;
		} else {
			return n;
		}
	}
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
 * retrieved from http://cvsweb.openwall.com/cgi/cvsweb.cgi/Owl/packages/popa3d/popa3d/misc.c
 * see also http://seclists.org/bugtraq/2006/Nov/594
 *
 */

char *
concat(const char *s1, ...)
{
	va_list args;
	char *s, *p, *result;
	unsigned long l, m, n;

	m = n = strlen(s1);
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((m += l) < l) break;
	}
	va_end(args);
	if (s || m >= INT_MAX) return NULL;

	result = malloc(m + 1);
	if (!result) return NULL;

	memcpy(p = result, s1, n);
	p += n;
	va_start(args, s1);
	while ((s = va_arg(args, char *))) {
		l = strlen(s);
		if ((n += l) < l || n > m) break;
		memcpy(p, s, l);
		p += l;
	}
	va_end(args);
	if (s || m != n || p - result != n) {
		free(result);
		return NULL;
	}

	*p = 0;
	return result;
}

