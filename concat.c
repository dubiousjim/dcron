
/*
 * CONCAT.C
 *
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

#include "defs.h"

Prototype char *concat(const char *s1, ...);

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
