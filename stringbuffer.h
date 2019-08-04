#ifndef STRING_BUFFER_H
#define STRING_BUFFER_H

#define SB_INIT \
    {           \
        NULL, 0 \
    }

typedef struct StringBuffer
{
    char *s;
    unsigned int len;
} StringBuffer;

void sbAppend(StringBuffer *sb, const char *s, const unsigned int len);
void sbFree(StringBuffer *sb);

#endif