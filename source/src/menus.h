#ifndef SOURCE_SRC_MENUS_H_
#define SOURCE_SRC_MENUS_H_

#include "tools.h"
#include "common.h"
#include <SDL/SDL_keysym.h>

struct mitem
{
	struct gmenu *parent;
	color *bgcolor;

	mitem ( gmenu *parent, color *bgcolor, int type ) :
			parent(parent), bgcolor(bgcolor), type(type)
	{
	}
	virtual ~mitem ()
	{
	}

	virtual void render ( int x, int y, int w );
	virtual int width () = 0;
	virtual void select ()
	{
	}
	virtual void focus ( bool on )
	{
	}
	virtual void key ( int code, bool isdown, int unicode )
	{
	}
	virtual void init ()
	{
	}
	virtual const char *getdesc ()
	{
		return NULL;
	}
	virtual const char *gettext ()
	{
		return NULL;
	}
	virtual const char *getaction ()
	{
		return NULL;
	}
	bool isselection ();
	void renderbg ( int x, int y, int w, color *c );
	static color gray, white, whitepulse;
	int type;

	enum
	{
		TYPE_TEXTINPUT, TYPE_KEYINPUT, TYPE_CHECKBOX, TYPE_MANUAL, TYPE_SLIDER
	};
};

struct mdirlist
{
	char *dir, *ext, *action;
	bool image;
	~mdirlist ()
	{
		DELETEA(dir);
		DELETEA(ext);
		DELETEA(action);
	}
};

struct gmenu
{
	const char *name, *title, *header, *footer;
	vector<mitem *> items;
	int mwidth;
	int menusel;
	bool allowinput, inited, hotkeys, forwardkeys;
	void (__cdecl *refreshfunc) ( void *, bool );
	bool (__cdecl *keyfunc) ( void *, int, bool, int );
	char *initaction;
	char *usefont;
	bool allowblink;
	const char *mdl;
	int anim, rotspeed, scale;
	int footlen;
	mdirlist *dirlist;

	gmenu () :
			name(0), title(0), header(0), footer(0), initaction(0), usefont(0),
			        allowblink(0), mdl(0), footlen(0), dirlist(0)
	{
	}
	virtual ~gmenu ()
	{
		DELETEA(name);
		DELETEA(mdl);
		DELETEP(dirlist);
		DELETEA(initaction);
		DELETEA(usefont);
		items.deletecontents();
	}

	void render ();
	void renderbg ( int x1, int y1, int x2, int y2, bool border );
	void conprintmenu ();
	void refresh ();
	void open ();
	void close ();
	void init ();
};

struct mline
{
	string name, cmd;
};

extern void* applymenu;

void menuset ( void *m, bool save = true );
void showmenu ( const char *name, bool top = true );
void closemenu ( const char *name );
void menuselect ( void *menu, int sel );
bool menuvisible ();
void rendermenu ();
void menureset ( void *menu );
void menumanual ( void *menu, char *text, char *action = NULL, color *bgcolor =
        NULL,
                  const char *desc = NULL );
void menuimagemanual ( void *menu,
                       const char *filename1,
                       const char *filename2,
                       char *text,
                       char *action = NULL,
                       color *bgcolor = NULL,
                       const char *desc = NULL );
void menutitle ( void *menu, const char *title = NULL );
void menuheader ( void *menu, char *header = NULL, char *footer = NULL );
bool menukey ( int code, bool isdown, int unicode, SDLMod mod = KMOD_NONE );
void *addmenu ( const char *name,
                const char *title = NULL,
                bool allowinput = true,
                void (__cdecl *refreshfunc) ( void *, bool ) = NULL,
                bool (__cdecl *keyfunc) ( void *, int, bool, int ) = NULL,
                bool hotkeys = false,
                bool forwardkeys = false );
void rendermenumdl ();
void addchange ( const char *desc, int type );
void clearchanges ( int type );
void refreshapplymenu ( void *menu, bool init );

#endif /* SOURCE_SRC_MENUS_H_ */
