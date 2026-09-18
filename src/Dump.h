#ifndef DUMP_H
#define DUMP_H

/******************************************************************************
 *
 * Dump.h
 *
 * Write the device tree, or a piece of it, to a struct Writer: the shared
 * machinery behind the GUI's "Save node as..." (DoSaveNode(), in GUI.c) and
 * the headless SEARCH=/NODE= command line mode (DoExport(), called from
 * main()). Nothing here touches MUI.
 *
 ******************************************************************************/

/* GLOBALS */

/* Whether a dump writes each node's own name or its full path (the "Use full
   names" menu item's checked state, read into this by DoSaveNode(); always
   TRUE for DoExport(), a path to nowhere on screen being the only context a
   plain text stream can offer) */

extern BOOL dumpFullNames;

/* The tree filter, applied both while the GUI tree is being built
   (InsertNodes()/InsertProperties(), in GUI.c) and while a dump writes it:
   empty matches everything, otherwise only entries containing this text
   (case insensitive, substring) and their ancestors, so a match stays
   reachable in context */

extern UBYTE searchFilter[128];

/* PROTOTYPES */

BOOL PropertyMatchesFilter(const of_property_t * prop);
BOOL NodeMatchesFilter(const of_node_t * node);

VOID DumpValue(struct Writer * writer, const of_property_t * prop);
VOID DumpOneProperty(struct Writer * writer, const of_node_t * owner,
         const of_property_t * prop, ULONG depth);
VOID DumpOneNode(struct Writer * writer, const of_node_t * node, ULONG depth);
VOID DumpNode(struct Writer * writer, const of_node_t * node, ULONG depth);

BOOL ResolvePath(const of_node_t * root, CONST_STRPTR path,
         const of_node_t ** outNode, const of_property_t ** outProp);
BOOL DoExport(CONST_STRPTR nodePath, CONST_STRPTR search);

#endif /* DUMP_H */
