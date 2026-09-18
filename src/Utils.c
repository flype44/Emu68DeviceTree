/******************************************************************************
 * 
 * Utils.c
 * 
 ******************************************************************************/

#include <stdarg.h>
#include <dos/dos.h>
#include <exec/exec.h>
#include <exec/types.h>
#include <utility/utility.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include "Utils.h"

extern struct ExecBase * SysBase;
extern struct Library * UtilityBase;

/******************************************************************************
 *
 * PROTOTYPES
 *
 ******************************************************************************/

STATIC LONG  StringCompare2(CONST_STRPTR string1, CONST_STRPTR string2);
STATIC ULONG BitsAt(ULONG high, ULONG low, UWORD offset, UWORD count);
STATIC BOOL  IsLeapYear(ULONG year);

/******************************************************************************
 *
 * PutChar()
 *
 ******************************************************************************/

STATIC ASM VOID PutChar(REG(d0) UBYTE character, REG(a3) struct FmtStream * stream)
{
    if (stream->left > 0)
    {
        *stream->pos++ = character;
        stream->left--;
    }
}

/******************************************************************************
 * 
 * SPrintf()
 * 
 ******************************************************************************/

VOID SPrintf(STRPTR buffer, LONG size, CONST_STRPTR format, ...)
{
    struct FmtStream stream;
    va_list args;

    if ((buffer == NULL) || (size <= 0))
    {
        return;
    }

    stream.pos  = buffer;
    stream.left = size - 1;
    va_start(args, format);
    RawDoFmt((STRPTR)format, (APTR)args, (VOID (*)())PutChar, (APTR)&stream);
    va_end(args);
    *stream.pos = '\0';
}

/******************************************************************************
 * 
 * StringLength()
 * 
 ******************************************************************************/

LONG StringLength(CONST_STRPTR string)
{
    CONST_STRPTR start = string;

    if (string == NULL)
    {
        return (0);
    }

    while (*string)
    {
        string++;
    }

    return ((LONG)(string - start));
}

/******************************************************************************
 * 
 * StringCopy()
 * 
 ******************************************************************************/

VOID StringCopy(STRPTR buffer, CONST_STRPTR string, LONG size)
{
    LONG i = 0;

    if ((buffer == NULL) || (size <= 0))
    {
        return;
    }

    if (string != NULL)
    {
        while ((i < (size - 1)) && string[i])
        {
            buffer[i] = string[i];
            i++;
        }
    }

    buffer[i] = '\0';
}

/******************************************************************************
 * 
 * StringCompare()
 * 
 ******************************************************************************/

LONG StringCompare(CONST_STRPTR string1, CONST_STRPTR string2)
{
    LONG result;

    if (string1 == NULL)
    {
        string1 = (CONST_STRPTR)"";
    }

    if (string2 == NULL)
    {
        string2 = (CONST_STRPTR)"";
    }

    result = (LONG)Stricmp(string1, string2);

    if (result == 0)
    {
        result = StringCompare2(string1, string2);
    }

    return (result);
}

/******************************************************************************
 * 
 * StringCompare2()
 * 
 ******************************************************************************/

STATIC LONG StringCompare2(CONST_STRPTR string1, CONST_STRPTR string2)
{
    while (*string1 && (*string1 == *string2))
    {
        string1++;
        string2++;
    }

    return ((LONG)*string1 - (LONG)*string2);
}

/******************************************************************************
 *
 * StringContains()
 *
 * Whether 'needle' occurs anywhere in 'haystack', case insensitive. An
 * empty needle matches everything; a NULL haystack matches nothing else.
 *
 ******************************************************************************/

BOOL StringContains(CONST_STRPTR haystack, CONST_STRPTR needle)
{
    LONG needleLen;
    LONG hayLen;
    LONG i;

    needleLen = StringLength(needle);

    if (needleLen == 0)
    {
        return (TRUE);
    }

    hayLen = StringLength(haystack);

    for (i = 0; (i + needleLen) <= hayLen; i++)
    {
        if ((LONG)Strnicmp(&haystack[i], needle, needleLen) == 0)
        {
            return (TRUE);
        }
    }

    return (FALSE);
}

/******************************************************************************
 *
 * StringCount()
 *
 ******************************************************************************/

ULONG StringCount(CONST_STRPTR bytes, ULONG length)
{
    ULONG count = 0;
    ULONG i = 0;

    if (bytes == NULL)
    {
        return (0);
    }

    while (i < length)
    {
        if (bytes[i] == '\0')
        {
            i++;
            continue;
        }

        count++;

        while ((i < length) && (bytes[i] != '\0'))
        {
            i++;
        }
    }

    return (count);
}

/******************************************************************************
 *
 * IsAsciiValue()
 *
 * Device-tree string properties are NUL-terminated, and a "string list"
 * packs several of them back to back, each ending in its own NUL. This
 * classifies 'bytes' as text under that convention: a final NUL is
 * mandatory, and at least one run of two or more printable characters is
 * required, so a length below 2, an all-NUL padding run, or a lone
 * printable byte between separators does not count as text.
 *
 ******************************************************************************/

BOOL IsAsciiValue(CONST_STRPTR bytes, ULONG length)
{
    BOOL text = FALSE;
    ULONG run;
    ULONG i;

    if ((bytes == NULL) || (length < 2))
    {
        return (FALSE);
    }

    if (bytes[length - 1] != '\0')
    {
        return (FALSE);
    }

    run = 0;

    for (i = 0; i < length - 1; i++)
    {
        if (bytes[i] == '\0')
        {
            run = 0; /* separator or padding */
            continue;
        }

        if ((bytes[i] < 32) || (bytes[i] >= 127))
        {
            return (FALSE);
        }

        if (++run >= 2)
        {
            text = TRUE;
        }
    }

    return (text);
}

/******************************************************************************
 * 
 * BitsAt()
 * 
 * The 'count' bits of a 64 bit value held in a pair of longwords, taken at
 * 'offset' bits from the bottom. Used to walk a value digit by digit in any
 * base which is a power of two.
 * 
 ******************************************************************************/

STATIC ULONG BitsAt(ULONG high, ULONG low, UWORD offset, UWORD count)
{
    ULONG value;
    ULONG mask;

    if (count == 0)
    {
        return (0);
    }

    if (offset >= 32)
    {
        value = high >> (offset - 32);
    }
    else
    {
        value = low >> offset;

        if (offset > 0)
        {
            value |= high << (32 - offset);
        }
    }

    if (count >= (8 * sizeof(ULONG)))
    {
        mask = ~0UL;
    }
    else
    {
        mask = (1UL << count) - 1;
    }

    return (value & mask);
}

/******************************************************************************
 * 
 * FormatBits()
 * 
 * A value written in binary, octal or hexadecimal.
 * 
 ******************************************************************************/

VOID FormatBits(
    ULONG  high,
    ULONG  low,
    UWORD  width,
    UWORD  bits,
    BOOL   trim,
    STRPTR buffer,
    LONG   size)
{
    STATIC CONST_STRPTR digits = (CONST_STRPTR)"0123456789ABCDEF";
    WORD count = (WORD)(((width * 8) + bits - 1) / bits);
    LONG pos = 0;

    if (trim)
    {
        while ((count > 1) && (BitsAt(high, low, (UWORD)((count - 1) * bits), bits) == 0))
        {
            count--;
        }
    }

    while ((count-- > 0) && (pos < (size - 1)))
    {
        buffer[pos++] = digits[BitsAt(high, low, (UWORD)(count * bits), bits)];
    }

    buffer[pos] = '\0';
}

/******************************************************************************
 *
 * IsLeapYear()
 *
 ******************************************************************************/

STATIC BOOL IsLeapYear(ULONG year)
{
    if ((year % 4) != 0)
    {
        return (FALSE);
    }

    if ((year % 100) != 0)
    {
        return (TRUE);
    }

    return ((BOOL)((year % 400) == 0));
}

/******************************************************************************
 *
 * FormatEpoch()
 *
 * Seconds since 1970-01-01 as "YYYY-MM-DD HH:MM:SS", proleptic Gregorian,
 * no timezone. Written by hand rather than through utility.library's clock
 * support, which works from a struct DateStamp, not a raw epoch value.
 *
 ******************************************************************************/

VOID FormatEpoch(ULONG epoch, STRPTR buffer, ULONG size)
{
    STATIC CONST UBYTE daysInMonth[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    ULONG days;
    ULONG secs;
    ULONG year = 1970;
    ULONG month = 0;
    ULONG day;
    ULONG hour;
    ULONG minute;
    ULONG second;

    if ((buffer == NULL) || (size < 20))
    {
        if ((buffer != NULL) && (size > 0))
        {
            buffer[0] = '\0';
        }

        return;
    }

    days = epoch / 86400UL;
    secs = epoch % 86400UL;
    hour = secs / 3600UL;
    secs %= 3600UL;
    minute = secs / 60UL;
    second = secs % 60UL;

    for (;;)
    {
        ULONG yearLength = IsLeapYear(year) ? 366UL : 365UL;

        if (days < yearLength)
        {
            break;
        }

        days -= yearLength;
        year++;
    }

    while (month < 12)
    {
        ULONG monthLength = daysInMonth[month];

        if ((month == 1) && IsLeapYear(year))
        {
            monthLength++;
        }

        if (days < monthLength)
        {
            break;
        }

        days -= monthLength;
        month++;
    }

    month++;
    day = days + 1;

    buffer[ 0] = (UBYTE)('0' + ((year / 1000) % 10));
    buffer[ 1] = (UBYTE)('0' + ((year / 100) % 10));
    buffer[ 2] = (UBYTE)('0' + ((year / 10) % 10));
    buffer[ 3] = (UBYTE)('0' + (year % 10));
    buffer[ 4] = '-';
    buffer[ 5] = (UBYTE)('0' + (month / 10));
    buffer[ 6] = (UBYTE)('0' + (month % 10));
    buffer[ 7] = '-';
    buffer[ 8] = (UBYTE)('0' + (day / 10));
    buffer[ 9] = (UBYTE)('0' + (day % 10));
    buffer[10] = ' ';
    buffer[11] = (UBYTE)('0' + (hour / 10));
    buffer[12] = (UBYTE)('0' + (hour % 10));
    buffer[13] = ':';
    buffer[14] = (UBYTE)('0' + (minute / 10));
    buffer[15] = (UBYTE)('0' + (minute % 10));
    buffer[16] = ':';
    buffer[17] = (UBYTE)('0' + (second / 10));
    buffer[18] = (UBYTE)('0' + (second % 10));
    buffer[19] = '\0';
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
