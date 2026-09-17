#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <exec/types.h>

/* COMPILER */

#ifdef __SASC
  #define REG(x) register __ ## x
  #define ASM __asm
  #define SAVEDS __saveds
#else
  #define REG(x)
  #define ASM
  #define SAVEDS
#endif

/* STRUCTURES */

struct FmtStream
{
    STRPTR pos;   /* Where the next character goes    */
    LONG   left;  /* Room left, terminator excluded   */
};

/* PROTOTYPES */

VOID  SPrintf(STRPTR buffer, LONG size, CONST_STRPTR format, ...);
LONG  StringCompare(CONST_STRPTR string1, CONST_STRPTR string2);
VOID  StringCopy(STRPTR buffer, CONST_STRPTR string, LONG size);
ULONG StringCount(CONST_STRPTR bytes, ULONG length);
LONG  StringLength(CONST_STRPTR string);
BOOL  IsAsciiValue(CONST_STRPTR bytes, ULONG length);
VOID  FormatBits(ULONG high, ULONG low, UWORD width, UWORD bits, BOOL trim, STRPTR buffer, LONG size);
VOID  FormatEpoch(ULONG epoch, STRPTR buffer, ULONG size);

#endif
