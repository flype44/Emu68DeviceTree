#ifndef GUI_H
#define GUI_H

/******************************************************************************
 *
 * GUI.h
 *
 * The MUI application: object tree, menus, hooks and the main event loop.
 * Only ever used from a Workbench launch (see main()/RunGUI(), in
 * Emu68DeviceTree.c) -- a Shell gets SEARCH=/NODE=/HELP instead.
 *
 ******************************************************************************/

/* PROTOTYPES */

BOOL       OpenLibs(VOID);
VOID       CloseLibs(VOID);

ObjApp_t * CreateApp(VOID);
VOID       DisposeApp(ObjApp_t * object);
VOID       ProcessEvents(VOID);

#endif /* GUI_H */
