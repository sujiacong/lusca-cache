#include <stdio.h>
#include <string.h>

int
strmatchbeg(const char *search, const char *match, int maxlen)
{
    int mlen = strlen(match);
    if (maxlen < mlen)
        return -1; 
    return strncmp(search, match, mlen);
}       
            
int
strmatch(const char *search, const char *match, int maxlen)
{           
    int mlen = strlen(match); 
    if (maxlen < mlen)
        return -1;
    return strncmp(search, match, maxlen);
}               

