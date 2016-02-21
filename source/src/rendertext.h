#ifndef RENDERTEXT_H_
#define RENDERTEXT_H_

#include "tools.h"
#include "texture.h"

// rendertext
struct font
{
	struct charinfo
	{
		short x, y, w, h;
	};

	struct utf8charinfo : charinfo
	{
		int code;
	};

	char *name;
	Texture *tex;
	vector<charinfo> chars;
	int defaultw, defaulth;
	int offsetx, offsety, offsetw, offseth;
	int skip;
};

extern int VIRTW;   // virtual screen size for text & HUD
extern bool ignoreblinkingbit;
extern font *curfont;
extern string myfont;

bool setfont ( const char *name );
font *getfont ( const char *name );
void pushfont ( const char *name );
void popfont ();
int text_width ( const char *str );
void draw_text ( const char *str,
                 int left,
                 int top,
                 int r = 255,
                 int g = 255,
                 int b = 255,
                 int a = 255,
                 int cursor = -1,
                 int maxwidth = -1 );
void draw_textf ( const char *fstr, int left, int top, ... );
void text_startcolumns ();
void text_endcolumns ();
int text_visible ( const char *str, int max );
void text_pos ( const char *str, int cursor, int &cx, int &cy, int maxwidth );
void text_bounds ( const char *str,
                   int &width,
                   int &height,
                   int maxwidth = -1 );
int text_visible ( const char *str, int hitx, int hity, int maxwidth );
void reloadfonts ();

#endif /* RENDERTEXT_H_ */
