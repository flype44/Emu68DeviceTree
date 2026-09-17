/******************************************************************************
 * 
 * DeviceTree.c
 * 
 ******************************************************************************/

#include <exec/types.h>
#include <proto/exec.h>
#include "DeviceTree.h"
#include "Utils.h"

/******************************************************************************
 *
 * GLOBALS
 *
 ******************************************************************************/

CONST_STRPTR typeNames[ENTRY_TYPE_COUNT] =
{
    (CONST_STRPTR)"node",
    (CONST_STRPTR)"empty",
    (CONST_STRPTR)"string",
    (CONST_STRPTR)"long",
    (CONST_STRPTR)"quad",
    (CONST_STRPTR)"blob"
};

/******************************************************************************
 *
 * CountProperties()
 *
 ******************************************************************************/

ULONG CountProperties(const of_property_t * prop)
{
    ULONG count = 0;

    while (prop)
    {
        count++;
        prop = prop->op_next;
    }

    return (count);
}

/******************************************************************************
 *
 * CountNodes()
 *
 ******************************************************************************/

ULONG CountNodes(const of_node_t * node)
{
    ULONG count = 0;

    while (node)
    {
        count++;
        node = node->on_next;
    }

    return (count);
}

/******************************************************************************
 *
 * CountSubItems()
 *
 ******************************************************************************/

ULONG CountSubItems(const of_node_t * node)
{
    if (node == NULL)
    {
        return (0);
    }

    return (CountNodes(node->on_children) + 
        CountProperties(node->on_properties));
}

/******************************************************************************
 * 
 * EntryType()
 * 
 ******************************************************************************/

ULONG EntryType(const of_property_t * prop)
{
    CONST_STRPTR bytes;

    if (prop == NULL)
    {
        return (ENTRY_TYPE_EMPTY);
    }

    bytes = (CONST_STRPTR)prop->op_value;

    if ((prop->op_length == 0) || (bytes == NULL))
    {
        return (ENTRY_TYPE_EMPTY);
    }

    if (IsAsciiValue(bytes, prop->op_length))
    {
        return (ENTRY_TYPE_STRING);
    }

    if (prop->op_length == sizeof(ULONG))
    {
        return (ENTRY_TYPE_LONG);
    }

    if (prop->op_length == (2 * sizeof(ULONG)))
    {
        return (ENTRY_TYPE_QUAD);
    }

    return (ENTRY_TYPE_BLOB);
}

/******************************************************************************
 * 
 * FormatType()
 * 
 ******************************************************************************/

VOID FormatType(const of_property_t * prop, STRPTR buffer, LONG size)
{
    ULONG type = EntryType(prop);
    CONST_STRPTR name = typeNames[type];

    if (type == ENTRY_TYPE_STRING)
    {
        if (StringCount((CONST_STRPTR)prop->op_value, prop->op_length) > 1)
        {
            name = (CONST_STRPTR)"string list";
        }
    }

    StringCopy(buffer, name, size);
}

/******************************************************************************
 * 
 * FormatValue()
 * 
 ******************************************************************************/

VOID FormatValue(const of_property_t * prop, STRPTR buffer, LONG size)
{
    CONST_STRPTR bytes;
    ULONG length;

    buffer[0] = '\0';

    if (prop == NULL)
    {
        buffer[0] = '-';
        buffer[1] = '\0';
        return;
    }

    bytes  = (CONST_STRPTR)prop->op_value;
    length = prop->op_length;

    if ((length == 0) || (bytes == NULL))
    {
        buffer[0] = '-';
        buffer[1] = '\0';
    }
    else if (IsAsciiValue(bytes, length))
    {
        ULONG strings = StringCount(bytes, length);

        if (strings > 1)
        {
            SPrintf(buffer, size, (CONST_STRPTR)"%lu strings", strings);
        }
        else
        {
            StringCopy(buffer, (CONST_STRPTR)bytes, size);
        }
    }
    else if (length == sizeof(ULONG))
    {
        ULONG value;

        CopyMem((APTR)bytes, (APTR)&value, sizeof(ULONG));

        SPrintf(buffer, size, (CONST_STRPTR)"0x%08lx (%lu)", value, value);
    }
    else if (length == (2 * sizeof(ULONG)))
    {
        ULONG high, low;

        CopyMem((APTR)bytes, (APTR)&high, sizeof(ULONG));
        CopyMem((APTR)(bytes + sizeof(ULONG)), (APTR)&low, sizeof(ULONG));

        if (high == 0)
        {
            SPrintf(buffer, size, (CONST_STRPTR)"0x%08lx (%lu)", low, low);
        }
        else
        {
            SPrintf(buffer, size, (CONST_STRPTR)"0x%08lx%08lx", high, low);
        }
    }
    else
    {
        ULONG i;
        ULONG n = (length > 8) ? 8 : length;
        LONG pos = 0;

        for (i = 0; i < n; i++)
        {
            if ((pos + 4) >= size)
            {
                break;
            }

            SPrintf(&buffer[pos], size - pos, (CONST_STRPTR)"%02lx ", (ULONG)bytes[i]);

            pos += 3;
        }

        if (length > 8)
        {
            SPrintf(&buffer[pos], size - pos, (CONST_STRPTR)"...");
        }
        else if (pos > 0)
        {
            buffer[pos - 1] = '\0';
        }
    }
}

/******************************************************************************
 *
 * END OF FILE
 *
 ******************************************************************************/
