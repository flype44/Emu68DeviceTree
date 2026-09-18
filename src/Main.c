/******************************************************************************
 *
 * Program:  Main.c
 * Purpose:  Display the PiStorm/Emu68 Device Tree
 * Authors:  Philippe CARPENTIER
 * Target:   AmigaOS 3.x
 * Compiler: SAS/C 6.59, Strict ANSI C89, NDK 3.2 includes
 * Requires: MUI 3.8, MCC_NList, MCC_NListview, MCC_NListtree, MCC_HexEdit
 *
 ******************************************************************************/

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <exec/exec.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include "Utils.h"
#include "Clipboard.h"
#include "DeviceTree.h"
#include "Main.h"
#include "Dump.h"
#include "GUI.h"

/******************************************************************************
 *
 * SAS/C
 *
 ******************************************************************************/

#ifdef __SASC
  ULONG CXBRK(VOID) { return (0); }
  ULONG _CXBRK(VOID) { return (0); }
  VOID  chkabort(VOID) { }
  LONG  __stack = 32768;
#endif

/******************************************************************************
 *
 * PROTOTYPES
 *
 ******************************************************************************/

STATIC VOID  DoHelp(VOID);
STATIC ULONG RunGUI(VOID);

/******************************************************************************
 *
 * EXTERNS
 *
 ******************************************************************************/

extern struct ExecBase * SysBase;

/******************************************************************************
 *
 * GLOBALS
 *
 ******************************************************************************/

struct DeviceTreeBase * DTBase = NULL;

ObjApp_t * appMain = NULL;

/******************************************************************************
 *
 * OpenDeviceTree()
 *
 * Just the resource, nothing MUI: what the headless SEARCH=/NODE= command
 * line mode needs and nothing more (see main()), and the first thing
 * OpenLibs() (GUI.c) opens for the windowed path too.
 *
 ******************************************************************************/

BOOL OpenDeviceTree(VOID)
{
    if (!(DTBase = (struct DeviceTreeBase *)OpenResource((CONST_STRPTR)DEVICETREE_NAME)))
    {
        PutStr("Failed to open " DEVICETREE_NAME ".\n");
        return (FALSE);
    }

    if (!DTBase->dt_Root)
    {
        PutStr("Failed to open " DEVICETREE_NAME ".\n");
        return (FALSE);
    }

    return (TRUE);
}

/******************************************************************************
 *
 * ENTRY POINT
 *
 ******************************************************************************/

#define ARGS_TEMPLATE (CONST_STRPTR)"SEARCH/K,NODE/K,HELP/S"

typedef enum { ARG_SEARCH, ARG_NODE, ARG_HELP, ARG_COUNT } Arg_t;

/******************************************************************************
 *
 * DoHelp()
 *
 * What "Emu68DeviceTree ?" leads to: it shows the template above, whoever
 * is reading it types HELP out of curiosity, and lands here. No window, no
 * MUI, same as SEARCH=/NODE=.
 *
 ******************************************************************************/

STATIC VOID DoHelp(VOID)
{
    PutStr(APP_VERSTRING "\n" APP_DESCRIPTION ".\n\n");
    PutStr("Emu68DeviceTree [SEARCH=<text>] [NODE=<path>] [HELP]\n\n");
    PutStr("From Workbench   Open the usual MUI browser window.\n");
    PutStr("No argument      Print this text (a Shell never gets a window\n");
    PutStr("                 it did not ask for).\n");
    PutStr("SEARCH=<text>    Print the tree filtered to <text> (case insensitive,\n");
    PutStr("                 substring, with full paths) to standard output, then\n");
    PutStr("                 quit. No window, no MUI. Ignored if NODE is given.\n");
    PutStr("NODE=<path>      Print that one node or property, and everything below\n");
    PutStr("                 it, to standard output, then quit. No window, no MUI.\n");
    PutStr("HELP             Show this text, then quit.\n\n");
    PutStr("Examples:\n");
    PutStr("  Emu68DeviceTree SEARCH=watchdog\n");
    PutStr("  Emu68DeviceTree NODE=\"/soc/watchdog@7e100000/\" >RAM:watchdog.txt\n");
}

/******************************************************************************
 *
 * RunGUI()
 *
 * The normal, windowed program: open the libraries, build and run the MUI
 * application, tear it down. Only ever reached from a Workbench launch
 * (see main()): a Shell gets SEARCH=/NODE= or DoHelp() instead, never a
 * window it did not ask for.
 *
 ******************************************************************************/

STATIC ULONG RunGUI(VOID)
{
    ULONG result = RETURN_FAIL;

    if (OpenLibs())
    {
        result = RETURN_WARN;

        if ((appMain = CreateApp()) != NULL)
        {
            ProcessEvents();
            DisposeApp(appMain);
            result = RETURN_OK;
        }
        else
        {
            PutStr("Failed to create MUI application.\n");
        }

        CloseLibs();
    }

    return (result);
}

/******************************************************************************
 *
 * Entry Point
 *
 ******************************************************************************/

ULONG main(VOID)
{
    ULONG result;
    LONG args[ARG_COUNT];
    struct RDArgs * rdArgs;
    struct Process * process = (struct Process *)SysBase->ThisTask;

    if (process->pr_CLI == 0)
    {
        /* No CommandLineInterface attached to this process: started from
           Workbench (icon double-click), not a Shell. The GUI is
           Workbench-only; a Shell gets SEARCH=/NODE= or, failing that, the
           same help a plain HELP would give (see below) -- never a window
           it did not ask for. */

        return (RunGUI());
    }

    args[ARG_SEARCH] = 0;
    args[ARG_NODE]   = 0;
    args[ARG_HELP]   = FALSE;

    if (!(rdArgs = ReadArgs(ARGS_TEMPLATE, args, NULL)))
    {
        PrintFault(IoErr(), (CONST_STRPTR)APP_NAME);
        return (RETURN_FAIL);
    }

    if ((args[ARG_SEARCH] != 0) || (args[ARG_NODE] != 0))
    {
        result = RETURN_FAIL;

        if (OpenDeviceTree())
        {
            result = DoExport((CONST_STRPTR)args[ARG_NODE],
                (CONST_STRPTR)args[ARG_SEARCH]) ? RETURN_OK : RETURN_WARN;
        }
    }
    else
    {
        /* HELP, or nothing at all: either way, print the same usage text
           rather than opening a window from a Shell that did not ask for
           one. */

        DoHelp();
        result = RETURN_OK;
    }

    FreeArgs(rdArgs);

    return (result);
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
