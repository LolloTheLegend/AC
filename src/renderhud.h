#ifndef RENDERHUD_H
#define RENDERHUD_H

#include "entities.h"
#include "geom.h"
#include "tools.h"

// TODO: Lollo: Fix this
struct color;

enum
{
	HUDMSG_INFO = 0, HUDMSG_TIMER, HUDMSG_MIPSTATS, HUDMSG_EDITFOCUS,

	HUDMSG_TYPE = 0xFF, HUDMSG_OVERWRITE = 1 << 8
};

enum
{
	CROSSHAIR_DEFAULT = 0,
	CROSSHAIR_TEAMMATE,
	CROSSHAIR_SCOPE,
	CROSSHAIR_KNIFE,
	CROSSHAIR_PISTOL,
	CROSSHAIR_CARBINE,
	CROSSHAIR_SHOTGUN,
	CROSSHAIR_SMG,
	CROSSHAIR_SNIPER,
	CROSSHAIR_AR,
	CROSSHAIR_CPISTOL,
	CROSSHAIR_GRENADES,
	CROSSHAIR_AKIMBO,
	CROSSHAIR_NUM,
};

extern const char *crosshairnames[CROSSHAIR_NUM];
extern int clockdisplay, clockcount;
extern string gtime;

void drawscope ( bool preload = false );
void drawcrosshair ( playerent *p, int n, struct color *c = NULL, float size =
        -1.0f );
void updatedmgindicator ( vec &attack );
void hudoutf ( const char *s, ... );
void hudonlyf ( const char *s, ... );
void hudeditf ( int type, const char *s, ... );
vec getradarpos ();
void damageblend ( int n );
void gl_drawhud ( int w,
                  int h,
                  int curfps,
                  int nquads,
                  int curvert,
                  bool underwater );
void loadingscreen ( const char *fmt = NULL, ... );
void show_out_of_renderloop_progress ( float bar1,
                                       const char *text1,
                                       float bar2 = 0,
                                       const char *text2 = NULL );

#endif /* RENDERHUD_H */
