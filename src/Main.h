/******************************************************************************
 *
 * Main.h
 *
 ******************************************************************************/

#include <exec/types.h>

/******************************************************************************
 *
 * MUI
 *
 ******************************************************************************/

#ifndef IPTR
  #define IPTR ULONG
#endif

#ifndef MAKE_ID
  #define MAKE_ID(a,b,c,d) ((ULONG) (a)<<24 | (ULONG) (b)<<16 | (ULONG) (c)<<8 | (ULONG) (d))
#endif

/* NList's own image-embedding preparse code ("ESC o[n]"), alongside
   MUIX_B/MUIX_N/etc.: mui.h has no macro for it since it is not a core MUI
   text style, but a NList.mcc extension */

#ifndef MUIX_O
  #define MUIX_O "\033o"
#endif

/******************************************************************************
 *
 * APPLICATION
 *
 ******************************************************************************/

#define APP_NAME        "Emu68DeviceTree"
#define APP_BASE        "Emu68DeviceTree"
#define APP_DATE        "18.9.2026"
#define APP_VERSION     "1.0.1"
#define APP_VERSTRING   APP_NAME " " APP_VERSION " (" APP_DATE ")"
#define APP_AUTHORS     "Philippe CARPENTIER"
#define APP_COPYRIGHT   "Written by " APP_AUTHORS
#define APP_DESCRIPTION "Display the PiStorm/Emu68 Device Tree"
#define APP_HELPFILE    "PROGDIR:Emu68DeviceTree.guide"
#define APP_TITLE       APP_NAME " (%lu entries)"
#define INFO_TITLE      "%.110s (%s, %s)"

/******************************************************************************
 *
 * LIMITS AND DEFAULTS
 *
 ******************************************************************************/

#define MAX_PATHNAME        (1024)

#define WIN_MAIN_WIDTH      (581)
#define WIN_MAIN_HEIGHT     (355)
#define WIN_INFO_WIDTH      (800)
#define WIN_INFO_HEIGHT     (422)

#define INSPECT_ROWS        (9)
#define INSPECT_CELL_SIZE   (72)
#define INSPECT_FORMAT      "W=0 BAR,W=100"

#define BASE_BINARY         (0)
#define BASE_OCTAL          (1)
#define BASE_DECIMAL        (2)
#define BASE_HEXADECIMAL    (3)

#define HEX_SHORT_ADDRESS   (0x10000UL)
#define HEX_ADDRESS_CHARS   (4)

/******************************************************************************
 *
 * COLUMNS
 *
 ******************************************************************************/

#define SORT_BY_NAME        (0)
#define SORT_BY_TYPE        (1)
#define SORT_BY_LENGTH      (2)
#define SORT_COLUMN_COUNT   (3)

#define TREE_FORMAT         "W=100 BAR,W=0 BAR,W=0"

/* How deep a walk up on_parent (DumpNodePath(), in Dump.c) or up the
   NListtree (BuildPath(), in GUI.c) is ever expected to go */

#define MAX_TREE_DEPTH      (64)

/******************************************************************************
 *
 * GUI
 *
 ******************************************************************************/

#define MakeButton(label, help) \
    (TextObject,\
        ButtonFrame,\
        MUIA_Font, MUIV_Font_Button,\
        MUIA_Text_Contents, label,\
        MUIA_ShortHelp, help, \
        MUIA_Text_PreParse, MUIX_C,\
        MUIA_InputMode, MUIV_InputMode_RelVerify,\
        MUIA_Background, MUII_ButtonBack,\
    End)

/******************************************************************************
 *
 * EVENTS
 *
 ******************************************************************************/

typedef enum
{
    EVENT_RELOAD = 100,
    EVENT_ABOUT,
    EVENT_ABOUT_MUI,
    EVENT_QUIT,
    EVENT_INSPECT,
    EVENT_COPY,
    EVENT_COPYCELL,
    EVENT_SAVE,
    EVENT_FIRST,
    EVENT_PREV,
    EVENT_NEXT,
    EVENT_LAST,
    EVENT_ACTIVE,
    EVENT_NODEINFO,
    EVENT_TITLECLICK,
    EVENT_SAVENODE,
    EVENT_SEARCH,
    EVENT_TOGGLESEARCH,
    EVENT_FULLNAMES,
    EVENT_EXPAND,
    EVENT_COLLAPSE,
    EVENT_MUISETTINGS,
    EVENT_ICONIFY

} ProcessEvent_t;

/******************************************************************************
 *
 * OBJECTS
 *
 ******************************************************************************/

typedef struct ObjApp
{
    APTR App;

    /* Menus */

    APTR MN_Main;
    APTR MI_FullNames;
    APTR MI_ShowSearch;

    /* Main Window */

    APTR WI_Main;
    APTR LV_Tree;
    APTR TR_Tree;
    APTR GR_Main;
    APTR GR_Search;
    APTR ST_Search;

    /* Property Window */

    APTR WI_Info;
    APTR GR_Hex;
    APTR OB_HexDump;
    APTR OB_HexBar;
    APTR BT_First;
    APTR BT_Prev;
    APTR BT_Next;
    APTR BT_Last;
    APTR LV_Inspect;
    APTR NL_Inspect;
    APTR CY_Endian;
    APTR CY_Base;
    APTR BT_InfoCopy;
    APTR BT_InfoSave;
    APTR BT_InfoClose;

} ObjApp_t;

/******************************************************************************
 *
 * GLOBALS
 *
 * Owned by Emu68DeviceTree.c, read (and, for DTBase, indirectly reached
 * through) by GUI.c and Dump.c.
 *
 ******************************************************************************/

extern struct DeviceTreeBase * DTBase;
extern ObjApp_t *               appMain;

/******************************************************************************
 *
 * PROTOTYPES
 *
 ******************************************************************************/

BOOL OpenDeviceTree(VOID);

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
