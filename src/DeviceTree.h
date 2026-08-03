#ifndef DEVICETREE_H
#define DEVICETREE_H

#define DEVICETREE_NAME "devicetree.resource"

/* ENUMERATIONS */

#define ENTRY_TYPE_NODE    (0)
#define ENTRY_TYPE_EMPTY   (1)
#define ENTRY_TYPE_STRING  (2)
#define ENTRY_TYPE_LONG    (3)
#define ENTRY_TYPE_QUAD    (4)
#define ENTRY_TYPE_BLOB    (5)
#define ENTRY_TYPE_COUNT   (6)

/* STRUCTURES */

typedef struct of_property {
    struct of_property * op_next;
    const char *         op_name;
    ULONG                op_length;
    const void *         op_value;
} of_property_t;

typedef struct of_node {
    struct of_node *     on_next;
    struct of_node *     on_parent;
    const char *         on_name;
    struct of_node *     on_children;
    of_property_t *      on_properties;
} of_node_t;

struct DeviceTreeBase {
    struct Library       dt_Node;
    struct ExecBase *    dt_ExecBase;
    of_node_t *          dt_Root;
    CONST_STRPTR         dt_StrNull;
    ULONG *              dt_Data;
    CONST_STRPTR         dt_Strings;
};

/* PROTOTYPES */

ULONG CountNodes(of_node_t * node);
ULONG CountSubItems(of_node_t * node);
ULONG CountProperties(of_property_t * prop);
ULONG EntryType(of_property_t * prop);
VOID  FormatType(of_property_t * prop, STRPTR buffer, LONG size);
VOID  FormatValue(of_property_t * prop, STRPTR buffer, LONG size);

#endif // DEVICETREE_H
