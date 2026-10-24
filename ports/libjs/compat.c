/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <sys/types.h>
/* GNU libstdc++'s large-file entry points use the SDK's 64-bit offsets. */
int fseeko64(FILE* file,off_t offset,int whence){return fseeko(file,offset,whence);}
off_t ftello64(FILE* file){return ftello(file);}
