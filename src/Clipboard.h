#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <dos/dos.h>
#include <exec/types.h>
#include <libraries/iffparse.h>

/******************************************************************************
 *
 * A writer takes text and puts it somewhere: in the clipboard, as the FORM
 * FTXT holding one CHRS chunk that every Amiga text application reads, or in
 * a plain file. Opening and closing are what tell the two apart; in between,
 * the caller only writes, and never has to know which of the two it got.
 *
 * Errors are collected rather than returned: WriterWrite() keeps quiet and
 * remembers, so that a long value can be written in one straight run, and
 * WriterClose() tells at the end whether all of it made it through. Closing
 * also undoes exactly what the opening did, in the right order, whether it
 * follows a completed write or abandons a half-opened writer.
 *
 ******************************************************************************/

/* STRUCTURES */

struct Writer
{
    struct IFFHandle *       wr_IFF;     /* Clipboard stream, else NULL      */
    struct ClipboardHandle * wr_Clip;    /* Its handle, else NULL            */
    BPTR                     wr_File;    /* File, else zero                  */
    UWORD                    wr_Chunks;  /* Chunks pushed, still to pop      */
    BOOL                     wr_Nested;  /* OpenIFF() done, CloseIFF() owed  */
    BOOL                     wr_Error;   /* Sticky: a write did not go       */
};

/* PROTOTYPES */

BOOL WriterOpenClipboard(struct Writer * writer);
BOOL WriterOpenFile(struct Writer * writer, CONST_STRPTR path);
VOID WriterWrite(struct Writer * writer, CONST_STRPTR data, LONG length);
BOOL WriterClose(struct Writer * writer);

#endif /* CLIPBOARD_H */
