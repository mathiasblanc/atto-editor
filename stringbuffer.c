#include <string.h>
#include <stdlib.h>
#include "stringbuffer.h"

void sbAppend(StringBuffer *sb, const char *s, const unsigned int len)
{
    char *new = realloc(sb->s, sb->len + len);

    if (new == NULL)
        return;

    memcpy(&new[sb->len], s, len);
    sb->s = new;
    sb->len += len;
}

void sbFree(StringBuffer *sb)
{
    free(sb->s);
}