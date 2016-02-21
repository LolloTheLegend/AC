#ifndef COMMON_H_
#define COMMON_H_

#include "entities.h"

#define AC_VERSION 1202
#define AC_MASTER_URI "ms.cubers.net"
#define AC_MASTER_PORT 28760
#define AC_MASTER_HTTP 1 // default
#define AC_MASTER_RAW 0
#define MAXCL 16
#define CONFIGROTATEMAX 5               // keep 5 old versions of saved.cfg and init.cfg around

#define VIRTH 1800
#define FONTH (curfont->defaulth)
#define PIXELTAB (10*curfont->defaultw)

#ifndef STANDALONE
#include "varray.h"
enum
{
	SDL_AC_BUTTON_WHEELDOWN = -5,
	SDL_AC_BUTTON_WHEELUP = -4,
	SDL_AC_BUTTON_RIGHT = -3,
	SDL_AC_BUTTON_MIDDLE = -2,
	SDL_AC_BUTTON_LEFT = -1
};
#endif

#include "tools.h"

struct color
{
	float r, g, b, alpha;
	color ()
	{
	}
	color ( float r, float g, float b ) :
			r(r), g(g), b(b), alpha(1.0f)
	{
	}
	color ( float r, float g, float b, float a ) :
			r(r), g(g), b(b), alpha(a)
	{
	}
};

struct authkey
{
	char *name, *key, *desc;
	int lastauth;

	authkey ( const char *name, const char *key, const char *desc ) :
			name(newstring(name)), key(newstring(key)), desc(newstring(desc)),
			        lastauth(0)
	{
	}

	~authkey ()
	{
		DELETEA(name);
		DELETEA(key);
		DELETEA(desc);
	}
};

// demo
#define DHDR_DESCCHARS 80
#define DHDR_PLISTCHARS 322
struct demoheader
{
	char magic[16];
	int version, protocol;
	char desc[DHDR_DESCCHARS];
	char plist[DHDR_PLISTCHARS];
};

#define PT_DECAL_MASK ((1<<PT_DECAL)|(1<<PT_BULLETHOLE)|(1<<PT_STAIN))
struct traceresult_s
{
	vec end;
	bool collided;
};
void TraceLine ( vec from,
                 vec to,
                 dynent *pTracer,
                 bool CheckPlayers,
                 traceresult_s *tr,
                 bool SkipTags = false );
extern int burstshotssettings[NUMGUNS];

#endif /* COMMON_H_ */
