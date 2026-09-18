/******************************************************************************
 *
 * Dump.c
 *
 * Write the device tree, or a piece of it, to a struct Writer: the shared
 * machinery behind the GUI's "Save node as..." and the headless SEARCH=/
 * NODE= command line mode. See Dump.h.
 *
 ******************************************************************************/

#include <dos/dos.h>
#include <exec/exec.h>
#include <exec/types.h>
#include <utility/utility.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "Utils.h"
#include "Clipboard.h"
#include "DeviceTree.h"
#include "Main.h"
#include "Dump.h"

extern struct Library * UtilityBase;

/******************************************************************************
 *
 * PROTOTYPES
 *
 ******************************************************************************/

STATIC BOOL SubtreeMatchesFilter(const of_node_t * node);
STATIC VOID DumpIndent(struct Writer * writer, ULONG depth);
STATIC VOID DumpNodePath(struct Writer * writer, const of_node_t * node);
STATIC VOID DumpProperties(struct Writer * writer, const of_node_t * owner,
                const of_property_t * prop, ULONG depth);

/******************************************************************************
 *
 * GLOBALS
 *
 ******************************************************************************/

BOOL  dumpFullNames = TRUE;
UBYTE searchFilter[128] = "";

/******************************************************************************
 *
 * PropertyMatchesFilter()
 *
 ******************************************************************************/

BOOL PropertyMatchesFilter(const of_property_t * prop)
{
    if (searchFilter[0] == '\0')
    {
        return (TRUE);
    }

    return (StringContains((CONST_STRPTR)prop->op_name, (CONST_STRPTR)searchFilter));
}

/******************************************************************************
 *
 * SubtreeMatchesFilter()
 *
 * Whether 'node' itself, one of its properties, or anything under one of
 * its children matches searchFilter. Only meant to be called when a filter
 * is actually set: see NodeMatchesFilter().
 *
 ******************************************************************************/

STATIC BOOL SubtreeMatchesFilter(const of_node_t * node)
{
    const of_property_t * prop;
    const of_node_t * child;

    if (StringContains((CONST_STRPTR)node->on_name, (CONST_STRPTR)searchFilter))
    {
        return (TRUE);
    }

    for (prop = node->on_properties; prop != NULL; prop = prop->op_next)
    {
        if (StringContains((CONST_STRPTR)prop->op_name, (CONST_STRPTR)searchFilter))
        {
            return (TRUE);
        }
    }

    for (child = node->on_children; child != NULL; child = child->on_next)
    {
        if (SubtreeMatchesFilter(child))
        {
            return (TRUE);
        }
    }

    return (FALSE);
}

/******************************************************************************
 *
 * NodeMatchesFilter()
 *
 * Whether 'node' belongs in a filtered tree: either there is no filter, or
 * 'node' is on the way to a match somewhere in its own subtree. Applied at
 * every level by InsertNodes() (GUI.c) and DumpNode(), this alone is what
 * keeps a match's parents in view while everything else is pruned away.
 *
 ******************************************************************************/

BOOL NodeMatchesFilter(const of_node_t * node)
{
    if (searchFilter[0] == '\0')
    {
        return (TRUE);
    }

    return (SubtreeMatchesFilter(node));
}

/******************************************************************************
 *
 * DumpValue()
 *
 * A property's whole value, in full, shaped after its type:
 *
 *   string       the string itself
 *   string list  one string per line, the empty ones skipped
 *   blob         every byte in hexadecimal, sixteen per line
 *   long, quad   the value as the Value column words it
 *
 * Nothing is gathered in a buffer first: the writer is fed piece by piece,
 * so a property of any size costs a few dozen bytes of stack. Callers are
 * expected to have already ruled out an empty property (NULL op_value or a
 * zero op_length), which this never writes anything for on its own.
 *
 ******************************************************************************/

#define HEX_PER_LINE (16)

VOID DumpValue(struct Writer * writer, const of_property_t * prop)
{
    UBYTE line[64];
    CONST_STRPTR bytes = (CONST_STRPTR)prop->op_value;
    ULONG length = prop->op_length;
    ULONG i;

    switch (EntryType(prop))
    {
    case ENTRY_TYPE_STRING:
        {
            BOOL first = TRUE;

            for (i = 0; i < length; )
            {
                ULONG start;

                if (bytes[i] == '\0')
                {
                    i++; /* separator or padding */
                    continue;
                }

                if (!first)
                {
                    WriterWrite(writer, (CONST_STRPTR)"\n", 1);
                }

                start = i;

                while ((i < length) && bytes[i])
                {
                    i++;
                }

                WriterWrite(writer, &bytes[start], (LONG)(i - start));

                first = FALSE;
            }
        }
        break;

    case ENTRY_TYPE_BLOB:
        {
            LONG pos = 0;

            for (i = 0; i < length; i++)
            {
                SPrintf(&line[pos], (LONG)sizeof(line) - pos,
                    (CONST_STRPTR)"%02lx ", (ULONG)bytes[i]);

                pos += 3;

                if (((i % HEX_PER_LINE) == (HEX_PER_LINE - 1)) ||
                    (i == (length - 1)))
                {
                    line[pos - 1] = '\n'; /* The blank of the last byte */

                    WriterWrite(writer, (CONST_STRPTR)line, pos);

                    pos = 0;
                }
            }
        }
        break;

    default:

        /* A number, worded as the tree words it */

        FormatValue(prop, line, (LONG)sizeof(line));

        WriterWrite(writer, (CONST_STRPTR)line, StringLength((CONST_STRPTR)line));
        break;
    }
}

/******************************************************************************
 *
 * DumpIndent()
 *
 ******************************************************************************/

#define DUMP_INDENT "    "

STATIC VOID DumpIndent(struct Writer * writer, ULONG depth)
{
    while (depth-- > 0)
    {
        WriterWrite(writer, (CONST_STRPTR)DUMP_INDENT,
            (LONG)StringLength((CONST_STRPTR)DUMP_INDENT));
    }
}

/******************************************************************************
 *
 * DumpNodePath()
 *
 * A node's full path, walking on_parent up to the root, then writing each
 * name from there back down ("/chosen", "/regulator-cam1", ...). Nothing at
 * all for the root itself, whose on_name is empty: DumpOneNode's own "/\n"
 * is enough for that line.
 *
 ******************************************************************************/

STATIC VOID DumpNodePath(struct Writer * writer, const of_node_t * node)
{
    const of_node_t * stack[MAX_TREE_DEPTH];
    LONG depth = 0;

    while ((node != NULL) && (depth < MAX_TREE_DEPTH))
    {
        stack[depth++] = node;
        node = node->on_parent;
    }

    while (depth-- > 0)
    {
        CONST_STRPTR name = (CONST_STRPTR)stack[depth]->on_name;

        if ((name == NULL) || (name[0] == '\0'))
        {
            continue;
        }

        WriterWrite(writer, (CONST_STRPTR)"/", 1);
        WriterWrite(writer, name, StringLength(name));
    }
}

/******************************************************************************
 *
 * DumpOneProperty()
 *
 * One line: name, type and the same value a glance at the tree already
 * shows. Never follows op_next, so it dumps this property alone.
 *
 * Indented under 'owner' the plain way, or, when dumpFullNames is set, not
 * indented at all and prefixed with owner's own full path instead ("/chosen/
 * stdout-path: ...") -- every line then stands on its own for a grep, tree
 * structure and all.
 *
 ******************************************************************************/

VOID DumpOneProperty(struct Writer * writer, const of_node_t * owner,
    const of_property_t * prop, ULONG depth)
{
    UBYTE type[32];
    ULONG entryType = EntryType(prop);
    BOOL hasValue = (prop->op_value != NULL) && (prop->op_length > 0);

    if (dumpFullNames)
    {
        DumpNodePath(writer, owner);
        WriterWrite(writer, (CONST_STRPTR)"/", 1);
    }
    else
    {
        DumpIndent(writer, depth);
    }

    FormatType(prop, type, sizeof(type));

    WriterWrite(writer, (CONST_STRPTR)prop->op_name,
        StringLength((CONST_STRPTR)prop->op_name));
    WriterWrite(writer, (CONST_STRPTR)": ", 2);
    WriterWrite(writer, (CONST_STRPTR)type, StringLength((CONST_STRPTR)type));
    WriterWrite(writer, (CONST_STRPTR)" = ", 3);

    if (!hasValue)
    {
        WriterWrite(writer, (CONST_STRPTR)"-\n", 2);
        return;
    }

    DumpValue(writer, prop);

    /* DumpValue() already ends a blob's last hex line with its own '\n' */

    if (entryType != ENTRY_TYPE_BLOB)
    {
        WriterWrite(writer, (CONST_STRPTR)"\n", 1);
    }
}

/******************************************************************************
 *
 * DumpProperties()
 *
 * A node's properties, one line each, in the order devicetree.resource
 * holds them (unsorted). Subject to the same searchFilter as the tree
 * view, via PropertyMatchesFilter(): an active search narrows an export
 * down to what it would show on screen.
 *
 ******************************************************************************/

STATIC VOID DumpProperties(struct Writer * writer, const of_node_t * owner,
    const of_property_t * prop, ULONG depth)
{
    for (; prop != NULL; prop = prop->op_next)
    {
        if (PropertyMatchesFilter(prop))
        {
            DumpOneProperty(writer, owner, prop, depth);
        }
    }
}

/******************************************************************************
 *
 * DumpOneNode()
 *
 * One node, its properties, then its children: the whole subtree under
 * 'node', but never its siblings (on_next is left untouched).
 *
 * The node's own line names it either the plain way ("chosen/"), indented,
 * or, when dumpFullNames is set, by its full path ("/chosen/") with no
 * indentation at all -- every line then stands on its own for a grep, tree
 * structure and all.
 *
 ******************************************************************************/

VOID DumpOneNode(struct Writer * writer, const of_node_t * node, ULONG depth)
{
    if (dumpFullNames)
    {
        DumpNodePath(writer, node);
    }
    else
    {
        CONST_STRPTR name = (CONST_STRPTR)node->on_name;

        DumpIndent(writer, depth);

        if ((name != NULL) && (name[0] != '\0'))
        {
            WriterWrite(writer, name, StringLength(name));
        }
    }

    WriterWrite(writer, (CONST_STRPTR)"/\n", 2);

    DumpProperties(writer, node, node->on_properties, depth + 1);
    DumpNode(writer, node->on_children, depth + 1);
}

/******************************************************************************
 *
 * DumpNode()
 *
 * One node, its properties, then its children: the whole tree from 'node'
 * down, in the order devicetree.resource holds it (unsorted). Subject to
 * the same searchFilter as the tree view, via NodeMatchesFilter(): a
 * sibling is skipped unless it, or something under it, matches.
 *
 ******************************************************************************/

VOID DumpNode(struct Writer * writer, const of_node_t * node, ULONG depth)
{
    for (; node != NULL; node = node->on_next)
    {
        if (NodeMatchesFilter(node))
        {
            DumpOneNode(writer, node, depth);
        }
    }
}

/******************************************************************************
 *
 * ResolvePath()
 *
 * The node or property at 'path' ("/soc/watchdog@7e100000/phandle", leading/
 * trailing slashes and repeats of them ignored), walking on_children one
 * name at a time, case insensitive. The last segment may name a property
 * instead of a child node, in which case '*outNode' is left holding its
 * parent and '*outProp' the property itself; otherwise '*outNode' is the
 * resolved node and '*outProp' is NULL. '*outNode' is 'root' and '*outProp'
 * is NULL for an empty path. Returns FALSE if any segment along the way
 * matches neither a child node nor, as the last segment, a property.
 *
 ******************************************************************************/

BOOL ResolvePath(const of_node_t * root, CONST_STRPTR path,
    const of_node_t ** outNode, const of_property_t ** outProp)
{
    const of_node_t * node = root;

    *outNode = root;
    *outProp = NULL;

    if (path == NULL)
    {
        return (TRUE);
    }

    while (*path == '/')
    {
        path++;
    }

    while (*path != '\0')
    {
        UBYTE segment[128];
        LONG len = 0;
        const of_node_t * child;
        CONST_STRPTR rest;

        while ((path[len] != '\0') && (path[len] != '/'))
        {
            len++;
        }

        if (len >= (LONG)sizeof(segment))
        {
            len = (LONG)sizeof(segment) - 1;
        }

        CopyMem((APTR)path, (APTR)segment, (ULONG)len);
        segment[len] = '\0';

        rest = path + len;

        while (*rest == '/')
        {
            rest++;
        }

        for (child = node->on_children; child != NULL; child = child->on_next)
        {
            CONST_STRPTR name = (CONST_STRPTR)child->on_name;

            if ((name != NULL) && (Stricmp(name, (CONST_STRPTR)segment) == 0))
            {
                break;
            }
        }

        if (child != NULL)
        {
            node  = child;
            path  = rest;
            *outNode = node;
            continue;
        }

        /* Not a child node: only a property of 'node' can still match, and
           only as the very last segment of the path */

        if (*rest == '\0')
        {
            const of_property_t * prop;

            for (prop = node->on_properties; prop != NULL; prop = prop->op_next)
            {
                CONST_STRPTR name = (CONST_STRPTR)prop->op_name;

                if ((name != NULL) && (Stricmp(name, (CONST_STRPTR)segment) == 0))
                {
                    *outProp = prop; /* *outNode is already its parent */
                    return (TRUE);
                }
            }
        }

        return (FALSE);
    }

    return (TRUE);
}

/******************************************************************************
 *
 * DoExport()
 *
 * The headless counterpart to DoSaveNode() (GUI.c): straight from the Shell
 * command line (see main()), write to standard output and quit, no window
 * ever opened -- so the result can be read directly or redirected to a
 * file, e.g. "Emu68DeviceTree SEARCH=watchdog >RAM:watchdog.txt".
 *
 * With 'nodePath' (NODE=), that single node (or property) and everything
 * below it, in full, exactly like selecting it in the tree and using *Save
 * node as...*. With 'search' (SEARCH=) instead, the whole tree filtered
 * exactly as the search gadget would: matching entries and their ancestors.
 * A typical remote session runs SEARCH first to find the interesting node's
 * path, then NODE to pull that node whole. Always with full names, since a
 * path to nowhere on screen is the only context a plain text stream can
 * offer. NODE wins if somehow both are given.
 *
 ******************************************************************************/

BOOL DoExport(CONST_STRPTR nodePath, CONST_STRPTR search)
{
    struct Writer writer;
    BOOL ok = FALSE;

    dumpFullNames   = TRUE;
    searchFilter[0] = '\0';

    if ((nodePath != NULL) && (nodePath[0] != '\0'))
    {
        const of_node_t * node = NULL;
        const of_property_t * prop = NULL;

        if (!ResolvePath(DTBase->dt_Root, nodePath, &node, &prop))
        {
            PutStr("No such node or property.\n");
            return (FALSE);
        }

        if (WriterOpenOutput(&writer))
        {
            if (prop != NULL)
            {
                DumpOneProperty(&writer, node, prop, 0);
            }
            else
            {
                DumpOneNode(&writer, node, 0);
            }

            ok = WriterClose(&writer);
        }
    }
    else
    {
        StringCopy(searchFilter, search, (LONG)sizeof(searchFilter));

        if (WriterOpenOutput(&writer))
        {
            DumpNode(&writer, DTBase->dt_Root, 0);

            ok = WriterClose(&writer);
        }
    }

    if (!ok)
    {
        PutStr("The device tree could not be written to standard output.\n");
    }

    return (ok);
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
