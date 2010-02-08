
/*
 * UTILS.C
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

Prototype char *stringcat(const char *first, ...);

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
	if (s || m >= INT_MAX) return NULL;

	if (!(dst = malloc(m + 1))) return NULL;

	memcpy(p = dst, first, n);
	p += n;
	va_start(va, first);
	while ((s = va_arg(va, char *))) {
		/*
		 * somewhat inefficient: calculates each strlen twice
		 * but not as bad as using strcat here, as C FAQ 15.4 does
		 */
		k = strlen(s);
		if ((n += k) < k || n > m) break;
		memcpy(p, s, k);
		p += k;
	}
	va_end(va);
	if (s || m != n || p - dst != n) {
		free(dst);
		return NULL;
	}
	*p = '\0';
	return dst;
}
