/******************************************************************************
 * 
 * Clipboard.c
 * 
 * The two places a value can be written to, and everything they owe the
 * system: the IFF plumbing of the clipboard, and a file handle.
 * 
 ******************************************************************************/

#include <dos/dos.h>
#include <exec/exec.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include "Clipboard.h"

/******************************************************************************
 * 
 * EXTERNS
 * 
 * iffparse.library is opened by the startup code, and has been part of the
 * system since V36. Should it be missing, only WriterOpenClipboard() fails.
 * 
 ******************************************************************************/

extern struct Library * IFFParseBase;

/******************************************************************************
 * 
 * MACROS
 * 
 * Chunk identifiers of a clipboard text, which iffparse.h does not carry.
 * 
 ******************************************************************************/

#ifndef ID_FTXT
  #define ID_FTXT MAKE_ID('F','T','X','T')
#endif

#ifndef ID_CHRS
  #define ID_CHRS MAKE_ID('C','H','R','S')
#endif

/******************************************************************************
 * 
 * PROTOTYPES
 * 
 ******************************************************************************/

STATIC VOID WriterClear(struct Writer * writer);

/******************************************************************************
 *
 * WriterClear()
 *
 * An empty writer: nothing open, nothing owed, no error.
 *
 ******************************************************************************/

STATIC VOID WriterClear(struct Writer * writer)
{
    writer->wr_IFF     = NULL;
    writer->wr_Clip    = NULL;
    writer->wr_File    = 0;
    writer->wr_OwnFile = FALSE;
    writer->wr_Chunks  = 0;
    writer->wr_Nested  = FALSE;
    writer->wr_Error   = FALSE;
}

/******************************************************************************
 *
 * WriterOpenClipboard()
 *
 * Open the primary clipboard unit and push the two chunks a text lives in.
 *
 * Each step records what it owes before the next one may fail, so that a
 * failure anywhere can simply hand the writer to WriterClose(): it undoes
 * as much as was done, and nothing else.
 *
 ******************************************************************************/

BOOL WriterOpenClipboard(struct Writer * writer)
{
    WriterClear(writer);

    if (IFFParseBase == NULL)
    {
        return (FALSE);
    }

    if ((writer->wr_IFF = AllocIFF()) == NULL)
    {
        return (FALSE);
    }

    if ((writer->wr_Clip = OpenClipboard(PRIMARY_CLIP)) == NULL)
    {
        WriterClose(writer);
        return (FALSE);
    }

    writer->wr_IFF->iff_Stream = (ULONG)writer->wr_Clip;

    InitIFFasClip(writer->wr_IFF);

    if (OpenIFF(writer->wr_IFF, IFFF_WRITE) != 0)
    {
        WriterClose(writer);
        return (FALSE);
    }

    writer->wr_Nested = TRUE;

    if (PushChunk(writer->wr_IFF, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) != 0)
    {
        WriterClose(writer);
        return (FALSE);
    }

    writer->wr_Chunks++;

    if (PushChunk(writer->wr_IFF, 0, ID_CHRS, IFFSIZE_UNKNOWN) != 0)
    {
        WriterClose(writer);
        return (FALSE);
    }

    writer->wr_Chunks++;

    return (TRUE);
}

/******************************************************************************
 *
 * WriterOpenFile()
 *
 ******************************************************************************/

BOOL WriterOpenFile(struct Writer * writer, CONST_STRPTR path)
{
    WriterClear(writer);

    if (path == NULL)
    {
        return (FALSE);
    }

    writer->wr_File    = Open(path, MODE_NEWFILE);
    writer->wr_OwnFile = TRUE;

    return ((BOOL)(writer->wr_File != 0));
}

/******************************************************************************
 *
 * WriterOpenOutput()
 *
 * The process's own standard output, exactly as the Shell set it up: a
 * plain window when run interactively, or whatever file a ">" redirection
 * points at. Never closed by WriterClose(): it belongs to the process, not
 * to this writer.
 *
 ******************************************************************************/

BOOL WriterOpenOutput(struct Writer * writer)
{
    WriterClear(writer);

    writer->wr_File = Output();

    return ((BOOL)(writer->wr_File != 0));
}

/******************************************************************************
 *
 * WriterWrite()
 *
 * One piece of the text. A writer which has already failed swallows the
 * rest without a word: the caller writes everything and asks once, at the
 * end.
 *
 ******************************************************************************/

VOID WriterWrite(struct Writer * writer, CONST_STRPTR data, LONG length)
{
    if (writer->wr_Error || (data == NULL) || (length <= 0))
    {
        return;
    }

    if (writer->wr_IFF != NULL)
    {
        if (WriteChunkBytes(writer->wr_IFF, (APTR)data, length) != length)
        {
            writer->wr_Error = TRUE;
        }

        return;
    }

    if (writer->wr_File)
    {
        if (Write(writer->wr_File, (APTR)data, length) != length)
        {
            writer->wr_Error = TRUE;
        }

        return;
    }

    writer->wr_Error = TRUE; /* Nothing open to write to */
}

/******************************************************************************
 *
 * WriterClose()
 *
 * Give everything back, innermost first, and tell whether the whole text
 * went through. Safe on a writer which never opened, and on one already
 * closed: it leaves an empty writer behind.
 *
 ******************************************************************************/

BOOL WriterClose(struct Writer * writer)
{
    BOOL ok = (BOOL)(writer->wr_Error == FALSE);

    while (writer->wr_Chunks > 0)
    {
        if (PopChunk(writer->wr_IFF) != 0)
        {
            ok = FALSE;
        }

        writer->wr_Chunks--;
    }

    if (writer->wr_Nested)
    {
        CloseIFF(writer->wr_IFF);
        writer->wr_Nested = FALSE;
    }

    if (writer->wr_Clip != NULL)
    {
        CloseClipboard(writer->wr_Clip);
        writer->wr_Clip = NULL;
    }

    if (writer->wr_IFF != NULL)
    {
        FreeIFF(writer->wr_IFF);
        writer->wr_IFF = NULL;
    }

    if (writer->wr_File)
    {
        if (writer->wr_OwnFile)
        {
            Close(writer->wr_File);
        }

        writer->wr_File = 0;
    }

    return (ok);
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
