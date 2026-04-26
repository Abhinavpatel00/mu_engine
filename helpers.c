#include "helpers.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t hash32_bytes(const void* data, size_t size)
{
    return (uint32_t)XXH32(data, size, 0);
}

uint64_t hash64_bytes(const void* data, size_t size)
{
    return (uint64_t)XXH64(data, size, 0);
}

uint32_t round_up(uint32_t a, uint32_t b)
{
    return (a + b - 1) & ~(b - 1);
}
uint64_t round_up_64(uint64_t a, uint64_t b)
{
    return (a + b - 1) & ~(b - 1);
}

size_t c99_strnlen(const char* s, size_t maxlen)
{
    size_t i = 0;
    if(!s)
        return 0;
    for(; i < maxlen && s[i]; i++)
    {
    }
    return i;
}

bool helpers_read_text_file(const char* path, char** out_text, size_t* out_size)
{
    if(!path || !out_text)
        return false;

    FILE* f = fopen(path, "rb");
    if(!f)
        return false;

    if(fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }

    long end = ftell(f);
    if(end < 0)
    {
        fclose(f);
        return false;
    }
    if(fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }

    size_t size = (size_t)end;
    char* text = (char*)malloc(size + 1u);
    if(!text)
    {
        fclose(f);
        return false;
    }

    if(size > 0)
    {
        size_t read_n = fread(text, 1, size, f);
        if(read_n != size)
        {
            free(text);
            fclose(f);
            return false;
        }
    }
    text[size] = '\0';
    fclose(f);

    *out_text = text;
    if(out_size)
        *out_size = size;
    return true;
}

char* helpers_next_line(char** cursor)
{
    if(!cursor || !*cursor)
        return NULL;

    char* start = *cursor;
    if(*start == '\0')
        return NULL;

    char* end = start;
    while(*end != '\0' && *end != '\n' && *end != '\r')
        ++end;

    if(*end == '\r')
    {
        *end = '\0';
        ++end;
        if(*end == '\n')
            ++end;
    }
    else if(*end == '\n')
    {
        *end = '\0';
        ++end;
    }

    *cursor = end;
    return start;
}

void helpers_trim_ascii(char* s)
{
    if(!s)
        return;

    char* start = s;
    while(*start && isspace((unsigned char)*start))
        ++start;

    char* end = start + strlen(start);
    while(end > start && isspace((unsigned char)end[-1]))
        --end;

    *end = '\0';
    if(start != s)
        memmove(s, start, (size_t)(end - start) + 1u);
}

bool helpers_parse_u32(const char* token, uint32_t* out_value)
{
    if(!token || !out_value)
        return false;

    errno = 0;
    char* end = NULL;
    unsigned long v = strtoul(token, &end, 10);
    if(errno != 0 || end == token || *end != '\0' || v > UINT32_MAX)
        return false;

    *out_value = (uint32_t)v;
    return true;
}

bool helpers_parse_f32(const char* token, float* out_value)
{
    if(!token || !out_value)
        return false;

    errno = 0;
    char* end = NULL;
    float v = strtof(token, &end);
    if(errno != 0 || end == token || *end != '\0')
        return false;

    *out_value = v;
    return true;
}
