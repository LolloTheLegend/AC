#ifndef SOURCE_SRC_MENUS_H_
#define SOURCE_SRC_MENUS_H_

#include "tools.h"
#include <SDL/SDL_keysym.h>

struct color;

extern void* applymenu;

void menuset(void *m, bool save = true);
void showmenu(const char *name, bool top = true);
void closemenu(const char *name);
void menuselect(void *menu, int sel);
bool menuvisible();
void rendermenu();
void menureset(void *menu);
void menumanual(void *menu, char *text, char *action = NULL, color *bgcolor = NULL, const char *desc = NULL);
void menuimagemanual(void *menu, const char *filename1, const char *filename2, char *text, char *action = NULL, color *bgcolor = NULL, const char *desc = NULL);
void menutitle(void *menu, const char *title = NULL);
void menuheader(void *menu, char *header = NULL, char *footer = NULL);
bool menukey(int code, bool isdown, int unicode, SDLMod mod = KMOD_NONE);
void *addmenu(const char *name, const char *title = NULL, bool allowinput = true, void (__cdecl *refreshfunc)(void *, bool) = NULL, bool (__cdecl *keyfunc)(void *, int, bool, int) = NULL, bool hotkeys = false, bool forwardkeys = false);
void rendermenumdl();
void addchange(const char *desc, int type);
void clearchanges(int type);
void refreshapplymenu(void *menu, bool init);

#endif /* SOURCE_SRC_MENUS_H_ */
