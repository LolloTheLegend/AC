// protos for ALL external functions in cube...

#ifndef PROTOS_H
#define PROTOS_H

#include "world.h"

extern sqr *world, *wmip[];             // map data, the mips are sequential 2D arrays in memory
extern header hdr;                      // current map header
extern int sfactor, ssize;              // ssize = 2^sfactor
extern int cubicsize, mipsize;          // cubicsize = ssize^2
extern vector<bounceent *> bounceents;
extern int interm;
extern int gamespeed;
extern int stenciling, stencilshadow;
extern hashtable<char *, enet_uint32> mapinfo;
extern int numspawn[3], maploaded, numflagspawn[2];
extern int verbose;

#define AC_VERSION 1202
#define AC_MASTER_URI "ms.cubers.net"
#define AC_MASTER_PORT 28760
#define AC_MASTER_HTTP 1 // default
#define AC_MASTER_RAW 0
#define MAXCL 16
#define CONFIGROTATEMAX 5               // keep 5 old versions of saved.cfg and init.cfg around



#ifndef STANDALONE

extern bool hasTE, hasMT, hasMDA, hasDRE, hasstencil, hasST2, hasSTW, hasSTS, hasAF;


struct color
{
    float r, g, b, alpha;
    color(){}
    color(float r, float g, float b) : r(r), g(g), b(b), alpha(1.0f) {}
    color(float r, float g, float b, float a) : r(r), g(g), b(b), alpha(a) {}
};

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

// serverbrowser
extern void addserver(const char *servername, int serverport, int weight);
extern char *getservername(int n);
extern bool resolverwait(const char *name, ENetAddress *address);
extern int connectwithtimeout(ENetSocket sock, const char *hostname, ENetAddress &remoteaddress);
extern void writeservercfg();
extern void refreshservers(void *menu, bool init);
extern bool serverskey(void *menu, int code, bool isdown, int unicode);
extern bool serverinfokey(void *menu, int code, bool isdown, int unicode);

struct serverinfo
{
    enum { UNRESOLVED = 0, RESOLVING, RESOLVED };

    string name;
    string full;
    string map;
    string sdesc;
    string description;
    string cmd;
    int mode, numplayers, maxclients, ping, protocol, minremain, resolved, port, lastpingmillis, pongflags, getinfo, menuline_from, menuline_to;
    ENetAddress address;
    vector<const char *> playernames;
    uchar namedata[MAXTRANS];
    vector<char *> infotexts;
    uchar textdata[MAXTRANS];
    char lang[3];
    color *bgcolor;
    int favcat, msweight, weight;

    int uplinkqual, uplinkqual_age;
    unsigned char uplinkstats[MAXCLIENTS + 1];

    serverinfo()
     : mode(0), numplayers(0), maxclients(0), ping(9999), protocol(0), minremain(0),
       resolved(UNRESOLVED), port(-1), lastpingmillis(0), pongflags(0), getinfo(EXTPING_NOP),
       bgcolor(NULL), favcat(-1), msweight(0), weight(0), uplinkqual(0), uplinkqual_age(0)
    {
        name[0] = full[0] = map[0] = sdesc[0] = description[0] = '\0';
        loopi(3) lang[i] = '\0';
        loopi(MAXCLIENTS + 1) uplinkstats[i] = 0;
    }
};

extern serverinfo *getconnectedserverinfo();
extern void pingservers();
extern void updatefrommaster(int force);

// rendergl
extern void cleangl();

// shadow
extern bool addshadowbox(const vec &bbmin, const vec &bbmax, const vec &extrude, const glmatrixf &mat);
extern void drawstencilshadows();

static inline Texture *lookupworldtexture(int tex, bool trydl = true)
{ return lookuptexture(tex, noworldtexture, trydl); }

struct zone { int x1, x2, y1, y2, color; }; // zones (drawn on the minimap)

// water
extern void setwatercolor(const char *r = "", const char *g = "", const char *b = "", const char *a = "");
extern void calcwaterscissor();
extern void addwaterquad(int x, int y, int size);
extern int renderwater(float hf, GLuint reflecttex, GLuint refracttex);
extern void resetwater();

// client
extern void neterr(const char *s);
extern void changeteam(int team, bool respawn = true); // deprecated?

// serverms
bool requestmasterf(const char *fmt, ...); // for AUTH et al
//moving to server.cpp seems a bad idea.
// :for AUTH

// clientgame
extern void changemap(const char *name);
//game mode extras

extern void arenarespawn();
extern void serveropcommand(int cmd, int arg1);

extern void displayvote(votedisplayinfo *v);
extern void voteresult(int v);

// world
extern void setupworld(int factor);
extern void sqrdefault(sqr *s);
extern bool worldbordercheck(int x1, int x2, int y1, int y2, int z1, int z2);
extern void calcmapdims();
extern bool empty_world(int factor, bool force);
extern void remip(const block &b, int level = 0);
extern void remipmore(const block &b, int level = 0);
extern int closestent();
extern int findtype(char *what);
extern int findentity(int type, int index = 0);
extern int findentity(int type, int index, uchar attr2);
extern entity *newentity(int index, int x, int y, int z, char *what, int v1, int v2, int v3, int v4);
extern void mapmrproper(bool manual);

// worldlight
extern int lastcalclight;

extern void fullbrightlight(int level = -1);
extern void calclight();
extern void adddynlight(physent *owner, const vec &o, int reach, int expire, int fade, uchar r, uchar g = 0, uchar b = 0);
extern void dodynlights();
extern void undodynlights();
extern void cleardynlights();
extern void removedynlights(physent *owner);
extern block *blockcopy(const block &b);
extern void blockpaste(const block &b, int bx, int by, bool light);
extern void blockpaste(const block &b);
extern void freeblockp(block *b);
extern void freeblock(block *&b);
extern block *duplicateblock(const block *s);

// worldrender
extern void render_world(float vx, float vy, float vh, float changelod, int yaw, int pitch, float fov, float fovy, int w, int h);
extern int lod_factor();

// worldocull
extern void disableraytable();
extern void computeraytable(float vx, float vy, float fov);
extern int isoccluded(float vx, float vy, float cx, float cy, float csize);

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

// worldio
extern const char *setnames(const char *name);
extern void save_world(char *mname, bool skipoptimise = false, bool addcomfort = false);
extern bool load_world(char *mname);
extern void writemap(char *name, int size, uchar *data);
extern void writecfggz(char *name, int size, int sizegz, uchar *data);
extern uchar *readmap(char *name, int *size, int *revision);
extern uchar *readmcfggz(char *name, int *size, int *sizegz);
extern void rlencodecubes(vector<uchar> &f, sqr *s, int len, bool preservesolids);
extern void rldecodecubes(ucharbuf &f, sqr *s, int len, int version, bool silent);
extern void clearheaderextras();
extern void xmapbackup(const char *nickprefix, const char *nick);
extern void writeallxmaps();
extern int loadallxmaps();

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
extern void fatal(const char *s, ...);
extern void initserver(bool dedicated, int argc = 0, char **argv = NULL);
extern void cleanupserver();
extern void localconnect();
extern void localdisconnect();
extern void localclienttoserver(int chan, ENetPacket *);
extern void serverslice(uint timeout);

extern void startintermission();
extern void restoreserverstate(vector<entity> &ents);
extern string mastername;
extern int masterport, mastertype;
extern ENetSocket connectmaster();
extern uchar *retrieveservers(uchar *buf, int buflen);
extern void serverms(int mode, int numplayers, int minremain, char *smapname, int millis, const ENetAddress &localaddr, int *mnum, int *msend, int *mrec, int *cnum, int *csend, int *crec, int protocol_version);
extern const char *genpwdhash(const char *name, const char *pwd, int salt);
extern void servermsinit(const char *master, const char *ip, int serverport, bool listen);
extern bool serverpickup(int i, int sender);
extern bool valid_client(int cn);
extern void extinfo_cnbuf(ucharbuf &p, int cn);
extern void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len, int *csend);
extern void extinfo_teamscorebuf(ucharbuf &p);
extern int wizardmain(int argc, char **argv);

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
#define DEFDEMOFILEFMT "%w_%h_%n_%Mmin_%G"
#define DEFDEMOTIMEFMT "%Y%m%d_%H%M"

extern bool watchingdemo;
extern int demoprotocol;
extern void enddemoplayback();

// logging

enum { ACLOG_DEBUG = 0, ACLOG_VERBOSE, ACLOG_INFO, ACLOG_WARNING, ACLOG_ERROR, ACLOG_NUM };

extern bool initlogging(const char *identity, int facility_, int consolethres, int filethres, int syslogthres, bool logtimestamp);
extern void exitlogging();
extern bool logline(int level, const char *msg, ...);

// server config

struct serverconfigfile
{
    string filename;
    int filelen;
    char *buf;
    serverconfigfile() : filelen(0), buf(NULL) { filename[0] = '\0'; }
    virtual ~serverconfigfile() { DELETEA(buf); }

    virtual void read() {}
    void init(const char *name);
    bool load();
};

// server commandline parsing
struct servercommandline
{
    int uprate, serverport, syslogfacility, filethres, syslogthres, maxdemos, maxclients, kickthreshold, banthreshold, verbose, incoming_limit, afk_limit, ban_time, demotimelocal;
    const char *ip, *master, *logident, *serverpassword, *adminpasswd, *demopath, *maprot, *pwdfile, *blfile, *nbfile, *infopath, *motdpath, *forbidden, *killmessages, *demofilenameformat, *demotimestampformat;
    bool logtimestamp, demo_interm, loggamestatus;
    string motd, servdesc_full, servdesc_pre, servdesc_suf, voteperm, mapperm;
    int clfilenesting;
    vector<const char *> adminonlymaps;

    servercommandline() :   uprate(0), serverport(CUBE_DEFAULT_SERVER_PORT), syslogfacility(6), filethres(-1), syslogthres(-1), maxdemos(5),
                            maxclients(DEFAULTCLIENTS), kickthreshold(-5), banthreshold(-6), verbose(0), incoming_limit(10), afk_limit(45000), ban_time(20*60*1000), demotimelocal(0),
                            ip(""), master(NULL), logident(""), serverpassword(""), adminpasswd(""), demopath(""),
                            maprot("config/maprot.cfg"), pwdfile("config/serverpwd.cfg"), blfile("config/serverblacklist.cfg"), nbfile("config/nicknameblacklist.cfg"),
                            infopath("config/serverinfo"), motdpath("config/motd"), forbidden("config/forbidden.cfg"), killmessages("config/serverkillmessages.cfg"),
                            logtimestamp(false), demo_interm(false), loggamestatus(true),
                            clfilenesting(0)
    {
        motd[0] = servdesc_full[0] = servdesc_pre[0] = servdesc_suf[0] = voteperm[0] = mapperm[0] = '\0';
        demofilenameformat = DEFDEMOFILEFMT;
        demotimestampformat = DEFDEMOTIMEFMT;
    }

    bool checkarg(const char *arg)
    {
        if(!strncmp(arg, "assaultcube://", 13)) return false;
        else if(arg[0] != '-' || arg[1] == '\0') return false;
        const char *a = arg + 2 + strspn(arg + 2, " ");
        int ai = atoi(a);
        // client: dtwhzbsave
        switch(arg[1])
        {
            case '-':
                    if(!strncmp(arg, "--demofilenameformat=", 21))
                    {
                        demofilenameformat = arg+21;
                    }
                    else if(!strncmp(arg, "--demotimestampformat=", 22))
                    {
                        demotimestampformat = arg+22;
                    }
                    else if(!strncmp(arg, "--demotimelocal=", 16))
                    {
                        int ai = atoi(arg+16);
                        demotimelocal = ai == 0 ? 0 : 1;
                    }
                    else if(!strncmp(arg, "--masterport=", 13))
                    {
                        int ai = atoi(arg+13);
                        masterport = ai == 0 ? AC_MASTER_PORT : ai;
                    }
                    else if(!strncmp(arg, "--mastertype=", 13))
                    {
                        int ai = atoi(arg+13);
                        mastertype = ai > 0 ? 1 : 0;
                    }
                    else return false;
                    break;
            case 'u': uprate = ai; break;
            case 'f': if(ai > 0 && ai < 65536) serverport = ai; break;
            case 'i': ip     = a; break;
            case 'm': master = a; break;
            case 'N': logident = a; break;
            case 'l': loggamestatus = ai != 0; break;
            case 'F': if(isdigit(*a) && ai >= 0 && ai <= 7) syslogfacility = ai; break;
            case 'T': logtimestamp = true; break;
            case 'L':
                switch(*a)
                {
                    case 'F': filethres = atoi(a + 1); break;
                    case 'S': syslogthres = atoi(a + 1); break;
                }
                break;
            case 'A': if(*a) adminonlymaps.add(a); break;
            case 'c': if(ai > 0) maxclients = min(ai, MAXCLIENTS); break;
            case 'k':
            {
                if(arg[2]=='A' && arg[3]!='\0')
                {
                    if ((ai = atoi(&arg[3])) >= 30) afk_limit = ai * 1000;
                    else afk_limit = 0;
                }
                else if(arg[2]=='B' && arg[3]!='\0')
                {
                    if ((ai = atoi(&arg[3])) >= 0) ban_time = ai * 60 * 1000;
                    else ban_time = 0;
                }
                else if(ai < 0) kickthreshold = ai;
                break;
            }
            case 'y': if(ai < 0) banthreshold = ai; break;
            case 'x': adminpasswd = a; break;
            case 'p': serverpassword = a; break;
            case 'D':
            {
                if(arg[2]=='I')
                {
                    demo_interm = true;
                }
                else if(ai > 0) maxdemos = ai; break;
            }
            case 'W': demopath = a; break;
            case 'r': maprot = a; break;
            case 'X': pwdfile = a; break;
            case 'B': blfile = a; break;
            case 'K': nbfile = a; break;
            case 'E': killmessages = a; break;
            case 'I': infopath = a; break;
            case 'o': filterrichtext(motd, a); break;
            case 'O': motdpath = a; break;
            case 'g': forbidden = a; break;
            case 'n':
            {
                char *t = servdesc_full;
                switch(*a)
                {
                    case '1': t = servdesc_pre; a += 1 + strspn(a + 1, " "); break;
                    case '2': t = servdesc_suf; a += 1 + strspn(a + 1, " "); break;
                }
                filterrichtext(t, a);
                filtertext(t, t, FTXT__SERVDESC);
                break;
            }
            case 'P': concatstring(voteperm, a); break;
            case 'M': concatstring(mapperm, a); break;
            case 'Z': if(ai >= 0) incoming_limit = ai; break;
            case 'V': verbose++; break;
            case 'C': if(*a && clfilenesting < 3)
            {
                serverconfigfile cfg;
                cfg.init(a);
                cfg.load();
                int line = 1;
                clfilenesting++;
                if(cfg.buf)
                {
                    printf("reading commandline parameters from file '%s'\n", a);
                    for(char *p = cfg.buf, *l; p < cfg.buf + cfg.filelen; line++)
                    {
                        l = p; p += strlen(p) + 1;
                        for(char *c = p - 2; c > l; c--) { if(*c == ' ') *c = '\0'; else break; }
                        l += strspn(l, " \t");
                        if(*l && !this->checkarg(newstring(l)))
                            printf("unknown parameter in file '%s', line %d: '%s'\n", cfg.filename, line, l);
                    }
                }
                else printf("failed to read file '%s'\n", a);
                clfilenesting--;
                break;
            }
            default: return false;
        }
        return true;
    }
};

// shotty: shotgun rays def
struct sgray {
    int ds; // damage flag: outer, medium, center: SGSEGDMG_*
    vec rv; // ray vector
};

#endif	// PROTOS_H

