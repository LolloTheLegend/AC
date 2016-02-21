// protos for ALL external functions in cube...

#ifndef PROTOS_H
#define PROTOS_H

#include "world.h"


extern header hdr;                      // current map header
extern int sfactor, ssize;              // ssize = 2^sfactor
extern int cubicsize, mipsize;          // cubicsize = ssize^2
extern vector<bounceent *> bounceents;
extern int interm;
extern int gamespeed;
extern int stenciling, stencilshadow;
extern int verbose;





#ifndef STANDALONE

extern bool hasTE, hasMT, hasMDA, hasDRE, hasstencil, hasST2, hasSTW, hasSTS, hasAF;




struct authkey // for AUTH
{
    char *name, *key, *desc;
    int lastauth;

    authkey(const char *name, const char *key, const char *desc)
        : name(newstring(name)), key(newstring(key)), desc(newstring(desc)),
          lastauth(0)
    {
    }

    ~authkey()
    {
        DELETEA(name);
        DELETEA(key);
        DELETEA(desc);
    }
};


struct mitem
{
    struct gmenu *parent;
    color *bgcolor;

    mitem(gmenu *parent, color *bgcolor, int type) : parent(parent), bgcolor(bgcolor), type(type) {}
    virtual ~mitem() {}

    virtual void render(int x, int y, int w);
    virtual int width() = 0;
    virtual void select() {}
    virtual void focus(bool on) { }
    virtual void key(int code, bool isdown, int unicode) { }
    virtual void init() {}
    virtual const char *getdesc() { return NULL; }
    virtual const char *gettext() { return NULL; }
    virtual const char *getaction() { return NULL; }
    bool isselection();
    void renderbg(int x, int y, int w, color *c);
    static color gray, white, whitepulse;
    int type;

    enum { TYPE_TEXTINPUT, TYPE_KEYINPUT, TYPE_CHECKBOX, TYPE_MANUAL, TYPE_SLIDER };
};

struct mdirlist
{
    char *dir, *ext, *action;
    bool image;
    ~mdirlist()
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
    void (__cdecl *refreshfunc)(void *, bool);
    bool (__cdecl *keyfunc)(void *, int, bool, int);
    char *initaction;
    char *usefont;
    bool allowblink;
    const char *mdl;
    int anim, rotspeed, scale;
    int footlen;
    mdirlist *dirlist;

    gmenu() : name(0), title(0), header(0), footer(0), initaction(0), usefont(0), allowblink(0), mdl(0), footlen(0), dirlist(0) {}
    virtual ~gmenu()
    {
        DELETEA(name);
        DELETEA(mdl);
        DELETEP(dirlist);
        DELETEA(initaction);
        DELETEA(usefont);
        items.deletecontents();
    }

    void render();
    void renderbg(int x1, int y1, int x2, int y2, bool border);
    void conprintmenu();
    void refresh();
    void open();
    void close();
    void init();
};

struct mline { string name, cmd; };

// rendergl
extern void cleangl();

static inline Texture *lookupworldtexture(int tex, bool trydl = true)
{ return lookuptexture(tex, noworldtexture, trydl); }

struct zone { int x1, x2, y1, y2, color; }; // zones (drawn on the minimap)

// client
extern void neterr(const char *s);
extern void changeteam(int team, bool respawn = true); // deprecated?

// clientgame
extern void changemap(const char *name);
//game mode extras

extern void arenarespawn();
extern void serveropcommand(int cmd, int arg1);

extern void displayvote(votedisplayinfo *v);
extern void voteresult(int v);

// main
extern SDL_Surface *screen;
extern int colorbits, depthbits, stencilbits;

extern void keyrepeat(bool on);
extern bool interceptkey(int sym);
extern bool firstrun, inmainloop;


#define VIRTH 1800
#define FONTH (curfont->defaulth)
#define PIXELTAB (10*curfont->defaultw)

extern void initfont();


// renderhud
enum
{
    HUDMSG_INFO = 0,
    HUDMSG_TIMER,
    HUDMSG_MIPSTATS,
    HUDMSG_EDITFOCUS,

    HUDMSG_TYPE = 0xFF,
    HUDMSG_OVERWRITE = 1<<8
};

// renderparticles
enum
{
    PT_PART = 0,
    PT_FIREBALL,
    PT_SHOTLINE,
    PT_DECAL,
    PT_BULLETHOLE,
    PT_BLOOD,
    PT_STAIN,
    PT_FLASH,
    PT_HUDFLASH
};

#define PT_DECAL_MASK ((1<<PT_DECAL)|(1<<PT_BULLETHOLE)|(1<<PT_STAIN))

enum
{
    PART_SPARK = 0,
    PART_SMOKE,
    PART_ECLOSEST,
    PART_BLOOD,
    PART_DEMOTRACK,
    PART_FIREBALL,
    PART_SHOTLINE,
    PART_BULLETHOLE,
    PART_BLOODSTAIN,
    PART_SCORCH,
    PART_HUDMUZZLEFLASH,
    PART_MUZZLEFLASH,
    PART_ELIGHT,
    PART_ESPAWN,
    PART_EAMMO,
    PART_EPICKUP,
    PART_EMODEL,
    PART_ECARROT,
    PART_ELADDER,
    PART_EFLAG
};

// TODO: Move this to tools.h where it belongs to
extern mapdim mapdims;

// Structure for storing traceresults
struct traceresult_s
{
     vec end;
     bool collided;
};
void TraceLine(vec from, vec to, dynent *pTracer, bool CheckPlayers, traceresult_s *tr, bool SkipTags=false);
extern int burstshotssettings[NUMGUNS];

#endif

// protocol [client and server]



// command
extern bool per_idents, neverpersist;
extern void changescriptcontext(int newcontext);
extern int curscontext();
extern int screenshottype;

// server


extern void startintermission();
extern int masterport, mastertype;
extern uchar *retrieveservers(uchar *buf, int buflen);
extern const char *genpwdhash(const char *name, const char *pwd, int salt);


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






// logging

enum { ACLOG_DEBUG = 0, ACLOG_VERBOSE, ACLOG_INFO, ACLOG_WARNING, ACLOG_ERROR, ACLOG_NUM };

extern bool initlogging(const char *identity, int facility_, int consolethres, int filethres, int syslogthres, bool logtimestamp);
extern void exitlogging();
extern bool logline(int level, const char *msg, ...);

// server config


// server commandline parsing


// shotty: shotgun rays def
struct sgray {
    int ds; // damage flag: outer, medium, center: SGSEGDMG_*
    vec rv; // ray vector
};

#endif	// PROTOS_H

