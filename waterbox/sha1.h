/* sha1.h - SHA-1 of an open file (sha1.c). */
#ifndef CHIMERA_SHA1_H
#define CHIMERA_SHA1_H

#include <stdio.h>

/* Reads f to its end; writes 40 uppercase hex digits and a NUL into hex and the
 * byte count into *size (if size is not NULL). Returns 0 on a read error. */
int sha1_file(FILE *f, char hex[41], long *size);

#endif
