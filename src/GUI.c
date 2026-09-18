/******************************************************************************
 *
 * GUI.c
 *
 * The MUI application: object tree, menus, hooks and the main event loop.
 * See GUI.h.
 *
 ******************************************************************************/

#include <dos/dos.h>
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <utility/utility.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/muimaster.h>

#include <mui/NListview_mcc.h>
#include <mui/NListtree_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/HexEdit_mcc.h>

#include "Utils.h"
#include "Clipboard.h"
#include "DeviceTree.h"
#include "Main.h"
#include "Dump.h"
#include "GUI.h"

extern struct ExecBase * SysBase;
extern struct DosLibrary * DOSBase;
extern struct IntuitionBase * IntuitionBase;
extern struct Library * UtilityBase;

/******************************************************************************
 *
 * PROTOTYPES
 *
 ******************************************************************************/

/* Tree building and sorting */

typedef LONG (*CompareFunc_t)(APTR item1, APTR item2);

STATIC VOID  InsertionSort(APTR * array, ULONG count, CompareFunc_t compare);
STATIC LONG  CompareProperties(APTR item1, APTR item2);
STATIC LONG  CompareNodes(APTR item1, APTR item2);
STATIC VOID  InsertProperty(APTR tree, of_property_t * prop, struct MUI_NListtree_TreeNode * parent);
STATIC VOID  InsertProperties(APTR tree, of_property_t * prop, struct MUI_NListtree_TreeNode * parent);
STATIC VOID  InsertNode(APTR tree, of_node_t * node, struct MUI_NListtree_TreeNode * parent);
STATIC VOID  InsertNodes(APTR tree, of_node_t * node, struct MUI_NListtree_TreeNode * parent);
STATIC ULONG BuildTree(ObjApp_t * object, struct DeviceTreeBase * base);
STATIC VOID  BuildPath(ObjApp_t * object, struct MUI_NListtree_TreeNode * tn, STRPTR buffer, LONG size);

/* Display */

STATIC VOID  SetMainTitle(ObjApp_t * object, ULONG count);
STATIC VOID  UseTypeImages(ObjApp_t * object);
STATIC VOID  UpdateTitleMark(ObjApp_t * object);
STATIC VOID  SetSortColumn(ObjApp_t * object, ULONG column, BOOL toggle);
STATIC VOID  SetSortReverse(ObjApp_t * object, BOOL reverse);

/* Information window */

STATIC APTR  MakeHexEdit(CONST_STRPTR bytes, ULONG length);
STATIC APTR  MakePlaceholder(CONST_STRPTR text);
STATIC VOID  ShowHexDump(ObjApp_t * object, of_property_t * prop);
STATIC VOID  FormatInspect(CONST_STRPTR bytes, ULONG avail, UWORD width, BOOL isSigned, BOOL isEpoch, STRPTR buffer, LONG size);
STATIC VOID  UpdateInspector(ObjApp_t * object);
STATIC VOID  FillInspector(ObjApp_t * object);
STATIC VOID  FillNodeInfo(ObjApp_t * object);
STATIC VOID  WriteValue(struct Writer * writer, CONST_STRPTR text);

/* Events */

STATIC VOID  ReportFailure(ObjApp_t * object, APTR window, CONST_STRPTR text);
STATIC BOOL  PickSavePath(CONST_STRPTR title, CONST_STRPTR fileName, STRPTR path, LONG size);
STATIC VOID  DoAbout(VOID);
STATIC VOID  DoAboutMUI(VOID);
STATIC VOID  DoActive(ObjApp_t * object);
STATIC VOID  DoCopy(ObjApp_t * object, CONST_STRPTR text);
STATIC VOID  DoCursor(ObjApp_t * object, ULONG where);
STATIC VOID  DoNodeInfo(ObjApp_t * object);
STATIC VOID  DoReload(ObjApp_t * object);
STATIC VOID  DoSearch(ObjApp_t * object);
STATIC VOID  DoToggleSearch(ObjApp_t * object);
STATIC VOID  DoSave(ObjApp_t * object);
STATIC VOID  DoSaveNode(ObjApp_t * object);
STATIC VOID  DoInspect(VOID);
STATIC VOID  DoCopyCell(VOID);
STATIC VOID  DoTitleClick(VOID);

/******************************************************************************
 *
 * HOOKS
 *
 ******************************************************************************/

STATIC SAVEDS ASM ULONG DisplayFunc(
    REG(a0) struct Hook * hook,
    REG(a2) Object * obj,
    REG(a1) struct MUIP_NListtree_DisplayMessage * msg);

STATIC SAVEDS ASM ULONG InspectFunc(
    REG(a0) struct Hook * hook,
    REG(a2) Object * obj,
    REG(a1) struct MUIP_NListtree_DisplayMessage * msg);

STATIC SAVEDS ASM ULONG HexDispatcher(
    REG(a0) struct IClass * cl,
    REG(a2) Object * obj,
    REG(a1) Msg msg);

/******************************************************************************
 *
 * GLOBALS
 *
 ******************************************************************************/

/* Not STATIC: proto/muimaster.h itself declares "extern struct Library *
   MUIMasterBase" by this exact name, for MUI's own auto-open machinery */

struct Library * MUIMasterBase = NULL;

STATIC struct MUI_CustomClass * HexClass = NULL;
STATIC struct Hook DisplayHook;
STATIC struct Hook InspectHook;
STATIC ULONG entryCount = 0;

/* Two alternating title buffers: Intuition keeps the pointer we hand over,
   and alternating guarantees MUI sees a changed value on every update. */

STATIC UBYTE titleBuffer[2][64];
STATIC UWORD titleIndex = 0;

/* Property currently dumped, and the address the inspector last worked on,
   NULL when there is nothing to inspect. A pointer is enough to catch both
   a cursor which moved and an entry which changed. */

STATIC of_property_t * shownProp = NULL;
STATIC CONST_STRPTR inspectBytes = NULL;

STATIC UBYTE inspectBuffer[INSPECT_ROWS][INSPECT_CELL_SIZE];

/* Byte order and base the inspector reads with, following the two cycle
   gadgets below its list */

STATIC BOOL  inspectLittle = FALSE;
STATIC ULONG inspectBase = BASE_DECIMAL;

STATIC STRPTR endianEntries[] =
{
    (STRPTR)"Big Endian",
    (STRPTR)"Little Endian",
    NULL
};

STATIC STRPTR baseEntries[] =
{
    (STRPTR)"Binary",
    (STRPTR)"Octal",
    (STRPTR)"Decimal",
    (STRPTR)"Hexadecimal",
    NULL
};

/* The Data Inspector rows, in display order */

STATIC const struct InspectRow
{
    CONST_STRPTR name;
    UWORD        width;
    BOOL         isSigned;
    BOOL         isEpoch;   /* Seconds since 1970, not a number to read */
}

inspectRows[INSPECT_ROWS] =
{
    { (CONST_STRPTR)"SInt08", 1, TRUE,  FALSE },
    { (CONST_STRPTR)"UInt08", 1, FALSE, FALSE },
    { (CONST_STRPTR)"SInt16", 2, TRUE,  FALSE },
    { (CONST_STRPTR)"UInt16", 2, FALSE, FALSE },
    { (CONST_STRPTR)"SInt32", 4, TRUE,  FALSE },
    { (CONST_STRPTR)"UInt32", 4, FALSE, FALSE },
    { (CONST_STRPTR)"SInt64", 8, TRUE,  FALSE },
    { (CONST_STRPTR)"UInt64", 8, FALSE, FALSE },
    { (CONST_STRPTR)"Epoch.", 4, FALSE, TRUE  }
};

/* Entry currently described by the information window, so that repeated
   notifications do not rebuild it for nothing. Only ever compared, never
   dereferenced, and reset whenever the tree is rebuilt. */

STATIC struct MUI_NListtree_TreeNode * shownEntry = NULL;

/* The information window displays MUI text objects, which do NOT copy their
   contents: these buffers must stay valid as long as the window is open. */

STATIC UBYTE infoTitleBuffer[160];
STATIC UBYTE infoPathBuffer[MAX_PATHNAME];
STATIC UBYTE infoTypeBuffer[32];
STATIC UBYTE infoLengthBuffer[32];

/* Current sort order, applied while the tree is being built */

STATIC ULONG sortColumn  = SORT_BY_NAME;
STATIC BOOL  sortReverse = FALSE;

/* Column names, indexed by SORT_BY_#? */

STATIC CONST_STRPTR columnNames[SORT_COLUMN_COUNT] =
{
    (CONST_STRPTR)"Name",
    (CONST_STRPTR)"Type",
    (CONST_STRPTR)"Length"
};

/* The drawer last confirmed in the ASL save requester, shared between
   DoSave() and DoSaveNode(): a folder picked for one is offered again for
   the other */

STATIC UBYTE exportDrawer[MAX_PATHNAME] = "RAM:";

/******************************************************************************
 *
 * ReportFailure()
 *
 ******************************************************************************/

STATIC VOID ReportFailure(ObjApp_t * object, APTR window, CONST_STRPTR text)
{
    MUI_Request(object->App, window, 0, (STRPTR)APP_NAME, (STRPTR)"*_Ok", (STRPTR)text);
}

/******************************************************************************
 *
 * PickSavePath()
 *
 * The ASL save requester shared by DoSave()/DoSaveNode(): 'path' is filled
 * with the confirmed drawer and file name, and exportDrawer is updated so
 * the next save (whichever of the two) offers the same folder again.
 *
 ******************************************************************************/

STATIC BOOL PickSavePath(CONST_STRPTR title, CONST_STRPTR fileName, STRPTR path, LONG size)
{
    struct FileRequester * request;
    BOOL confirmed = FALSE;

    request = (struct FileRequester *)MUI_AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText,     (IPTR)title,
        ASLFR_DoSaveMode,    TRUE,
        ASLFR_InitialDrawer, (IPTR)exportDrawer,
        ASLFR_InitialFile,   (IPTR)fileName,
        TAG_DONE);

    if (request == NULL)
    {
        return (FALSE);
    }

    if (MUI_AslRequestTags(request, TAG_DONE))
    {
        StringCopy(exportDrawer, (CONST_STRPTR)request->fr_Drawer, (LONG)sizeof(exportDrawer));
        StringCopy(path, (CONST_STRPTR)request->fr_Drawer, size);

        confirmed = AddPart(path, (CONST_STRPTR)request->fr_File, size) ? TRUE : FALSE;
    }

    MUI_FreeAslRequest(request);

    return (confirmed);
}

/******************************************************************************
 *
 * DoAbout()
 *
 ******************************************************************************/

STATIC VOID DoAbout(VOID)
{
    MUI_Request(appMain->App, appMain->WI_Main, 0,
        (STRPTR)"About " APP_NAME, (STRPTR)"*_Ok",
        (STRPTR)"\n" MUIX_C MUIX_B APP_VERSTRING MUIX_N "\n\n"
        MUIX_PH APP_DESCRIPTION MUIX_PT "\n\n" APP_COPYRIGHT "\n\n"
        "This a MUI application\nMUI is copyrighted by Stefan Stuntz\n");
}

/******************************************************************************
 *
 * DoAboutMUI()
 *
 ******************************************************************************/

STATIC VOID DoAboutMUI(VOID)
{
    DoMethod(appMain->App, MUIM_Application_AboutMUI, (IPTR)appMain->WI_Main);
}

/******************************************************************************
 *
 * DoReload()
 *
 * Clears any active search first: a reload always shows the whole tree.
 *
 ******************************************************************************/

STATIC VOID DoReload(ObjApp_t * object)
{
    searchFilter[0] = '\0';

    set(object->ST_Search, MUIA_String_Contents, (IPTR)"");

    SetMainTitle(object, BuildTree(object, DTBase));
}

/******************************************************************************
 *
 * DoSearch()
 *
 ******************************************************************************/

STATIC VOID DoSearch(ObjApp_t * object)
{
    CONST_STRPTR text = NULL;

    get(object->ST_Search, MUIA_String_Contents, &text);

    StringCopy(searchFilter, text, (LONG)sizeof(searchFilter));

    SetMainTitle(object, BuildTree(object, DTBase));
}

/******************************************************************************
 *
 * DoToggleSearch()
 *
 * GR_Search is removed from/re-added to GR_Main rather than toggling
 * MUIA_ShowMe on it: a plain ShowMe left a stray focus border behind (a
 * known MUI rough edge). OM_REMMEMBER/OM_ADDMEMBER under InitChange/
 * ExitChange forces a full redraw instead, same as ShowHexDump() below.
 *
 ******************************************************************************/

STATIC VOID DoToggleSearch(ObjApp_t * object)
{
    ULONG checked = FALSE;

    get(object->MI_ShowSearch, MUIA_Menuitem_Checked, &checked);

    DoMethod(object->GR_Main, MUIM_Group_InitChange);

    if (checked)
    {
        DoMethod(object->GR_Main, OM_ADDMEMBER, (IPTR)object->GR_Search);
    }
    else
    {
        DoMethod(object->GR_Main, OM_REMMEMBER, (IPTR)object->GR_Search);
    }

    DoMethod(object->GR_Main, MUIM_Group_ExitChange);

    set(object->WI_Main, MUIA_Window_ActiveObject,
        (IPTR)(checked ? object->ST_Search : object->TR_Tree));
}

/******************************************************************************
 *
 * DisplayFunc()
 *
 ******************************************************************************/

STATIC SAVEDS ASM ULONG DisplayFunc(
    REG(a0) struct Hook * hook,
    REG(a2) Object * obj,
    REG(a1) struct MUIP_NListtree_DisplayMessage * msg)
{
    STATIC BYTE nameBuffer[160];
    STATIC BYTE typeBuffer[24];
    STATIC BYTE lengthBuffer[16];

    (void)hook;
    (void)obj;

    if (msg->TreeNode != NULL)
    {
        STRPTR typeText = (STRPTR)typeBuffer;
        ULONG type = ENTRY_TYPE_NODE;
        STRPTR name = msg->TreeNode->tn_Name;

        if (name == NULL)
        {
            name = (STRPTR)"";
        }

        if (msg->TreeNode->tn_Flags & TNF_LIST)
        {
            of_node_t * node = (of_node_t *)msg->TreeNode->tn_User;
            typeText = (STRPTR)typeNames[ENTRY_TYPE_NODE];
            SPrintf(lengthBuffer, sizeof(lengthBuffer),
                (CONST_STRPTR)MUIX_R "%lu", CountSubItems(node));
        }
        else
        {
            of_property_t * prop = (of_property_t *)msg->TreeNode->tn_User;
            type = EntryType(prop);
            FormatType(prop, typeBuffer, sizeof(typeBuffer));
            SPrintf(lengthBuffer, sizeof(lengthBuffer), (CONST_STRPTR)MUIX_R "%lu",
                (prop != NULL) ? prop->op_length : 0);
        }

        if (msg->TreeNode->tn_Flags & TNF_LIST)
        {
            *msg->Array++ = name;
        }
        else
        {
            SPrintf(nameBuffer, sizeof(nameBuffer),
                (CONST_STRPTR)MUIX_O "[%lu] %s", type, (IPTR)name);
            *msg->Array++ = nameBuffer;
        }

        *msg->Array++ = typeText;
        *msg->Array++ = lengthBuffer;
    }
    else
    {
        *msg->Array++ = (STRPTR)columnNames[SORT_BY_NAME];
        *msg->Array++ = (STRPTR)columnNames[SORT_BY_TYPE];
        *msg->Array++ = (STRPTR)columnNames[SORT_BY_LENGTH];
        *msg->Preparse++ = (STRPTR)MUIX_B;
        *msg->Preparse++ = (STRPTR)MUIX_B;
        *msg->Preparse++ = (STRPTR)MUIX_B MUIX_R;
    }

    return (0);
}

/******************************************************************************
 *
 * InsertionSort()
 *
 * Stable, small-N sort shared by the property and node arrays below.
 *
 ******************************************************************************/

STATIC VOID InsertionSort(APTR * array, ULONG count, CompareFunc_t compare)
{
    APTR temp;
    ULONG i;
    LONG j;

    for (i = 1; i < count; i++)
    {
        temp = array[i];

        for (j = (LONG)i - 1; j >= 0; j--)
        {
            if (compare(array[j], temp) <= 0)
            {
                break;
            }

            array[j + 1] = array[j];
        }

        array[j + 1] = temp;
    }
}

/******************************************************************************
 *
 * CompareProperties()
 *
 ******************************************************************************/

STATIC LONG CompareProperties(APTR item1, APTR item2)
{
    of_property_t * prop1 = (of_property_t *)item1;
    of_property_t * prop2 = (of_property_t *)item2;
    LONG result = 0;

    switch (sortColumn)
    {
    case SORT_BY_TYPE:
        {
            ULONG type1 = EntryType(prop1);
            ULONG type2 = EntryType(prop2);

            if (type1 != type2)
            {
                result = (type1 < type2) ? -1 : 1;
            }
            else if ((type1 == ENTRY_TYPE_BLOB) &&
                     (prop1->op_length != prop2->op_length))
            {
                result = (prop1->op_length < prop2->op_length) ? -1 : 1;
            }
        }
        break;

    case SORT_BY_LENGTH:
        if (prop1->op_length != prop2->op_length)
        {
            result = (prop1->op_length < prop2->op_length) ? -1 : 1;
        }
        break;

    default:
        break;
    }

    if (result == 0)
    {
        result = StringCompare((CONST_STRPTR)prop1->op_name,
                               (CONST_STRPTR)prop2->op_name);
    }

    return (sortReverse ? -result : result);
}

/******************************************************************************
 *
 * CompareNodes()
 *
 ******************************************************************************/

STATIC LONG CompareNodes(APTR item1, APTR item2)
{
    of_node_t * node1 = (of_node_t *)item1;
    of_node_t * node2 = (of_node_t *)item2;
    LONG result = 0;

    if (sortColumn == SORT_BY_LENGTH)
    {
        ULONG count1 = CountSubItems(node1);
        ULONG count2 = CountSubItems(node2);

        if (count1 != count2)
        {
            result = (count1 < count2) ? -1 : 1;
        }
    }

    if (result == 0)
    {
        result = StringCompare((CONST_STRPTR)node1->on_name,
                               (CONST_STRPTR)node2->on_name);
    }

    return (sortReverse ? -result : result);
}

/******************************************************************************
 *
 * InsertProperty()
 *
 ******************************************************************************/

STATIC VOID InsertProperty(
    APTR tree,
    of_property_t * prop,
    struct MUI_NListtree_TreeNode * parent)
{
    DoMethod(tree, MUIM_NListtree_Insert,
        (IPTR)prop->op_name, /* name      */
        (IPTR)prop,          /* user data */
        (IPTR)parent,        /* list node */
        MUIV_NListtree_Insert_PrevNode_Tail,
        0);                  /* leaf      */

    entryCount++;
}

/******************************************************************************
 *
 * InsertProperties()
 *
 ******************************************************************************/

STATIC VOID InsertProperties(
    APTR tree,
    of_property_t * prop,
    struct MUI_NListtree_TreeNode * parent)
{
    of_property_t ** array;
    of_property_t * p;
    ULONG count = 0;
    ULONG i;

    for (p = prop; p != NULL; p = p->op_next)
    {
        if (PropertyMatchesFilter(p))
        {
            count++;
        }
    }

    if (count == 0)
    {
        return;
    }

    array = (of_property_t **)AllocVec(
        (ULONG)(count * sizeof(of_property_t *)), MEMF_PUBLIC | MEMF_CLEAR);

    if (array == NULL)
    {
        for (p = prop; p != NULL; p = p->op_next)
        {
            if (PropertyMatchesFilter(p))
            {
                InsertProperty(tree, p, parent);
            }
        }

        return;
    }

    for (i = 0, p = prop; (i < count) && (p != NULL); p = p->op_next)
    {
        if (PropertyMatchesFilter(p))
        {
            array[i++] = p;
        }
    }

    InsertionSort((APTR *)array, count, CompareProperties);

    for (i = 0; i < count; i++)
    {
        InsertProperty(tree, array[i], parent);
    }

    FreeVec(array);
}

/******************************************************************************
 *
 * InsertNode()
 *
 ******************************************************************************/

STATIC VOID InsertNode(
    APTR tree,
    of_node_t * node,
    struct MUI_NListtree_TreeNode * parent)
{
    struct MUI_NListtree_TreeNode * tn;
    CONST_STRPTR name = (CONST_STRPTR)node->on_name;

    if ((name == NULL) || (name[0] == '\0'))
    {
        name = (CONST_STRPTR)"/";
    }

    tn = (struct MUI_NListtree_TreeNode *)DoMethod(tree,
        MUIM_NListtree_Insert,
        (IPTR)name,    /* name      */
        (IPTR)node,    /* user data */
        (IPTR)parent,  /* list node */
        MUIV_NListtree_Insert_PrevNode_Tail,
        TNF_LIST);

    entryCount++;

    if (tn != NULL)
    {
        InsertProperties(tree, node->on_properties, tn);
        InsertNodes(tree, node->on_children, tn);
    }
}

/******************************************************************************
 *
 * InsertNodes()
 *
 ******************************************************************************/

STATIC VOID InsertNodes(
    APTR tree,
    of_node_t * node,
    struct MUI_NListtree_TreeNode * parent)
{
    of_node_t ** array;
    of_node_t * n;
    ULONG count = 0;
    ULONG i;

    for (n = node; n != NULL; n = n->on_next)
    {
        if (NodeMatchesFilter(n))
        {
            count++;
        }
    }

    if (count == 0)
    {
        return;
    }

    array = (of_node_t **)AllocVec(
        (ULONG)(count * sizeof(of_node_t *)), MEMF_PUBLIC | MEMF_CLEAR);

    if (array == NULL)
    {
        for (n = node; n != NULL; n = n->on_next)
        {
            if (NodeMatchesFilter(n))
            {
                InsertNode(tree, n, parent);
            }
        }

        return;
    }

    for (i = 0, n = node; (i < count) && (n != NULL); n = n->on_next)
    {
        if (NodeMatchesFilter(n))
        {
            array[i++] = n;
        }
    }

    InsertionSort((APTR *)array, count, CompareNodes);

    for (i = 0; i < count; i++)
    {
        n = array[i];
        array[i] = NULL;
        InsertNode(tree, n, parent);
    }

    FreeVec(array);
}

/******************************************************************************
 *
 * BuildTree()
 *
 ******************************************************************************/

STATIC ULONG BuildTree(ObjApp_t * object, struct DeviceTreeBase * base)
{
    entryCount = 0;

    shownEntry = NULL;

    set(object->TR_Tree, MUIA_NListtree_Quiet, TRUE);

    DoMethod(object->TR_Tree, MUIM_NListtree_Clear, NULL, 0);

    InsertNodes(object->TR_Tree, base->dt_Root,
        (struct MUI_NListtree_TreeNode *)MUIV_NListtree_Insert_ListNode_Root);

    /* A search result is small and only worth showing fully open: nothing
       left to prune away, unlike the full tree's default top level only */

    DoMethod(object->TR_Tree, MUIM_NListtree_Open,
        MUIV_NListtree_Open_ListNode_Root,
        (searchFilter[0] != '\0') ? MUIV_NListtree_Open_TreeNode_All
                                  : MUIV_NListtree_Open_TreeNode_Head, 0);

    set(object->TR_Tree, MUIA_NListtree_Quiet, FALSE);

    return (entryCount);
}

/******************************************************************************
 *
 * BuildPath()
 *
 ******************************************************************************/

STATIC VOID BuildPath(
    ObjApp_t * object,
    struct MUI_NListtree_TreeNode * tn,
    STRPTR buffer,
    LONG size)
{
    struct MUI_NListtree_TreeNode * stack[MAX_TREE_DEPTH];
    LONG depth = 0;
    LONG used = 0;
    LONG len;

    while (tn && (depth < MAX_TREE_DEPTH))
    {
        stack[depth++] = tn;

        tn = (struct MUI_NListtree_TreeNode *)DoMethod(object->TR_Tree,
            MUIM_NListtree_GetEntry, (IPTR)tn,
            MUIV_NListtree_GetEntry_Position_Parent, 0);
    }

    buffer[0] = '\0';

    while (depth--)
    {
        CONST_STRPTR name = (CONST_STRPTR)stack[depth]->tn_Name;

        if ((name == NULL) || (name[0] == '\0') ||
            ((name[0] == '/') && (name[1] == '\0')))
        {
            continue;
        }

        len = StringLength(name);

        if (used + len + 2 >= size)
        {
            break;
        }

        if ((used == 0) || (buffer[used - 1] != '/'))
        {
            buffer[used++] = '/';
        }

        CopyMem((APTR)name, (APTR)&buffer[used], (ULONG)len);

        used += len;
        buffer[used] = '\0';
    }

    if (used == 0)
    {
        buffer[0] = '/';
        buffer[1] = '\0';
    }
}

/******************************************************************************
 *
 * SetMainTitle()
 *
 ******************************************************************************/

STATIC VOID SetMainTitle(ObjApp_t * object, ULONG count)
{
    titleIndex ^= 1;

    SPrintf(titleBuffer[titleIndex], (LONG)sizeof(titleBuffer[titleIndex]),
        (CONST_STRPTR)APP_TITLE, count);

    set(object->WI_Main, MUIA_Window_Title, (IPTR)titleBuffer[titleIndex]);
}

/******************************************************************************
 *
 * UseTypeImages()
 *
 ******************************************************************************/

STATIC VOID UseTypeImages(ObjApp_t * object)
{
    ULONG i;

    for (i = ENTRY_TYPE_EMPTY; i < ENTRY_TYPE_COUNT; i++)
    {
        APTR image = ImageObject, MUIA_Image_Spec, (IPTR)MUII_Chip, End;

        if (image != NULL)
        {
            DoMethod(object->TR_Tree, MUIM_NList_UseImage, (IPTR)image, i, 0);
        }
    }

    {
        APTR image = ImageObject, MUIA_Image_Spec, (IPTR)MUII_Chip, End;

        if (image != NULL)
        {
            DoMethod(object->NL_Inspect, MUIM_NList_UseImage, (IPTR)image, 0, 0);
        }
    }
}

/******************************************************************************
 *
 * UpdateTitleMark()
 *
 ******************************************************************************/

STATIC VOID UpdateTitleMark(ObjApp_t * object)
{
    set(object->TR_Tree, MUIA_NList_TitleMark, (IPTR)(sortColumn |
        (sortReverse ? MUIV_NList_TitleMark_Up : MUIV_NList_TitleMark_Down)));
}

/******************************************************************************
 *
 * SetSortColumn()
 *
 ******************************************************************************/

STATIC VOID SetSortColumn(ObjApp_t * object, ULONG column, BOOL toggle)
{
    if (column >= SORT_COLUMN_COUNT)
    {
        return;
    }

    if (column == sortColumn)
    {
        if (!toggle)
        {
            return;
        }

        sortReverse = (BOOL)!sortReverse;
    }
    else
    {
        sortColumn = column;
    }

    UpdateTitleMark(object);
    DoReload(object);
}

/******************************************************************************
 *
 * SetSortReverse()
 *
 ******************************************************************************/

STATIC VOID SetSortReverse(ObjApp_t * object, BOOL reverse)
{
    if (reverse == sortReverse)
    {
        return;
    }

    sortReverse = reverse;

    UpdateTitleMark(object);
    DoReload(object);
}

/******************************************************************************
 *
 * HexDispatcher()
 *
 * A HexEdit subclass which never writes anything (read-only).
 *
 ******************************************************************************/

STATIC SAVEDS ASM ULONG HexDispatcher(
    REG(a0) struct IClass * cl,
    REG(a2) Object * obj,
    REG(a1) Msg msg)
{
    if (msg->MethodID == MUIM_HexEdit_WriteMemoryByte)
    {
        return (TRUE); /* Read only */
    }

    return (DoSuperMethodA(cl, obj, msg));
}

/******************************************************************************
 *
 * MakeHexEdit()
 *
 ******************************************************************************/

STATIC APTR MakeHexEdit(CONST_STRPTR bytes, ULONG length)
{
    if (HexClass == NULL)
    {
        return (NULL);
    }

    return ((APTR)NewObject(HexClass->mcc_Class, NULL,
        InputListFrame,
        MUIA_Font,                      MUIV_Font_Fixed,
        MUIA_CycleChain,                1,
        MUIA_ShortHelp,                 MUIX_B "Hexadecimal Dump" MUIX_N "\nDisplay the raw content of the Device Tree item.",
        MUIA_HexEdit_LowBound,          (IPTR)bytes,
        MUIA_HexEdit_HighBound,         (IPTR)(bytes + length - 1),
        MUIA_HexEdit_BaseAddressOffset, (IPTR)(-(LONG)bytes),
        MUIA_HexEdit_AddressChars,      (IPTR)((length > HEX_SHORT_ADDRESS) ? 8 : HEX_ADDRESS_CHARS),
        MUIA_HexEdit_SelectMode,        MUIV_HexEdit_SelectMode_Byte,
        MUIA_HexEdit_EditMode,          TRUE,
        TAG_DONE));
}

/******************************************************************************
 *
 * MakePlaceholder()
 *
 ******************************************************************************/

STATIC APTR MakePlaceholder(CONST_STRPTR text)
{
    return (VGroup,
        Child, VSpace(0),
        Child, TextObject,
            MUIA_Text_Contents, (IPTR)text,
            MUIA_Text_PreParse, (IPTR)MUIX_C,
            MUIA_Text_SetMax,   FALSE,
        End,
        Child, VSpace(0),
    End);
}

/******************************************************************************
 *
 * ShowHexDump()
 *
 * Replace the dump group's contents with a HexEdit object and its scrollbar,
 * built for the given property. Group members may only change between
 * MUIM_Group_InitChange/ExitChange, and an object must be removed from its
 * group before being disposed. A node, an empty property, or a missing
 * HexEdit.mcc fall back to a placeholder text: the group is never left
 * without a child.
 *
 ******************************************************************************/

STATIC VOID ShowHexDump(ObjApp_t * object, of_property_t * prop)
{
    CONST_STRPTR bytes = NULL;
    ULONG length = 0;
    APTR dump = NULL;
    APTR bar = NULL;

    if (prop != NULL)
    {
        bytes  = (CONST_STRPTR)prop->op_value;
        length = prop->op_length;
    }

    if (!DoMethod(object->GR_Hex, MUIM_Group_InitChange))
    {
        return;
    }

    /* Drop the objects built for the previously displayed entry */

    if (object->OB_HexDump != NULL)
    {
        DoMethod(object->GR_Hex, OM_REMMEMBER, (IPTR)object->OB_HexDump);
        MUI_DisposeObject(object->OB_HexDump);
        object->OB_HexDump = NULL;
    }

    if (object->OB_HexBar != NULL)
    {
        DoMethod(object->GR_Hex, OM_REMMEMBER, (IPTR)object->OB_HexBar);
        MUI_DisposeObject(object->OB_HexBar);
        object->OB_HexBar = NULL;
    }

    if ((bytes != NULL) && (length > 0))
    {
        if ((dump = MakeHexEdit(bytes, length)) != NULL)
        {
            bar = ScrollbarObject, End;

            if (bar == NULL)
            {
                MUI_DisposeObject(dump);
                dump = NULL;
            }
        }
    }

    if (dump != NULL)
    {
        DoMethod(object->GR_Hex, OM_ADDMEMBER, (IPTR)dump);
        DoMethod(object->GR_Hex, OM_ADDMEMBER, (IPTR)bar);

        /* HexEdit does all the scrolling house-keeping by itself */

        set(dump, MUIA_HexEdit_PropObject, (IPTR)bar);

        object->OB_HexDump = dump;
        object->OB_HexBar  = bar;
    }
    else
    {
        dump = MakePlaceholder((length == 0)
            ? (CONST_STRPTR)"No data."
            : (CONST_STRPTR)"HexEdit.mcc is not installed.");

        if (dump != NULL)
        {
            DoMethod(object->GR_Hex, OM_ADDMEMBER, (IPTR)dump);
            object->OB_HexDump = dump;
        }
    }

    DoMethod(object->GR_Hex, MUIM_Group_ExitChange);

    /* The dump is the only thing worth typing in, and it has just been
       built anew: whatever had the keyboard in that window is gone */

    if (object->OB_HexBar != NULL)
    {
        set(object->WI_Info, MUIA_Window_ActiveObject, (IPTR)object->OB_HexDump);
    }
}

/******************************************************************************
 *
 * FormatInspect()
 *
 * One cell of the data inspector: the 'width' bytes at 'bytes', read in the
 * chosen byte order and written in the chosen base. Accumulated into a pair
 * of longwords (all a 68000 has); in decimal, RawDoFmt() knowing no 64 bit
 * specifier, a quad prints while it still fits in 32 bits and falls back to
 * hexadecimal beyond. A dash means the property ends before the value does.
 *
 ******************************************************************************/

STATIC VOID FormatInspect(
    CONST_STRPTR bytes,
    ULONG avail,
    UWORD width,
    BOOL isSigned,
    BOOL isEpoch,
    STRPTR buffer,
    LONG size)
{
    ULONG high = 0;
    ULONG low = 0;
    UWORD i;

    if ((bytes == NULL) || (avail < (ULONG)width))
    {
        StringCopy(buffer, (CONST_STRPTR)"-", size);
        return;
    }

    for (i = 0; i < width; i++)
    {
        UBYTE byte = inspectLittle ? bytes[width - 1 - i] : bytes[i];

        high = (high << 8) | (low >> 24);
        low  = (low << 8) | (ULONG)byte;
    }

    /* A date is a date, whichever base the cycle is on */

    if (isEpoch)
    {
        FormatEpoch(low, buffer, size);
        return;
    }

    if (inspectBase == BASE_BINARY)
    {
        FormatBits(high, low, width, 1, TRUE, buffer, size);
        return;
    }

    if (inspectBase == BASE_OCTAL)
    {
        FormatBits(high, low, width, 3, TRUE, buffer, size);
        return;
    }

    if (inspectBase == BASE_HEXADECIMAL)
    {
        FormatBits(high, low, width, 4, FALSE, buffer, size);
        return;
    }

    if (width < 8)
    {
        if (isSigned && (low & (1UL << ((width * 8) - 1))))
        {
            /* Sign extension, skipped at 32 bits where the cast is enough */

            if (width < 4)
            {
                low |= (0xFFFFFFFFUL << (width * 8));
            }

            SPrintf(buffer, size, (CONST_STRPTR)"%ld", (LONG)low);
            return;
        }

        SPrintf(buffer, size, (CONST_STRPTR)"%lu", low);
        return;
    }

    if (isSigned)
    {
        if (((high == 0) && !(low & 0x80000000UL)) ||
            ((high == 0xFFFFFFFFUL) && (low & 0x80000000UL)))
        {
            SPrintf(buffer, size, (CONST_STRPTR)"%ld", (LONG)low);
            return;
        }
    }
    else if (high == 0)
    {
        SPrintf(buffer, size, (CONST_STRPTR)"%lu", low);
        return;
    }

    /* A quad beyond 32 bits, in hexadecimal for want of a decimal */

    FormatBits(high, low, width, 4, FALSE, buffer, size);
}

/******************************************************************************
 *
 * DoCursor()
 *
 ******************************************************************************/

STATIC VOID DoCursor(ObjApp_t * object, ULONG where)
{
    if ((object->OB_HexBar == NULL) || (shownProp == NULL))
    {
        return;
    }

    switch (where)
    {
    case EVENT_FIRST:
        set(object->OB_HexDump, MUIA_HexEdit_CursorAddress, 0);
        break;

    case EVENT_PREV:
        set(object->OB_HexDump, MUIA_HexEdit_MoveCursor,
            MUIV_HexEdit_MoveCursor_Left);
        break;

    case EVENT_NEXT:
        set(object->OB_HexDump, MUIA_HexEdit_MoveCursor,
            MUIV_HexEdit_MoveCursor_Right);
        break;

    case EVENT_LAST:
        set(object->OB_HexDump, MUIA_HexEdit_CursorAddress,
            (IPTR)(shownProp->op_length - 1));
        break;
    }

    set(object->WI_Info, MUIA_Window_ActiveObject, (IPTR)object->OB_HexDump);
}

/******************************************************************************
 *
 * InspectFunc()
 *
 * Display hook of the inspector: Type | Value. Built on NListtree rather
 * than NList only because this program already has a working display hook
 * of that shape; NListtree being a subclass of NList, columns/title/
 * separators look identical.
 *
 ******************************************************************************/

STATIC SAVEDS ASM ULONG InspectFunc(
    REG(a0) struct Hook * hook,
    REG(a2) Object * obj,
    REG(a1) struct MUIP_NListtree_DisplayMessage * msg)
{
    STATIC BYTE nameBuffer[32];

    (void)hook;
    (void)obj;

    if (msg->TreeNode != NULL)
    {
        const struct InspectRow * row =
            (const struct InspectRow *)msg->TreeNode->tn_User;

        LONG index;

        if (row == NULL)
        {
            return (0);
        }

        index = (LONG)(row - inspectRows);

        if ((index < 0) || (index >= INSPECT_ROWS))
        {
            return (0);
        }

        SPrintf(nameBuffer, sizeof(nameBuffer), (CONST_STRPTR)MUIX_O "[0] %s",
            (IPTR)msg->TreeNode->tn_Name);

        *msg->Array++ = nameBuffer;
        *msg->Array++ = (STRPTR)inspectBuffer[index];
    }
    else
    {
        *msg->Array++ = (STRPTR)"Type";
        *msg->Array++ = (STRPTR)"Value";

        *msg->Preparse++ = (STRPTR)MUIX_B;
        *msg->Preparse++ = (STRPTR)MUIX_B;
    }

    return (0);
}

/******************************************************************************
 *
 * FillInspector()
 *
 * Insert the rows once, as leaves of the root list; user data is the
 * address of the description InspectFunc() works from.
 *
 ******************************************************************************/

STATIC VOID FillInspector(ObjApp_t * object)
{
    UWORD row;

    for (row = 0; row < INSPECT_ROWS; row++)
    {
        DoMethod(object->NL_Inspect, MUIM_NListtree_Insert,
            (IPTR)inspectRows[row].name,          /* name      */
            (IPTR)&inspectRows[row],              /* user data */
            MUIV_NListtree_Insert_ListNode_Root,  /* list node */
            MUIV_NListtree_Insert_PrevNode_Tail,
            0);                                   /* leaf      */
    }
}

/******************************************************************************
 *
 * UpdateInspector()
 *
 * Refresh the inspector from the byte the dump cursor sits on. Polled from
 * the main loop after every input rather than notified, since HexEdit does
 * not advertise its cursor that way; cheap, since it bails out on the first
 * line whenever the byte hasn't changed.
 *
 ******************************************************************************/

STATIC VOID UpdateInspector(ObjApp_t * object)
{
    CONST_STRPTR bytes = NULL;
    ULONG avail = 0;
    ULONG open = FALSE;
    UWORD row;

    get(object->WI_Info, MUIA_Window_Open, &open);

    if (!open)
    {
        return;
    }

    /* The inspector is there to be read, not typed in: should it have taken
       the keyboard, on a click of its own or through the cycle chain, the
       dump takes it back. Checked before anything else, because a cursor
       which cannot move would never bring us back here. */

    if (object->OB_HexBar != NULL)
    {
        APTR active = NULL;

        get(object->WI_Info, MUIA_Window_ActiveObject, &active);

        if ((active == object->LV_Inspect) || (active == object->NL_Inspect))
        {
            set(object->WI_Info, MUIA_Window_ActiveObject,
                (IPTR)object->OB_HexDump);
        }
    }

    /* OB_HexBar exists only next to a real HexEdit object, never next to
       the placeholder text */

    if ((object->OB_HexBar != NULL) && (shownProp != NULL))
    {
        LONG cursor = 0;

        get(object->OB_HexDump, MUIA_HexEdit_CursorAddress, &cursor);

        if ((cursor >= 0) && ((ULONG)cursor < shownProp->op_length))
        {
            bytes = (CONST_STRPTR)shownProp->op_value + cursor;
            avail = shownProp->op_length - (ULONG)cursor;
        }
    }

    if (bytes == inspectBytes)
    {
        return; /* Same byte as last time */
    }

    inspectBytes = bytes;

    for (row = 0; row < INSPECT_ROWS; row++)
    {
        FormatInspect(bytes, avail, inspectRows[row].width,
            inspectRows[row].isSigned, inspectRows[row].isEpoch,
            inspectBuffer[row], INSPECT_CELL_SIZE);
    }

    /* The entries never change, only the cells they point at. NListtree
       being a subclass of NList, it answers the redraw method of its
       superclass. */

    DoMethod(object->NL_Inspect, MUIM_NList_Redraw, MUIV_NList_Redraw_All);
}

/******************************************************************************
 *
 * FillNodeInfo()
 *
 * Describe the active tree entry (property or node) in the information
 * window; nothing is opened or closed here. A node has no value of its own,
 * so its length is its subitem count and its dump is empty.
 *
 ******************************************************************************/

STATIC VOID FillNodeInfo(ObjApp_t * object)
{
    struct MUI_NListtree_TreeNode * tn = NULL;

    get(object->TR_Tree, MUIA_NListtree_Active, &tn);

    if ((tn == NULL) || ((IPTR)tn == (IPTR)MUIV_NListtree_Active_Off))
    {
        return;
    }

    if (tn == shownEntry)
    {
        return; /* Already on display */
    }

    shownEntry = tn;
    shownProp  = NULL;

    /* Any address would do as long as it differs from the next one: this
       forces UpdateInspector() to redraw its cells even when the new entry
       has nothing to inspect */

    inspectBytes = (CONST_STRPTR)-1;

    /* The title of the window, and what the clipboard holds for a node,
       which has no value of its own */

    BuildPath(object, tn, infoPathBuffer, sizeof(infoPathBuffer));

    if (tn->tn_Flags & TNF_LIST)
    {
        /* A device tree node */

        ULONG count = CountSubItems((of_node_t *)tn->tn_User);

        StringCopy(infoTypeBuffer, typeNames[ENTRY_TYPE_NODE],
            (LONG)sizeof(infoTypeBuffer));

        SPrintf(infoLengthBuffer, sizeof(infoLengthBuffer),
            (CONST_STRPTR)"%lu subitem%s", count,
            (IPTR)((count > 1) ? "s" : ""));

        ShowHexDump(object, NULL);
    }
    else
    {
        /* A property */

        of_property_t * prop = (of_property_t *)tn->tn_User;
        ULONG length = (prop != NULL) ? prop->op_length : 0;

        shownProp = prop;

        FormatType(prop, infoTypeBuffer, sizeof(infoTypeBuffer));

        SPrintf(infoLengthBuffer, sizeof(infoLengthBuffer),
            (CONST_STRPTR)"%lu byte%s", length,
            (IPTR)((length == 1) ? "" : "s"));

        ShowHexDump(object, prop);
    }

    /* Path, type and size all fit in the title, which leaves the whole of
       the window to the dump and its inspector */

    SPrintf(infoTitleBuffer, sizeof(infoTitleBuffer), (CONST_STRPTR)INFO_TITLE,
        (IPTR)infoPathBuffer, (IPTR)infoTypeBuffer, (IPTR)infoLengthBuffer);

    set(object->WI_Info, MUIA_Window_Title, (IPTR)infoTitleBuffer);
}

/******************************************************************************
 *
 * DoActive()
 *
 ******************************************************************************/

STATIC VOID DoActive(ObjApp_t * object)
{
    ULONG open = FALSE;

    get(object->WI_Info, MUIA_Window_Open, &open);

    if (open)
    {
        FillNodeInfo(object);
    }
}

/******************************************************************************
 *
 * DoNodeInfo()
 *
 ******************************************************************************/

STATIC VOID DoNodeInfo(ObjApp_t * object)
{
    struct MUI_NListtree_TreeNode * tn = NULL;

    get(object->TR_Tree, MUIA_NListtree_Active, &tn);

    if ((tn == NULL) || ((IPTR)tn == (IPTR)MUIV_NListtree_Active_Off))
    {
        return;
    }

    if (tn->tn_Flags & TNF_LIST)
    {
        return;
    }

    FillNodeInfo(object);

    set(object->WI_Info, MUIA_Window_Open, TRUE);

    if (object->OB_HexBar != NULL)
    {
        set(object->WI_Info, MUIA_Window_ActiveObject, (IPTR)object->OB_HexDump);
    }
}

/******************************************************************************
 *
 * WriteValue()
 *
 * Write what the displayed entry is worth: an inspector cell if 'text' is
 * given, else the shown property's value (DumpValue()), or a node's path.
 *
 ******************************************************************************/

STATIC VOID WriteValue(struct Writer * writer, CONST_STRPTR text)
{
    if (text != NULL)
    {
        /* One cell of the inspector, handed over as it stands */

        WriterWrite(writer, text, StringLength(text));
        return;
    }

    if (shownProp == NULL)
    {
        /* A node: its path */

        WriterWrite(writer, (CONST_STRPTR)infoPathBuffer,
            StringLength((CONST_STRPTR)infoPathBuffer));
        return;
    }

    if ((shownProp->op_value == NULL) || (shownProp->op_length == 0))
    {
        return; /* Nothing to say */
    }

    DumpValue(writer, shownProp);
}

/******************************************************************************
 *
 * DoCopy()
 *
 ******************************************************************************/

STATIC VOID DoCopy(ObjApp_t * object, CONST_STRPTR text)
{
    struct Writer writer;
    BOOL ok = FALSE;

    if (shownEntry != NULL)
    {
        if (WriterOpenClipboard(&writer))
        {
            WriteValue(&writer, text);

            ok = WriterClose(&writer);
        }
    }

    if (!ok)
    {
        ReportFailure(object, object->WI_Info, (CONST_STRPTR)"Nothing could be written to the clipboard.");
    }
}

/******************************************************************************
 *
 * DoSave()
 *
 * Save the displayed property's raw bytes as they lie in memory, byte for
 * byte -- unlike DoCopy(), which gives a readable rendering to the
 * clipboard. A node has nothing of its own to save.
 *
 ******************************************************************************/

STATIC VOID DoSave(ObjApp_t * object)
{
    struct Writer writer;
    UBYTE path[MAX_PATHNAME];
    UBYTE fileName[128];
    BOOL ok = TRUE;

    if ((shownProp == NULL) ||
        (shownProp->op_value == NULL) ||
        (shownProp->op_length == 0))
    {
        ReportFailure(object, object->WI_Info, (CONST_STRPTR)"This entry holds no value to save.");
        return;
    }

    SPrintf(fileName, sizeof(fileName), (CONST_STRPTR)"%s.raw",
        (IPTR)((shownProp->op_name != NULL) ? shownProp->op_name : ""));

    if (PickSavePath((CONST_STRPTR)"Save the raw bytes to...", fileName, path, sizeof(path)))
    {
        ok = FALSE;

        if (WriterOpenFile(&writer, (CONST_STRPTR)path))
        {
            WriterWrite(&writer, (CONST_STRPTR)shownProp->op_value,
                (LONG)shownProp->op_length);

            ok = WriterClose(&writer);
        }
    }

    if (!ok)
    {
        ReportFailure(object, object->WI_Info, (CONST_STRPTR)"The bytes could not be written to that file.");
    }
}

/******************************************************************************
 *
 * DoSaveNode()
 *
 * Save the tree's currently selected entry, and everything below it, to a
 * plain text file. Selecting the root node "/" dumps the whole tree, and
 * an active search narrows this down exactly as it does the tree view.
 *
 ******************************************************************************/

STATIC VOID DoSaveNode(ObjApp_t * object)
{
    struct MUI_NListtree_TreeNode * tn = NULL;
    struct Writer writer;
    UBYTE path[MAX_PATHNAME];
    UBYTE fileName[128];
    CONST_STRPTR entryName;
    ULONG checked = FALSE;
    BOOL ok = TRUE;

    get(object->TR_Tree, MUIA_NListtree_Active, &tn);

    if ((tn == NULL) || ((IPTR)tn == (IPTR)MUIV_NListtree_Active_Off))
    {
        ReportFailure(object, object->WI_Main, (CONST_STRPTR)"Select a node or a property first.");
        return;
    }

    get(object->MI_FullNames, MUIA_Menuitem_Checked, &checked);
    dumpFullNames = (BOOL)(checked != 0);

    entryName = (CONST_STRPTR)tn->tn_Name;

    if ((entryName == NULL) || (entryName[0] == '\0') ||
        ((entryName[0] == '/') && (entryName[1] == '\0')))
    {
        entryName = (CONST_STRPTR)"DeviceTree";
    }

    SPrintf(fileName, sizeof(fileName), (CONST_STRPTR)"%s.txt", (IPTR)entryName);

    if (PickSavePath((CONST_STRPTR)"Save this entry to...", fileName, path, sizeof(path)))
    {
        ok = FALSE;

        if (WriterOpenFile(&writer, (CONST_STRPTR)path))
        {
            if (tn->tn_Flags & TNF_LIST)
            {
                DumpOneNode(&writer, (const of_node_t *)tn->tn_User, 0);
            }
            else
            {
                struct MUI_NListtree_TreeNode * parentTn =
                    (struct MUI_NListtree_TreeNode *)DoMethod(object->TR_Tree,
                        MUIM_NListtree_GetEntry, (IPTR)tn,
                        MUIV_NListtree_GetEntry_Position_Parent, 0);

                const of_node_t * owner = (parentTn != NULL)
                    ? (const of_node_t *)parentTn->tn_User : NULL;

                DumpOneProperty(&writer, owner,
                    (const of_property_t *)tn->tn_User, 0);
            }

            ok = WriterClose(&writer);
        }
    }

    if (!ok)
    {
        ReportFailure(object, object->WI_Main, (CONST_STRPTR)"The entry could not be dumped to that file.");
    }
}

/******************************************************************************
 *
 * DoInspect()
 *
 ******************************************************************************/

STATIC VOID DoInspect(VOID)
{
    ULONG value = 0;

    get(appMain->CY_Endian, MUIA_Cycle_Active, &value);
    inspectLittle = (BOOL)(value != 0);

    get(appMain->CY_Base, MUIA_Cycle_Active, &value);
    inspectBase = value;

    inspectBytes = (CONST_STRPTR)-1;
}

/******************************************************************************
 *
 * DoCopyCell()
 *
 ******************************************************************************/

STATIC VOID DoCopyCell(VOID)
{
    struct MUI_NListtree_TreeNode * tn = NULL;

    get(appMain->NL_Inspect, MUIA_NListtree_Active, &tn);

    if ((tn != NULL) &&
        ((IPTR)tn != (IPTR)MUIV_NListtree_Active_Off) &&
        (tn->tn_User != NULL))
    {
        LONG index = (LONG)
            ((const struct InspectRow *)tn->tn_User - inspectRows);

        if ((index >= 0) && (index < INSPECT_ROWS))
        {
            DoCopy(appMain, (CONST_STRPTR)inspectBuffer[index]);
        }
    }
}

/******************************************************************************
 *
 * DoTitleClick()
 *
 ******************************************************************************/

STATIC VOID DoTitleClick(VOID)
{
    LONG column = -1;

    get(appMain->TR_Tree, MUIA_NList_TitleClick, &column);

    if ((column >= SORT_BY_NAME) && (column < SORT_COLUMN_COUNT))
    {
        SetSortColumn(appMain, (ULONG)column, TRUE);
    }
}

/******************************************************************************
 *
 * ProcessEvents()
 *
 ******************************************************************************/

VOID ProcessEvents(VOID)
{
    BOOL running = TRUE;

    while (running)
    {
        ULONG signals;

        ULONG id = DoMethod(appMain->App, MUIM_Application_Input, &signals);

        switch (id)
        {
        case EVENT_RELOAD:
            DoReload(appMain);
            break;

        case EVENT_ABOUT:
            DoAbout();
            break;

        case EVENT_ABOUT_MUI:
            DoAboutMUI();
            break;

        case EVENT_ACTIVE:
            DoActive(appMain);
            break;

        case EVENT_INSPECT:
            DoInspect();
            break;

        case EVENT_COPYCELL:
            DoCopyCell();
            break;

        case EVENT_COPY:
            DoCopy(appMain, NULL);
            break;

        case EVENT_SAVE:
            DoSave(appMain);
            break;

        case EVENT_SAVENODE:
            DoSaveNode(appMain);
            break;

        case EVENT_SEARCH:
            DoSearch(appMain);
            break;

        case EVENT_TOGGLESEARCH:
            DoToggleSearch(appMain);
            break;

        case EVENT_EXPAND:
            DoMethod(appMain->TR_Tree, MUIM_NListtree_Open,
                MUIV_NListtree_Open_ListNode_Root,
                MUIV_NListtree_Open_TreeNode_All, 0);
            break;

        case EVENT_COLLAPSE:
            DoMethod(appMain->TR_Tree, MUIM_NListtree_Close,
                MUIV_NListtree_Close_ListNode_Root,
                MUIV_NListtree_Close_TreeNode_All, 0);
            break;

        case EVENT_MUISETTINGS:
            DoMethod(appMain->App, MUIM_Application_OpenConfigWindow, 0);
            break;

        case EVENT_ICONIFY:
            set(appMain->App, MUIA_Application_Iconified, TRUE);
            break;

        case EVENT_FIRST:
        case EVENT_PREV:
        case EVENT_NEXT:
        case EVENT_LAST:
            DoCursor(appMain, id);
            break;

        case EVENT_NODEINFO:
            DoNodeInfo(appMain);
            break;

        case EVENT_TITLECLICK:
            DoTitleClick();
            break;

        case MUIV_Application_ReturnID_Quit:
        case EVENT_QUIT:
            running = FALSE;
            break;
        }

        if (running)
        {
            UpdateInspector(appMain);
        }

        if (running && signals)
        {
            signals = Wait(signals |
                SIGBREAKF_CTRL_C |
                SIGBREAKF_CTRL_E |
                SIGBREAKF_CTRL_F);

            if (signals & SIGBREAKF_CTRL_C)
            {
                break;
            }

            if ((signals & SIGBREAKF_CTRL_E) ||
                (signals & SIGBREAKF_CTRL_F))
            {
                set(appMain->App, MUIA_Application_Iconified, FALSE);
                set(appMain->WI_Main, MUIA_Window_Open, TRUE);
            }
        }
    }
}

/******************************************************************************
 *
 * MENU
 *
 * A classic gadtools.library NewMenu array, handed to
 * MUI_MakeObject(MUIO_MenustripNM, ...). Every real item's nm_UserData is a
 * ProcessEvent_t, delivered straight back by MUIM_Application_Input when
 * picked, so no per-item MUIM_Notify wiring is needed.
 *
 ******************************************************************************/

STATIC struct NewMenu menuData[] =
{
    { NM_TITLE, (CONST_STRPTR)"Project",            NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Reload...",          "L",   0,                          0, (APTR)EVENT_RELOAD },
    { NM_ITEM,  (CONST_STRPTR)"Save node as...",    "A",   0,                          0, (APTR)EVENT_SAVENODE },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Use full names",     "N",   CHECKIT|MENUTOGGLE|CHECKED, 0, (APTR)EVENT_FULLNAMES },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"About...",           "?",   0,                          0, (APTR)EVENT_ABOUT },
    { NM_ITEM,  (CONST_STRPTR)"About MUI...",       NULL,  0,                          0, (APTR)EVENT_ABOUT_MUI },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Settings MUI...",    NULL,  0,                          0, (APTR)EVENT_MUISETTINGS },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Iconify",            "I",   0,                          0, (APTR)EVENT_ICONIFY },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Quit",               "Q",   0,                          0, (APTR)EVENT_QUIT },

    { NM_TITLE, (CONST_STRPTR)"Tree",               NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Expand All",         "E",   0,                          0, (APTR)EVENT_EXPAND },
    { NM_ITEM,  (CONST_STRPTR)"Collapse All",       "C",   0,                          0, (APTR)EVENT_COLLAPSE },
    { NM_ITEM,  NM_BARLABEL,                        NULL,  0,                          0, (APTR)0 },
    { NM_ITEM,  (CONST_STRPTR)"Show search gadget", "F",   CHECKIT|MENUTOGGLE|CHECKED, 0, (APTR)EVENT_TOGGLESEARCH },

    { NM_END,   NULL,                               NULL,  0,                          0, (APTR)0 }
};

/******************************************************************************
 *
 * CreateApp()
 *
 ******************************************************************************/

ObjApp_t * CreateApp(VOID)
{
    ObjApp_t * object;
    APTR GROUP_INFO;

    /* Allocate App */

    if (!(object = AllocVec(sizeof(ObjApp_t), MEMF_PUBLIC | MEMF_CLEAR)))
    {
        return (NULL);
    }

    /* MUI Hooks */

    DisplayHook.h_Entry    = (ULONG (*)())DisplayFunc;
    DisplayHook.h_SubEntry = NULL;
    DisplayHook.h_Data     = NULL;

    InspectHook.h_Entry    = (ULONG (*)())InspectFunc;
    InspectHook.h_SubEntry = NULL;
    InspectHook.h_Data     = NULL;

    /* MUI MenuStrips */

    object->MN_Main = (APTR)MUI_MakeObject(MUIO_MenustripNM, menuData, 0);

    if (object->MN_Main == NULL)
    {
        FreeVec(object);
        return (NULL);
    }

    object->MI_FullNames  = (APTR)DoMethod(object->MN_Main,
        MUIM_FindUData, EVENT_FULLNAMES);
    object->MI_ShowSearch = (APTR)DoMethod(object->MN_Main,
        MUIM_FindUData, EVENT_TOGGLESEARCH);

    /* MUI NListview */

    object->LV_Tree = NListviewObject,
        MUIA_ShortHelp,       MUIX_B "DeviceTree Explorer" MUIX_N "\nExplore the DeviceTree.resource items",
        MUIA_CycleChain,      TRUE,
        MUIA_NListview_NList, object->TR_Tree = NListtreeObject,
            InputListFrame,
            MUIA_CycleChain,             TRUE,
            MUIA_NListtree_DisplayHook,  &DisplayHook,
            MUIA_NListtree_MultiSelect,  MUIV_NListtree_MultiSelect_None,
            MUIA_NListtree_DoubleClick,  MUIV_NListtree_DoubleClick_Tree,
            MUIA_NListtree_EmptyNodes,   FALSE,
            MUIA_NListtree_TreeColumn,   0,
            MUIA_NListtree_DragDropSort, FALSE,
            MUIA_NListtree_Title,        TRUE,
            MUIA_NListtree_Format,       TREE_FORMAT,
            MUIA_NList_TitleSeparator,   TRUE,
            MUIA_NList_TitleClick,       TRUE,
        End,
    End;

    /* Node Property window */

    GROUP_INFO = VGroup,
        MUIA_Group_HorizSpacing, 4,
        MUIA_Group_VertSpacing,  4,
        Child, HGroup,
            MUIA_InnerLeft,   4,
            MUIA_InnerRight,  4,
            MUIA_InnerTop,    4,
            MUIA_InnerBottom, 4,
            Child, object->GR_Hex = HGroup,
                MUIA_Group_Spacing, 2,
            End,
            Child, BalanceObject, End,
            Child, VGroup,
                MUIA_HorizWeight, 50,
                Child, object->LV_Inspect = NListviewObject,
                    MUIA_ShortHelp,
                        MUIX_B "Data Inspector" MUIX_N "\n"
                        "Inspect the value under the cursor.",
                    MUIA_NListview_NList, object->NL_Inspect = NListtreeObject,
                        ReadListFrame,
                        MUIA_Font,                   MUIV_Font_Fixed,
                        MUIA_NListtree_DisplayHook,  &InspectHook,
                        MUIA_NListtree_Title,        TRUE,
                        MUIA_NListtree_Format,       INSPECT_FORMAT,
                        MUIA_NListtree_MultiSelect,  MUIV_NListtree_MultiSelect_None,
                        MUIA_NListtree_DoubleClick,  MUIV_NListtree_DoubleClick_Tree,
                        MUIA_NListtree_DragDropSort, FALSE,
                        MUIA_NListtree_EmptyNodes,   FALSE,
                        MUIA_NList_TitleSeparator,   TRUE,
                    End,
                End,
                Child, HGroup,
                    Child, object->BT_First = MakeButton("|<", "First byte"),
                    Child, object->BT_Prev  = MakeButton("<",  "Previous byte"),
                    Child, object->BT_Next  = MakeButton(">",  "Next byte"),
                    Child, object->BT_Last  = MakeButton(">|", "Last byte"),
                End,
                Child, ColGroup(2),
                    Child, object->CY_Endian = CycleObject,
                        MUIA_CycleChain,    1,
                        MUIA_Cycle_Entries, endianEntries,
                        MUIA_Cycle_Active,  0,
                        MUIA_ShortHelp,     MUIX_B "Data Inspector" MUIX_N "\nSelect how to read the value.",
                    End,
                    Child, object->CY_Base = CycleObject,
                        MUIA_CycleChain,    1,
                        MUIA_Cycle_Entries, baseEntries,
                        MUIA_Cycle_Active,  BASE_DECIMAL,
                        MUIA_ShortHelp,     MUIX_B "Data Inspector" MUIX_N "\nSelect how to display the value.",
                    End,
                End,
            End,
        End,
        Child, RectangleObject,
            MUIA_Rectangle_HBar, TRUE,
            MUIA_VertWeight,     0,
        End,
        Child, HGroup,
            Child, object->BT_InfoCopy  = KeyButton("Copy to clipboard", 'c'),
            Child, object->BT_InfoSave  = KeyButton("Save to file", 's'),
            Child, object->BT_InfoClose = KeyButton("Close", 'o'),
        End,
    End;

    /* MUI Windows */

    object->WI_Main = WindowObject,
        MUIA_Window_Title,      APP_NAME,
        MUIA_Window_ID,         MAKE_ID('E', 'D', 'T', 'M'),
        MUIA_Window_SizeGadget, TRUE,
        MUIA_Window_SizeRight,  FALSE,
        MUIA_Window_Width,      WIN_MAIN_WIDTH,
        MUIA_Window_Height,     WIN_MAIN_HEIGHT,
        WindowContents, object->GR_Main = VGroup,
            MUIA_InnerLeft,   4,
            MUIA_InnerRight,  4,
            MUIA_InnerTop,    4,
            MUIA_InnerBottom, 4,
            Child, object->LV_Tree,
            Child, object->GR_Search = HGroup,
                MUIA_VertWeight, 0,
                Child, object->ST_Search = StringObject,
                    StringFrame,
                    MUIA_CycleChain,     TRUE,
                    MUIA_String_MaxLen,  (LONG)sizeof(searchFilter),
                    MUIA_ShortHelp,
                        MUIX_B "Search" MUIX_N "\n"
                        "Press RETURN to filter the tree down to the nodes and properties\n"
                        "whose name contains this text (case insensitive search).\n"
                        "Clear and press RETURN again to show everything.",
                End,
            End,
        End,
    End;

    object->WI_Info = WindowObject,
        MUIA_Window_Title,      "Property Information",
        MUIA_Window_ID,         MAKE_ID('E', 'D', 'T', 'I'),
        MUIA_Window_SizeGadget, TRUE,
        MUIA_Window_SizeRight,  FALSE,
        MUIA_Window_Width,      WIN_INFO_WIDTH,
        MUIA_Window_Height,     WIN_INFO_HEIGHT,
        WindowContents,         GROUP_INFO,
    End;

    /* MUI Application */

    object->App = ApplicationObject,
        MUIA_Application_Author,      APP_AUTHORS,
        MUIA_Application_Base,        APP_BASE,
        MUIA_Application_Copyright,   APP_COPYRIGHT,
        MUIA_Application_Description, APP_DESCRIPTION,
        MUIA_Application_HelpFile,    APP_HELPFILE,
        MUIA_Application_Title,       APP_NAME,
        MUIA_Application_Version,     "$VER: " APP_VERSTRING,
        MUIA_Application_Menustrip,   object->MN_Main,
        SubWindow, object->WI_Main,
        SubWindow, object->WI_Info,
    End;

    if (object->App == NULL)
    {
        FreeVec(object);
        return (NULL);
    }

    /* MUI Cycle Chain */

    set(object->BT_First,     MUIA_CycleChain, 1);
    set(object->BT_Prev,      MUIA_CycleChain, 1);
    set(object->BT_Next,      MUIA_CycleChain, 1);
    set(object->BT_Last,      MUIA_CycleChain, 1);
    set(object->BT_InfoCopy,  MUIA_CycleChain, 1);
    set(object->BT_InfoSave,  MUIA_CycleChain, 1);
    set(object->BT_InfoClose, MUIA_CycleChain, 1);

    /* MUI Notify */

    DoMethod(object->WI_Main, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
        object->App, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    DoMethod(object->TR_Tree, MUIM_Notify, MUIA_NListtree_DoubleClick, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_NODEINFO);

    DoMethod(object->TR_Tree, MUIM_Notify, MUIA_NListtree_Active, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_ACTIVE);

    DoMethod(object->TR_Tree, MUIM_Notify, MUIA_NList_TitleClick, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_TITLECLICK);

    DoMethod(object->CY_Endian, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_INSPECT);

    DoMethod(object->CY_Base, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_INSPECT);

    DoMethod(object->WI_Info, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
        object->WI_Info, 3, MUIM_Set, MUIA_Window_Open, FALSE);

    DoMethod(object->BT_InfoCopy, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_COPY);

    DoMethod(object->BT_InfoSave, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_SAVE);

    DoMethod(object->BT_InfoClose, MUIM_Notify, MUIA_Pressed, FALSE,
        object->WI_Info, 3, MUIM_Set, MUIA_Window_Open, FALSE);

    DoMethod(object->BT_First, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_FIRST);

    DoMethod(object->BT_Prev, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_PREV);

    DoMethod(object->BT_Next, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_NEXT);

    DoMethod(object->BT_Last, MUIM_Notify, MUIA_Pressed, FALSE,
        object->App, 2, MUIM_Application_ReturnID, EVENT_LAST);

    DoMethod(object->NL_Inspect, MUIM_Notify, MUIA_NListtree_DoubleClick, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_COPYCELL);

    DoMethod(object->ST_Search, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime,
        object->App, 2, MUIM_Application_ReturnID, EVENT_SEARCH);

    /* Final inits */

    UseTypeImages(object);
    FillInspector(object);
    UpdateTitleMark(object);
    DoReload(object);
    set(object->WI_Main, MUIA_Window_Open, TRUE);
    set(object->WI_Main, MUIA_Window_ActiveObject, (IPTR)object->ST_Search);

    return (object);
}

/******************************************************************************
 *
 * DisposeApp()
 *
 ******************************************************************************/

VOID DisposeApp(ObjApp_t * object)
{
    if (object)
    {
        ULONG checked = TRUE;

        set(object->WI_Info, MUIA_Window_Open, FALSE);
        set(object->WI_Main, MUIA_Window_Open, FALSE);
        DoMethod(object->TR_Tree, MUIM_NListtree_Clear, NULL, 0);

        /* DoToggleSearch() may have removed GR_Search from GR_Main; if it
           is hidden right now, parent it back so MUI_DisposeObject() below
           actually reaches and frees it instead of leaking it */

        get(object->MI_ShowSearch, MUIA_Menuitem_Checked, &checked);

        if (!checked)
        {
            DoMethod(object->GR_Main, OM_ADDMEMBER, (IPTR)object->GR_Search);
        }

        MUI_DisposeObject(object->App);
        FreeVec(object);
    }
}

/******************************************************************************
 *
 * OpenLibs()
 *
 ******************************************************************************/

BOOL OpenLibs(VOID)
{
    if (!OpenDeviceTree())
    {
        return (FALSE);
    }

    if (!(MUIMasterBase = OpenLibrary((CONST_STRPTR)MUIMASTER_NAME, MUIMASTER_VMIN)))
    {
        PutStr("Failed to open " MUIMASTER_NAME ".\n");
        return (FALSE);
    }

    HexClass = MUI_CreateCustomClass(NULL, (STRPTR)MUIC_HexEdit, NULL,
        0, (APTR)HexDispatcher);

    return (TRUE);
}

/******************************************************************************
 *
 * CloseLibs()
 *
 ******************************************************************************/

VOID CloseLibs(VOID)
{
    if (HexClass)
    {
        MUI_DeleteCustomClass(HexClass);
        HexClass = NULL;
    }

    if (MUIMasterBase)
    {
        CloseLibrary(MUIMasterBase);
        MUIMasterBase = NULL;
    }
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
