// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "server.h"
#include "cube.h"

#ifdef STANDALONE
#define DEBUGCOND (true)
#else
static VARP(serverdebug, 0, 0, 1);
#define DEBUGCOND (serverdebug==1)
#endif

#define gamemode smode   // allows the gamemode macros to work with the server mode

#define SERVER_PROTOCOL_VERSION    (PROTOCOL_VERSION)    // server without any gameplay modification
//#define SERVER_PROTOCOL_VERSION   (-PROTOCOL_VERSION)  // server with gameplay modification but compatible to vanilla client (using /modconnect)
//#define SERVER_PROTOCOL_VERSION  (PROTOCOL_VERSION)    // server with incompatible protocol (change PROTOCOL_VERSION in file protocol.h to a negative number!)

#define valid_flag(f) (f >= 0 && f < 2)

enum { GE_NONE = 0, GE_SHOT, GE_EXPLODE, GE_HIT, GE_AKIMBO, GE_RELOAD, GE_SUICIDE, GE_PICKUP };
enum { ST_EMPTY, ST_LOCAL, ST_TCPIP };

int smode = 0, nextgamemode;

struct shotevent
{
    int type;
    int millis, id;
    int gun;
    float from[3], to[3];
};

struct explodeevent
{
    int type;
    int millis, id;
    int gun;
};

struct hitevent
{
    int type;
    int target;
    int lifesequence;
    union
    {
        int info;
        float dist;
    };
    float dir[3];
};

struct suicideevent
{
    int type;
};

struct pickupevent
{
    int type;
    int ent;
};

struct akimboevent
{
    int type;
    int millis, id;
};

struct reloadevent
{
    int type;
    int millis, id;
    int gun;
};

union gameevent
{
    int type;
    shotevent shot;
    explodeevent explode;
    hitevent hit;
    suicideevent suicide;
    pickupevent pickup;
    akimboevent akimbo;
    reloadevent reload;
};

template <int N>
struct projectilestate
{
    int projs[N];
    int numprojs;

    projectilestate() : numprojs(0) {}

    void reset() { numprojs = 0; }

    void add(int val)
    {
        if(numprojs>=N) numprojs = 0;
        projs[numprojs++] = val;
    }

    bool remove(int val)
    {
        loopi(numprojs) if(projs[i]==val)
        {
            projs[i] = projs[--numprojs];
            return true;
        }
        return false;
    }
};

static const int DEATHMILLIS = 300;

struct clientstate : playerstate
{
    vec o;
    int state;
    int lastdeath, lastspawn, spawn, lifesequence;
    bool forced;
    int lastshot;
    projectilestate<8> grenades;
    int akimbomillis;
    bool scoped;
    int flagscore, frags, teamkills, deaths, shotdamage, damage, points, events, lastdisc, reconnections;

    clientstate() : state(CS_DEAD) {}

    bool isalive(int gamemillis)
    {
        return state==CS_ALIVE || (state==CS_DEAD && gamemillis - lastdeath <= DEATHMILLIS);
    }

    bool waitexpired(int gamemillis)
    {
        int wait = gamemillis - lastshot;
        loopi(NUMGUNS) if(wait < gunwait[i]) return false;
        return true;
    }

    void reset()
    {
        state = CS_DEAD;
        lifesequence = -1;
        grenades.reset();
        akimbomillis = 0;
        scoped = forced = false;
        flagscore = frags = teamkills = deaths = shotdamage = damage = points = events = lastdisc = reconnections = 0;
        respawn();
    }

    void respawn()
    {
        playerstate::respawn();
        o = vec(-1e10f, -1e10f, -1e10f);
        lastdeath = 0;
        lastspawn = -1;
        spawn = 0;
        lastshot = 0;
        akimbomillis = 0;
        scoped = false;
    }
};

struct savedscore
{
    string name;
    uint ip;
    int frags, flagscore, deaths, teamkills, shotdamage, damage, team, points, events, lastdisc, reconnections;
    bool valid, forced;

    void reset()
    {
        // to avoid 2 connections with the same score... this can disrupt some laggers that eventually produces 2 connections (but it is rare)
        frags = flagscore = deaths = teamkills = shotdamage = damage = points = events = lastdisc = reconnections = 0;
    }

    void save(clientstate &cs, int t)
    {
        frags = cs.frags;
        flagscore = cs.flagscore;
        deaths = cs.deaths;
        teamkills = cs.teamkills;
        shotdamage = cs.shotdamage;
        damage = cs.damage;
        points = cs.points;
        forced = cs.forced;
        events = cs.events;
        lastdisc = cs.lastdisc;
        reconnections = cs.reconnections;
        team = t;
        valid = true;
    }

    void restore(clientstate &cs)
    {
        cs.frags = frags;
        cs.flagscore = flagscore;
        cs.deaths = deaths;
        cs.teamkills = teamkills;
        cs.shotdamage = shotdamage;
        cs.damage = damage;
        cs.points = points;
        cs.forced = forced;
        cs.events = events;
        cs.lastdisc = lastdisc;
        cs.reconnections = reconnections;
        reset();
    }
};

struct medals
{
    int dpt, lasthit, lastgun, ncovers, nhs;
    int combohits, combo, combofrags, combotime, combodamage, ncombos;
    int ask, askmillis, linked, linkmillis, linkreason, upmillis, flagmillis;
    int totalhits, totalshots;
    bool updated, combosend;
    vec pos, flagpos;
    void reset()
    {
        dpt = lasthit = lastgun = ncovers = nhs = 0;
        combohits = combo = combofrags = combotime = combodamage = ncombos = 0;
        askmillis = linkmillis = upmillis = flagmillis = 0;
        linkreason = linked = ask = -1;
        totalhits = totalshots = 0;
        updated = combosend = false;
        pos = flagpos = vec(-1e10f, -1e10f, -1e10f);
    }
};

struct client                   // server side version of "dynent" type
{
    int type;
    int clientnum;
    ENetPeer *peer;
    string hostname;
    string name;
    int team;
    char lang[3];
    int ping;
    int skin[2];
    int vote;
    int role;
    int connectmillis, lmillis, ldt, spj;
    int mute, spam, lastvc; // server side voice comm spam control
    int acversion, acbuildtype;
    bool isauthed; // for passworded servers
    bool haswelcome;
    bool isonrightmap, loggedwrongmap, freshgame;
    bool timesync;
    int overflow;
    int gameoffset, lastevent, lastvotecall;
    int lastprofileupdate, fastprofileupdates;
    int demoflags;
    clientstate state;
    vector<gameevent> events;
    vector<uchar> position, messages;
    string lastsaytext;
    int saychars, lastsay, spamcount, badspeech, badmillis;
    int at3_score, at3_lastforce, eff_score;
    bool at3_dontmove;
    int spawnindex;
    int spawnperm, spawnpermsent;
	bool autospawn;
    int salt;
    string pwd;
    uint authreq; // for AUTH
    string authname; // for AUTH
    int mapcollisions, farpickups;
    enet_uint32 bottomRTT;
    medals md;
    bool upspawnp;
    int lag;
    vec spawnp;
    int nvotes;
    int input, inputmillis;
    int ffire, wn, f, g, t, y, p;
    int yb, pb, oy, op, lda, ldda, fam;
    int nt[10], np, lp, ls, lsm, ld, nd, nlt, lem, led;
    vec cp[10], dp[10], d0, lv, lt, le;
    float dda, tr, sda;
    int ps, ph, tcn, bdt, pws;
    float pr;
    int yls, pls, tls;
    int bs, bt, blg, bp;

    gameevent &addevent()
    {
        static gameevent dummy;
        if(events.length()>100) return dummy;
        return events.add();
    }

    void mapchange(bool getmap = false)
    {
        state.reset();
        events.setsize(0);
        overflow = 0;
        timesync = false;
        isonrightmap = m_coop;
        spawnperm = SP_WRONGMAP;
        spawnpermsent = servmillis;
		autospawn = false;
        if(!getmap)
        {
            loggedwrongmap = false;
            freshgame = true;         // permission to spawn at once
        }
        lastevent = 0;
        at3_lastforce = eff_score = 0;
        mapcollisions = farpickups = 0;
        md.reset();
        upspawnp = false;
        lag = 0;
        spawnp = vec(-1e10f, -1e10f, -1e10f);
        lmillis = ldt = spj = 0;
        ffire = 0;
        f = g = y = p = t = 0;
        yb = pb = oy = op = lda = ldda = fam = 0;
        np = lp = ls = lsm = ld = nd = nlt = lem = led = 0;
        d0 = lv = lt = le = vec(0,0,0);
        loopi(10) { cp[i] = dp[i] = vec(0,0,0); nt[i] = 0; }
        dda = tr = sda = 0;
        ps = ph = bdt = pws = 0;
        tcn = -1;
        pr = 0.0f;
        yls = pls = tls = 0;
    }

    void reset()
    {
        name[0] = pwd[0] = demoflags = 0;
        bottomRTT = ping = 9999;
        team = TEAM_SPECT;
        state.state = CS_SPECTATE;
        loopi(2) skin[i] = 0;
        position.setsize(0);
        messages.setsize(0);
        isauthed = haswelcome = false;
        role = CR_DEFAULT;
        lastvotecall = 0;
        lastprofileupdate = fastprofileupdates = 0;
        vote = VOTE_NEUTRAL;
        lastsaytext[0] = '\0';
        saychars = 0;
        spawnindex = -1;
        authreq = 0; // for AUTH
        mapchange();
        freshgame = false;         // don't spawn into running games
        mute = spam = lastvc = badspeech = badmillis = nvotes = 0;
        input = inputmillis = 0;
        wn = -1;
        bs = bt = blg = bp = 0;
    }

    void zap()
    {
        type = ST_EMPTY;
        role = CR_DEFAULT;
        isauthed = haswelcome = false;
    }
};

struct ban
{
    ENetAddress address;
    int millis, type;
};

struct worldstate
{
    enet_uint32 uses;
    vector<uchar> positions, messages;
};

struct server_entity            // server side version of "entity" type
{
    int type;
    bool spawned, legalpickup, twice;
    int spawntime;
    short x, y;
};

struct clientidentity
{
    uint ip;
    int clientnum;
};

struct demofile
{
    string info;
    string file;
    uchar *data;
    int len;
    vector<clientidentity> clientssent;
};

static void startgame(const char *newname, int newmode, int newtime = -1, bool notify = true);
static void disconnect_client(int n, int reason = -1);
static void sendiplist(int receiver, int cn = -1);
static int clienthasflag(int cn);
static bool refillteams(bool now = false, int ftr = FTR_AUTOTEAM);
static void changeclientrole(int client, int role, char *pwd = NULL, bool force=false);
static mapstats *getservermapstats(const char *mapname, bool getlayout = false, int *maploc = NULL);
static int findmappath(const char *mapname, char *filename = NULL);
static int calcscores();
static void recordpacket(int chan, void *data, int len);
static void senddisconnectedscores(int cn);
static void process(ENetPacket *packet, int sender, int chan);
static void welcomepacket(packetbuf &p, int n);
static void sendwelcome(client *cl, int chan = 1);
static void sendpacket(int n, int chan, ENetPacket *packet, int exclude = -1, bool demopacket = false);
static int numclients();
static bool updateclientteam(int cln, int newteam, int ftr);
static void forcedeath(client *cl);
static void sendf(int cn, int chan, const char *format, ...);

bool isdedicated;
string smapname;
mapstats smapstats;
char *maplayout = NULL, *testlayout = NULL;


static const char *messagenames[SV_NUM] =
{
    "SV_SERVINFO", "SV_WELCOME", "SV_INITCLIENT", "SV_POS", "SV_POSC", "SV_POSN", "SV_TEXT", "SV_TEAMTEXT", "SV_TEXTME", "SV_TEAMTEXTME", "SV_TEXTPRIVATE",
    "SV_SOUND", "SV_VOICECOM", "SV_VOICECOMTEAM", "SV_CDIS",
    "SV_SHOOT", "SV_EXPLODE", "SV_SUICIDE", "SV_AKIMBO", "SV_RELOAD", "SV_AUTHT", "SV_AUTHREQ", "SV_AUTHTRY", "SV_AUTHANS", "SV_AUTHCHAL",
    "SV_GIBDIED", "SV_DIED", "SV_GIBDAMAGE", "SV_DAMAGE", "SV_HITPUSH", "SV_SHOTFX", "SV_THROWNADE",
    "SV_TRYSPAWN", "SV_SPAWNSTATE", "SV_SPAWN", "SV_SPAWNDENY", "SV_FORCEDEATH", "SV_RESUME",
    "SV_DISCSCORES", "SV_TIMEUP", "SV_EDITENT", "SV_ITEMACC",
    "SV_MAPCHANGE", "SV_ITEMSPAWN", "SV_ITEMPICKUP",
    "SV_PING", "SV_PONG", "SV_CLIENTPING", "SV_GAMEMODE",
    "SV_EDITMODE", "SV_EDITH", "SV_EDITT", "SV_EDITS", "SV_EDITD", "SV_EDITE", "SV_NEWMAP",
    "SV_SENDMAP", "SV_RECVMAP", "SV_REMOVEMAP",
    "SV_SERVMSG", "SV_ITEMLIST", "SV_WEAPCHANGE", "SV_PRIMARYWEAP",
    "SV_FLAGACTION", "SV_FLAGINFO", "SV_FLAGMSG", "SV_FLAGCNT",
    "SV_ARENAWIN",
    "SV_SETADMIN", "SV_SERVOPINFO",
    "SV_CALLVOTE", "SV_CALLVOTESUC", "SV_CALLVOTEERR", "SV_VOTE", "SV_VOTERESULT",
    "SV_SETTEAM", "SV_TEAMDENY", "SV_SERVERMODE",
    "SV_IPLIST",
    "SV_LISTDEMOS", "SV_SENDDEMOLIST", "SV_GETDEMO", "SV_SENDDEMO", "SV_DEMOPLAYBACK",
    "SV_CONNECT",
    "SV_SWITCHNAME", "SV_SWITCHSKIN", "SV_SWITCHTEAM",
    "SV_CLIENT",
    "SV_EXTENSION",
    "SV_MAPIDENT", "SV_HUDEXTRAS", "SV_POINTS"
};

const char *entnames[MAXENTTYPES] =
{
    "none?",
    "light", "playerstart", "pistol", "ammobox","grenades",
    "health", "helmet", "armour", "akimbo",
    "mapmodel", "trigger", "ladder", "ctf-flag", "sound", "clip", "plclip"
};

// see entity.h:61: struct itemstat { int add, start, max, sound; };
// Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
itemstat ammostats[NUMGUNS] =
{
    {  1,  1,   1,  S_ITEMAMMO  },   // knife dummy
    { 20, 60, 100,  S_ITEMAMMO  },   // pistol
    { 15, 30,  30,  S_ITEMAMMO  },   // carbine
    { 14, 28,  21,  S_ITEMAMMO  },   // shotgun
    { 60, 90,  90,  S_ITEMAMMO  },   // subgun
    { 10, 20,  15,  S_ITEMAMMO  },   // sniper
    { 40, 60,  60,  S_ITEMAMMO  },   // assault
    { 30, 45,  75,  S_ITEMAMMO  },   // cpistol
    {  1,  0,   3,  S_ITEMAMMO  },   // grenade
    {100,  0, 100,  S_ITEMAKIMBO}    // akimbo
};

itemstat powerupstats[I_ARMOUR-I_HEALTH+1] =
{
    {33, 0, 100, S_ITEMHEALTH}, // 0 health
    {25, 0, 100, S_ITEMHELMET}, // 1 helmet
    {50, 0, 100, S_ITEMARMOUR}, // 2 armour
};

guninfo guns[NUMGUNS] =
{
    // Please update ./ac_website/htdocs/docs/introduction.html if these figures change.
    //mKR: mdl_kick_rot && mKB: mdl_kick_back
    //reI: recoilincrease && reB: recoilbase && reM: maxrecoil && reF: recoilbackfade
    //pFX: pushfactor
    //modelname                   reload       attackdelay      piercing     part     recoil       mKR       reI          reM           pFX
    //              sound                reloadtime        damage    projspeed  spread     magsize     mKB        reB             reF           isauto
    { "knife",      S_KNIFE,      S_NULL,     0,      500,    50, 100,     0,   0,  1,    1,   1,    0,  0,   0,   0,      0,      0,   1,      false },
    { "pistol",     S_PISTOL,     S_RPISTOL,  1400,   160,    18,   0,     0,   0, 53,   10,   10,   6,  5,   6,  35,     58,     125,  1,      false },
    { "carbine",    S_CARBINE,    S_RCARBINE, 1800,   720,    60,  40,     0,   0, 10,   60,   10,   4,  4,  10,  60,     60,     150,  1,      false },
    { "shotgun",    S_SHOTGUN,    S_RSHOTGUN, 2400,   880,    1,    0,     0,   0,  1,   35,    7,   9,  9,  10, 140,    140,    125,   1,      false },   // CAUTION dmg only sane for server!
    { "subgun",     S_SUBGUN,     S_RSUBGUN,  1650,   80,     16,   0,     0,   0, 45,   15,   30,   1,  2,   5,  25,     50,     188,  1,      true  },
    { "sniper",     S_SNIPER,     S_RSNIPER,  1950,   1500,   82,  25,     0,   0, 50,   50,    5,   4,  4,  10,  85,     85,     100,  1,      false },
    { "assault",    S_ASSAULT,    S_RASSAULT, 2000,   120,    22,   0,     0,   0, 18,   30,   20,   0,  2,   3,  25,     50,     115,  1,      true  },
    { "cpistol",    S_PISTOL,     S_RPISTOL,  1400,   120,    19,   0,     0,   0, 35,   10,   15,   6,  5,   6,  35,     50,     125,  1,      false },   // temporary
    { "grenade",    S_NULL,       S_NULL,     1000,   650,    200,  0,    20,  6,  1,    1,   1,    3,   1,    0,   0,      0,      0,   3,      false },
    { "pistol",     S_PISTOL,     S_RAKIMBO,  1400,   80,     19,   0,     0,   0, 50,   10,   20,   6,  5,   4,  15,     25,     115,  1,      true  },
};

const char *teamnames[TEAM_NUM+1] = {"CLA", "RVSF", "CLA-SPECT", "RVSF-SPECT", "SPECTATOR", "void"};
const char *teamnames_s[TEAM_NUM+1] = {"CLA", "RVSF", "CSPC", "RSPC", "SPEC", "void"};

// for both client and server
// default messages are hardcoded !
char killmessages[2][NUMGUNS][MAXKILLMSGLEN] = {{ "", "busted", "picked off", "peppered", "sprayed", "punctured", "shredded", "busted", "", "busted" }, { "slashed", "", "", "splattered", "", "headshot", "", "", "gibbed", "" }};

struct servercontroller
{
    virtual void start() = 0;
    virtual void keepalive() = 0;
    virtual void stop() = 0;
    virtual ~servercontroller() {}
    int argc;
    char **argv;
};

#ifdef WIN32

struct winservice : servercontroller
{
    SERVICE_STATUS_HANDLE statushandle;
    SERVICE_STATUS status;
    HANDLE stopevent;
    const char *name;

    winservice(const char *name) : name(name)
    {
        callbacks::svc = this;
        statushandle = 0;
    };

    ~winservice()
    {
        if(status.dwCurrentState != SERVICE_STOPPED) stop();
        callbacks::svc = NULL;
    }

    void start() // starts the server again on a new thread and returns once the windows service has stopped
    {
        SERVICE_TABLE_ENTRY dispatchtable[] = { { (LPSTR)name, (LPSERVICE_MAIN_FUNCTION)callbacks::main }, { NULL, NULL } };
        if(StartServiceCtrlDispatcher(dispatchtable)) exit(EXIT_SUCCESS);
        else fatal("an error occurred running the AC server as windows service. make sure you start the server from the service control manager and not from the command line.");
    }

    void keepalive()
    {
        if(statushandle)
        {
            report(SERVICE_RUNNING, 0);
            handleevents();
        }
    };

    void stop()
    {
        if(statushandle)
        {
            report(SERVICE_STOP_PENDING, 0);
            if(stopevent) CloseHandle(stopevent);
            status.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
            report(SERVICE_STOPPED, 0);
        }
    }

    void handleevents()
    {
        if(WaitForSingleObject(stopevent, 0) == WAIT_OBJECT_0) stop();
    }

    void WINAPI requesthandler(DWORD ctrl)
    {
        switch(ctrl)
        {
            case SERVICE_CONTROL_STOP:
                report(SERVICE_STOP_PENDING, 0);
                SetEvent(stopevent);
                return;
            default: break;
        }
        report(status.dwCurrentState, 0);
    }

    void report(DWORD state, DWORD wait)
    {
        status.dwCurrentState = state;
        status.dwWaitHint = wait;
        status.dwWin32ExitCode = NO_ERROR;
        status.dwControlsAccepted = SERVICE_START_PENDING == state || SERVICE_STOP_PENDING == state ? 0 : SERVICE_ACCEPT_STOP;
        if(state == SERVICE_RUNNING || state == SERVICE_STOPPED) status.dwCheckPoint = 0;
        else status.dwCheckPoint++;
        SetServiceStatus(statushandle, &status);
    }

    int WINAPI svcmain() // new server thread's entry point
    {
        // fix working directory to make relative paths work
        if(argv && argv[0])
        {
            string procpath;
            copystring(procpath, parentdir(argv[0]));
            copystring(procpath, parentdir(procpath));
            SetCurrentDirectory((LPSTR)procpath);
        }

        statushandle = RegisterServiceCtrlHandler(name, (LPHANDLER_FUNCTION)callbacks::requesthandler);
        if(!statushandle) return EXIT_FAILURE;
        status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        status.dwServiceSpecificExitCode = 0;
        report(SERVICE_START_PENDING, 3000);
        stopevent = CreateEvent(NULL, true, false, NULL);
        if(!stopevent) { stop(); return EXIT_FAILURE; }
        extern int main(int argc, char **argv);
        return main(argc, argv);
    }


    struct callbacks
    {
        static winservice *svc;
        static int WINAPI main() { return svc->svcmain(); };
        static void WINAPI requesthandler(DWORD request) { svc->requesthandler(request); };
    };

}
winservice *winservice::callbacks::svc = (winservice *)NULL;

#endif

#define SERVERMAP_PATH          "packages/maps/servermaps/"
#define SERVERMAP_PATH_BUILTIN  "packages/maps/official/"
#define SERVERMAP_PATH_INCOMING "packages/maps/servermaps/incoming/"

#define GZBUFSIZE ((MAXCFGFILESIZE * 11) / 10)

struct servermapbuffer  // sending of maps between clients
{
    string mapname;
    int cgzsize, cfgsize, cfgsizegz, revision, datasize;
    uchar *data, *gzbuf;

    servermapbuffer() : data(NULL) { gzbuf = new uchar[GZBUFSIZE]; }
    ~servermapbuffer() { delete[] gzbuf; }

    void clear() { DELETEA(data); revision = 0; }

    int available()
    {
        if(data && !strcmp(mapname, behindpath(smapname)) && cgzsize == smapstats.cgzsize)
        {
            if( !revision || (revision == smapstats.hdr.maprevision)) return cgzsize;
        }
        return 0;
    }

    void setrevision()
    {
        if(available() && !revision) revision = smapstats.hdr.maprevision;
    }

    void load(void)  // load currently played map into the buffer (if distributable), clear buffer otherwise
    {
        string cgzname, cfgname;
        const char *name = behindpath(smapname);   // no paths allowed here

        clear();
        formatstring(cgzname)(SERVERMAP_PATH "%s.cgz", name);
        path(cgzname);
        if(fileexists(cgzname, "r"))
        {
            formatstring(cfgname)(SERVERMAP_PATH "%s.cfg", name);
        }
        else
        {
            formatstring(cgzname)(SERVERMAP_PATH_INCOMING "%s.cgz", name);
            path(cgzname);
            formatstring(cfgname)(SERVERMAP_PATH_INCOMING "%s.cfg", name);
        }
        path(cfgname);
        uchar *cgzdata = (uchar *)loadfile(cgzname, &cgzsize);
        uchar *cfgdata = (uchar *)loadfile(cfgname, &cfgsize);
        if(cgzdata && (!cfgdata || cfgsize < MAXCFGFILESIZE))
        {
            uLongf gzbufsize = GZBUFSIZE;
            if(!cfgdata || compress2(gzbuf, &gzbufsize, cfgdata, cfgsize, 9) != Z_OK)
            {
                cfgsize = 0;
                gzbufsize = 0;
            }
            cfgsizegz = (int) gzbufsize;
            if(cgzsize + cfgsizegz < MAXMAPSENDSIZE)
            { // map is ok, fill buffer
                copystring(mapname, name);
                datasize = cgzsize + cfgsizegz;
                data = new uchar[datasize];
                memcpy(data, cgzdata, cgzsize);
                memcpy(data + cgzsize, gzbuf, cfgsizegz);
                logline(ACLOG_INFO,"loaded map %s, %d + %d(%d) bytes.", cgzname, cgzsize, cfgsize, cfgsizegz);
            }
        }
        DELETEA(cgzdata);
        DELETEA(cfgdata);
    }

    bool sendmap(const char *nmapname, int nmapsize, int ncfgsize, int ncfgsizegz, uchar *ndata)
    {
        FILE *fp;
        bool written = false;

        if(!nmapname[0] || nmapsize <= 0 || ncfgsizegz < 0 || nmapsize + ncfgsizegz > MAXMAPSENDSIZE || ncfgsize > MAXCFGFILESIZE) return false;  // malformed: probably modded client
        int cfgsize = ncfgsize;
        if(smode == GMODE_COOPEDIT && !strcmp(nmapname, behindpath(smapname)))
        { // update mapbuffer only in coopedit mode (and on same map)
            copystring(mapname, nmapname);
            datasize = nmapsize + ncfgsizegz;
            revision = 0;
            DELETEA(data);
            data = new uchar[datasize];
            memcpy(data, ndata, datasize);
        }

        defformatstring(name)(SERVERMAP_PATH_INCOMING "%s.cgz", nmapname);
        path(name);
        fp = fopen(name, "wb");
        if(fp)
        {
            fwrite(ndata, 1, nmapsize, fp);
            fclose(fp);
            formatstring(name)(SERVERMAP_PATH_INCOMING "%s.cfg", nmapname);
            path(name);
            fp = fopen(name, "wb");
            if(fp)
            {
                uLongf rawsize = ncfgsize;
                if(uncompress(gzbuf, &rawsize, ndata + nmapsize, ncfgsizegz) == Z_OK && rawsize - ncfgsize == 0)
                    fwrite(gzbuf, 1, cfgsize, fp);
                fclose(fp);
                written = true;
            }
        }
        return written;
    }

    void sendmap(client *cl, int chan)
    {
        if(!available()) return;
        packetbuf p(MAXTRANS + datasize, ENET_PACKET_FLAG_RELIABLE);
        putint(p, SV_RECVMAP);
        sendstring(mapname, p);
        putint(p, cgzsize);
        putint(p, cfgsize);
        putint(p, cfgsizegz);
        putint(p, revision);
        p.put(data, datasize);
        sendpacket(cl->clientnum, chan, p.finalize());
    }
};


// provide maps by the server

enum { MAP_NOTFOUND = 0, MAP_TEMP, MAP_CUSTOM, MAP_LOCAL, MAP_OFFICIAL, MAP_VOID };
static const char * const maplocstr[] = { "not found", "temporary", "custom", "local", "official", "void" };
#define readonlymap(x) ((x) >= MAP_CUSTOM)
#define distributablemap(x) ((x) == MAP_TEMP || (x) == MAP_CUSTOM)

static int findmappath(const char *mapname, char *filename)
{
    if(!mapname[0]) return MAP_NOTFOUND;
    string tempname;
    if(!filename) filename = tempname;
    const char *name = behindpath(mapname);
    formatstring(filename)(SERVERMAP_PATH_BUILTIN "%s.cgz", name);
    path(filename);
    int loc = MAP_NOTFOUND;
    if(getfilesize(filename) > 10) loc = MAP_OFFICIAL;
    else
    {
#ifndef STANDALONE
        copystring(filename, setnames(name));
        if(!isdedicated && getfilesize(filename) > 10) loc = MAP_LOCAL;
        else
        {
#endif
            formatstring(filename)(SERVERMAP_PATH "%s.cgz", name);
            path(filename);
            if(isdedicated && getfilesize(filename) > 10) loc = MAP_CUSTOM;
            else
            {
                formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cgz", name);
                path(filename);
                if(isdedicated && getfilesize(filename) > 10) loc = MAP_TEMP;
            }
#ifndef STANDALONE
        }
#endif
    }
    return loc;
}

static mapstats *getservermapstats(const char *mapname, bool getlayout, int *maploc)
{
    string filename;
    int ml;
    if(!maploc) maploc = &ml;
    *maploc = findmappath(mapname, filename);
    if(getlayout) DELETEA(maplayout);
    return *maploc == MAP_NOTFOUND ? NULL : loadmapstats(filename, getlayout);
}


// maprot.cfg

#define CONFIG_MAXPAR 6

struct configset
{
    string mapname;
    union
    {
        struct { int mode, time, vote, minplayer, maxplayer, skiplines; };
        int par[CONFIG_MAXPAR];
    };
};

static int FlagFlag = MINFF * 1000;
int Mvolume, Marea, SHhits, Mopen = 0;
float Mheight = 0;

static bool mapisok(mapstats *ms)
{
    if ( Mheight > MAXMHEIGHT ) { logline(ACLOG_INFO, "MAP CHECK FAIL: The overall ceil height is too high (%.1f cubes)", Mheight); return false; }
    if ( Mopen > MAXMAREA ) { logline(ACLOG_INFO, "MAP CHECK FAIL: There is a big open area in this (hint: use more solid walls)", Mheight); return false; }
    if ( SHhits > MAXHHITS ) { logline(ACLOG_INFO, "MAP CHECK FAIL: Too high height in some parts of the map (%d hits)", SHhits); return false; }

    if ( ms->hasflags ) // Check if flags are ok
    {
        struct { short x, y; } fl[2];
        loopi(2)
        {
            if(ms->flags[i] == 1)
            {
                short *fe = ms->entposs + ms->flagents[i] * 3;
                fl[i].x = *fe; fe++; fl[i].y = *fe;
            }
            else fl[i].x = fl[i].y = 0; // the map has no valid flags
        }
        FlagFlag = pow2(fl[0].x - fl[1].x) + pow2(fl[0].y - fl[1].y);
    }
    else FlagFlag = MINFF * 1000; // the map has no flags

    if ( FlagFlag < MINFF ) { logline(ACLOG_INFO, "MAP CHECK FAIL: The flags are too close to each other"); return false; }

    for (int i = 0; i < ms->hdr.numents; i++)
    {
        int v = ms->enttypes[i];
        if (v < I_CLIPS || v > I_AKIMBO) continue;
        short *p = &ms->entposs[i*3];
        float density = 0, hdensity = 0;
        for(int j = 0; j < ms->hdr.numents; j++)
        {
            int w = ms->enttypes[j];
            if (w < I_CLIPS || w > I_AKIMBO || i == j) continue;
            short *q = &ms->entposs[j*3];
            float r2 = 0;
            loopk(3){ r2 += (p[k]-q[k])*(p[k]-q[k]); }
            if ( r2 == 0.0f ) { logline(ACLOG_INFO, "MAP CHECK FAIL: Items too close %s %s (%hd,%hd)", entnames[v], entnames[w],p[0],p[1]); return false; }
            r2 = 1/r2;
            if (r2 < 0.0025f) continue;
            if (w != v)
            {
                hdensity += r2;
                continue;
            }
            density += r2;
        }
/*        if (hdensity > 0.0f) { logline(ACLOG_INFO, "ITEM CHECK H %s %f", entnames[v], hdensity); }
        if (density > 0.0f) { logline(ACLOG_INFO, "ITEM CHECK D %s %f", entnames[v], density); }*/
        if ( hdensity > 0.5f ) { logline(ACLOG_INFO, "MAP CHECK FAIL: Items too close %s %.2f (%hd,%hd)", entnames[v],hdensity,p[0],p[1]); return false; }
        switch(v)
        {
#define LOGTHISSWITCH(X) if( density > X ) { logline(ACLOG_INFO, "MAP CHECK FAIL: Items too close %s %.2f (%hd,%hd)", entnames[v],density,p[0],p[1]); return false; }
            case I_CLIPS:
            case I_HEALTH: LOGTHISSWITCH(0.24f); break;
            case I_AMMO: LOGTHISSWITCH(0.04f); break;
            case I_HELMET: LOGTHISSWITCH(0.02f); break;
            case I_ARMOUR:
            case I_GRENADE:
            case I_AKIMBO: LOGTHISSWITCH(0.005f); break;
            default: break;
#undef LOGTHISSWITCH
        }
    }
    return true;
}

struct servermaprot : serverconfigfile
{
    vector<configset> configsets;
    int curcfgset;

    servermaprot() : curcfgset(-1) {}

    void read()
    {
        if(getfilesize(filename) == filelen) return;
        configsets.shrink(0);
        if(!load()) return;

        const char *sep = ": ";
        configset c;
        int i, line = 0;
        char *l, *p = buf;
        logline(ACLOG_VERBOSE,"reading map rotation '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1; line++;
            l = strtok(l, sep);
            if(l)
            {
                copystring(c.mapname, behindpath(l));
                for(i = 3; i < CONFIG_MAXPAR; i++) c.par[i] = 0;  // default values
                for(i = 0; i < CONFIG_MAXPAR; i++)
                {
                    if((l = strtok(NULL, sep)) != NULL) c.par[i] = atoi(l);
                    else break;
                }
                if(i > 2)
                {
                    configsets.add(c);
                    logline(ACLOG_VERBOSE," %s, %s, %d minutes, vote:%d, minplayer:%d, maxplayer:%d, skiplines:%d", c.mapname, modestr(c.mode, false), c.time, c.vote, c.minplayer, c.maxplayer, c.skiplines);
                }
                else logline(ACLOG_INFO," error in line %d, file %s", line, filename);
            }
        }
        DELETEA(buf);
        logline(ACLOG_INFO,"read %d map rotation entries from '%s'", configsets.length(), filename);
        return;
    }

    int next(bool notify = true, bool nochange = false) // load next maprotation set
    {
#ifndef STANDALONE
        if(!isdedicated)
        {
            defformatstring(nextmapalias)("nextmap_%s", getclientmap());
            const char *map = getalias(nextmapalias);     // look up map in the cycle
            startgame(map && notify ? map : getclientmap(), getclientmode(), -1, notify);
            return -1;
        }
#endif
        if(configsets.empty()) fatal("maprot unavailable");
        int n = numclients();
        int csl = configsets.length();
        int ccs = curcfgset;
        if(ccs >= 0 && ccs < csl) ccs += configsets[ccs].skiplines;
        configset *c = NULL;
        loopi(3 * csl + 1)
        {
            ccs++;
            if(ccs >= csl || ccs < 0) ccs = 0;
            c = &configsets[ccs];
            if((n >= c->minplayer || i >= csl) && (!c->maxplayer || n <= c->maxplayer || i >= 2 * csl))
            {
                mapstats *ms = NULL;
                if((ms = getservermapstats(c->mapname)) && mapisok(ms)) break;
                else logline(ACLOG_INFO, "maprot error: map '%s' %s", c->mapname, (ms ? "does not satisfy some basic requirements" : "not found"));
            }
            if(i >= 3 * csl) fatal("maprot unusable"); // not a single map in rotation can be found...
        }
        if(!nochange)
        {
            curcfgset = ccs;
            startgame(c->mapname, c->mode, c->time, notify);
        }
        return ccs;
    }

    void restart(bool notify = true) // restart current map
    {
#ifndef STANDALONE
        if(!isdedicated)
        {
            startgame(getclientmap(), getclientmode(), -1, notify);
            return;
        }
#endif
    startgame(smapname, smode, -1, notify);
    }

    configset *current() { return configsets.inrange(curcfgset) ? &configsets[curcfgset] : NULL; }
    configset *get(int ccs) { return configsets.inrange(ccs) ? &configsets[ccs] : NULL; }
};

// serverblacklist.cfg

struct serveripblacklist : serverconfigfile
{
    vector<iprange> ipranges;

    void read()
    {
        if(getfilesize(filename) == filelen) return;
        ipranges.shrink(0);
        if(!load()) return;

        iprange ir;
        int line = 0, errors = 0;
        char *l, *r, *p = buf;
        logline(ACLOG_VERBOSE,"reading ip blacklist '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1; line++;
            if((r = (char *) atoipr(l, &ir)))
            {
                ipranges.add(ir);
                l = r;
            }
            if(l[strspn(l, " ")])
            {
                for(int i = (int)strlen(l) - 1; i > 0 && l[i] == ' '; i--) l[i] = '\0';
                logline(ACLOG_INFO," error in line %d, file %s: ignored '%s'", line, filename, l);
                errors++;
            }
        }
        DELETEA(buf);
        ipranges.sort(cmpiprange);
        int orglength = ipranges.length();
        loopv(ipranges)
        {
            if(!i) continue;
            if(ipranges[i].ur <= ipranges[i - 1].ur)
            {
                if(ipranges[i].lr == ipranges[i - 1].lr && ipranges[i].ur == ipranges[i - 1].ur)
                    logline(ACLOG_VERBOSE," blacklist entry %s got dropped (double entry)", iprtoa(ipranges[i]));
                else
                    logline(ACLOG_VERBOSE," blacklist entry %s got dropped (already covered by %s)", iprtoa(ipranges[i]), iprtoa(ipranges[i - 1]));
                ipranges.remove(i--); continue;
            }
            if(ipranges[i].lr <= ipranges[i - 1].ur)
            {
                logline(ACLOG_VERBOSE," blacklist entries %s and %s are joined due to overlap", iprtoa(ipranges[i - 1]), iprtoa(ipranges[i]));
                ipranges[i - 1].ur = ipranges[i].ur;
                ipranges.remove(i--); continue;
            }
        }
        loopv(ipranges) logline(ACLOG_VERBOSE," %s", iprtoa(ipranges[i]));
        logline(ACLOG_INFO,"read %d (%d) blacklist entries from '%s', %d errors", ipranges.length(), orglength, filename, errors);
    }

    bool check(enet_uint32 ip) // ip: network byte order
    {
        iprange t;
        t.lr = ENET_NET_TO_HOST_32(ip); // blacklist uses host byte order
        t.ur = 0;
        return ipranges.search(&t, cmpipmatch) != NULL;
    }
};

// nicknameblacklist.cfg

#define MAXNICKFRAGMENTS 5
enum { NWL_UNLISTED = 0, NWL_PASS, NWL_PWDFAIL, NWL_IPFAIL };

struct servernickblacklist : serverconfigfile
{
    struct iprchain     { struct iprange ipr; const char *pwd; int next; };
    struct blackline    { int frag[MAXNICKFRAGMENTS]; bool ignorecase; int line; void clear() { loopi(MAXNICKFRAGMENTS) frag[i] = -1; } };
    hashtable<const char *, int> whitelist;
    vector<iprchain> whitelistranges;
    vector<blackline> blacklines;
    vector<const char *> blfraglist;

    void destroylists()
    {
        whitelistranges.setsize(0);
        enumeratek(whitelist, const char *, key, delete key);
        whitelist.clear(false);
        blfraglist.deletecontents();
        blacklines.setsize(0);
    }

    void read()
    {
        if(getfilesize(filename) == filelen) return;
        destroylists();
        if(!load()) return;

        const char *sep = " ";
        int line = 1, errors = 0;
        iprchain iprc;
        blackline bl;
        char *l, *s, *r, *p = buf;
        logline(ACLOG_VERBOSE,"reading nickname blacklist '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1;
            l = strtok(l, sep);
            if(l)
            {
                s = strtok(NULL, sep);
                int ic = 0;
                if(s && (!strcmp(l, "accept") || !strcmp(l, "a")))
                { // accept nickname IP-range
                    int *i = whitelist.access(s);
                    if(!i) i = &whitelist.access(newstring(s), -1);
                    s += strlen(s) + 1;
                    while(s < p)
                    {
                        r = (char *) atoipr(s, &iprc.ipr);
                        s += strspn(s, sep);
                        iprc.pwd = r && *s ? NULL : newstring(s, strcspn(s, sep));
                        if(r || *s)
                        {
                            iprc.next = *i;
                            *i = whitelistranges.length();
                            whitelistranges.add(iprc);
                            s = r ? r : s + strlen(iprc.pwd);
                        }
                        else break;
                    }
                    s = NULL;
                }
                else if(s && (!strcmp(l, "block") || !strcmp(l, "b") || ic++ || !strcmp(l, "blocki") || !strcmp(l, "bi")))
                { // block nickname fragments (ic == ignore case)
                    bl.clear();
                    loopi(MAXNICKFRAGMENTS)
                    {
                        if(ic) strtoupper(s);
                        loopvj(blfraglist)
                        {
                            if(!strcmp(s, blfraglist[j])) { bl.frag[i] = j; break; }
                        }
                        if(bl.frag[i] < 0)
                        {
                            bl.frag[i] = blfraglist.length();
                            blfraglist.add(newstring(s));
                        }
                        s = strtok(NULL, sep);
                        if(!s) break;
                    }
                    bl.ignorecase = ic > 0;
                    bl.line = line;
                    blacklines.add(bl);
                }
                else { logline(ACLOG_INFO," error in line %d, file %s: unknown keyword '%s'", line, filename, l); errors++; }
                if(s && s[strspn(s, " ")]) { logline(ACLOG_INFO," error in line %d, file %s: ignored '%s'", line, filename, s); errors++; }
            }
            line++;
        }
        DELETEA(buf);
        logline(ACLOG_VERBOSE," nickname whitelist (%d entries):", whitelist.numelems);
        string text;
        enumeratekt(whitelist, const char *, key, int, idx,
        {
            text[0] = '\0';
            for(int i = idx; i >= 0; i = whitelistranges[i].next)
            {
                iprchain &ic = whitelistranges[i];
                if(ic.pwd) concatformatstring(text, "  pwd:\"%s\"", hiddenpwd(ic.pwd));
                else concatformatstring(text, "  %s", iprtoa(ic.ipr));
            }
            logline(ACLOG_VERBOSE, "  accept %s%s", key, text);
        });
        logline(ACLOG_VERBOSE," nickname blacklist (%d entries):", blacklines.length());
        loopv(blacklines)
        {
            text[0] = '\0';
            loopj(MAXNICKFRAGMENTS)
            {
                int k = blacklines[i].frag[j];
                if(k >= 0) { concatstring(text, " "); concatstring(text, blfraglist[k]); }
            }
            logline(ACLOG_VERBOSE, "  %2d block%s%s", blacklines[i].line, blacklines[i].ignorecase ? "i" : "", text);
        }
        logline(ACLOG_INFO,"read %d + %d entries from nickname blacklist file '%s', %d errors", whitelist.numelems, blacklines.length(), filename, errors);
    }

    int checkwhitelist(const client &c)
    {
        if(c.type != ST_TCPIP) return NWL_PASS;
        iprange ipr;
        ipr.lr = ENET_NET_TO_HOST_32(c.peer->address.host); // blacklist uses host byte order
        int *idx = whitelist.access(c.name);
        if(!idx) return NWL_UNLISTED; // no matching entry
        int i = *idx;
        bool needipr = false, iprok = false, needpwd = false, pwdok = false;
        while(i >= 0)
        {
            iprchain &ic = whitelistranges[i];
            if(ic.pwd)
            { // check pwd
                needpwd = true;
                if(pwdok || !strcmp(genpwdhash(c.name, ic.pwd, c.salt), c.pwd)) pwdok = true;
            }
            else
            { // check IP
                needipr = true;
                if(!cmpipmatch(&ipr, &ic.ipr)) iprok = true; // range match found
            }
            i = whitelistranges[i].next;
        }
        if(needpwd && !pwdok) return NWL_PWDFAIL; // wrong PWD
        if(needipr && !iprok) return NWL_IPFAIL; // wrong IP
        return NWL_PASS;
    }

    int checkblacklist(const char *name)
    {
        if(blacklines.empty()) return -2;  // no nickname blacklist loaded
        string nameuc;
        copystring(nameuc, name);
        strtoupper(nameuc);
        loopv(blacklines)
        {
            loopj(MAXNICKFRAGMENTS)
            {
                int k = blacklines[i].frag[j];
                if(k < 0) return blacklines[i].line; // no more fragments to check
                if(strstr(blacklines[i].ignorecase ? nameuc : name, blfraglist[k]))
                {
                    if(j == MAXNICKFRAGMENTS - 1) return blacklines[i].line; // all fragments match
                }
                else break; // this line no match
            }
        }
        return -1; // no match
    }
};

#define FORBIDDENSIZE 15
struct serverforbiddenlist : serverconfigfile
{
    int num;
    char entries[100][2][FORBIDDENSIZE+1]; // 100 entries and 2 words per entry is more than enough

    void initlist()
    {
        num = 0;
        memset(entries,'\0',2*100*(FORBIDDENSIZE+1));
    }

    void addentry(char *s)
    {
        int len = strlen(s);
        if ( len > 128 || len < 3 ) return;
        int n = 0;
        string s1, s2;
        char *c1 = s1, *c2 = s2;
        if (num < 100 && (n = sscanf(s,"%s %s",s1,s2)) > 0 ) // no warnings
        {
            strncpy(entries[num][0],c1,FORBIDDENSIZE);
            if ( n > 1 ) strncpy(entries[num][1],c2,FORBIDDENSIZE);
            else entries[num][1][0]='\0';
            num++;
        }
    }

    void read()
    {
        if(getfilesize(filename) == filelen) return;
        initlist();
        if(!load()) return;

        char *l, *p = buf;
        logline(ACLOG_VERBOSE,"reading forbidden list '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1;
            addentry(l);
        }
        DELETEA(buf);
    }

    bool canspeech(char *s)
    {
        for (int i=0; i<num; i++){
            if ( !findpattern(s,entries[i][0]) ) continue;
            else if ( entries[i][1][0] == '\0' || findpattern(s,entries[i][1]) ) return false;
        }
        return true;
    }
};

// serverpwd.cfg

#define ADMINPWD_MAXPAR 1
struct pwddetail
{
    string pwd;
    int line;
    bool denyadmin;    // true: connect only
};

static int passtime = 0, passtries = 0;
static enet_uint32 passguy = 0;

struct serverpasswords : serverconfigfile
{
    vector<pwddetail> adminpwds;
    int staticpasses;

    serverpasswords() : staticpasses(0) {}

    void init(const char *name, const char *cmdlinepass)
    {
        if(cmdlinepass[0])
        {
            pwddetail c;
            copystring(c.pwd, cmdlinepass);
            c.line = 0;   // commandline is 'line 0'
            c.denyadmin = false;
            adminpwds.add(c);
        }
        staticpasses = adminpwds.length();
        serverconfigfile::init(name);
    }

    void read()
    {
        if(getfilesize(filename) == filelen) return;
        adminpwds.shrink(staticpasses);
        if(!load()) return;

        pwddetail c;
        const char *sep = " ";
        int i, line = 1, par[ADMINPWD_MAXPAR];
        char *l, *p = buf;
        logline(ACLOG_VERBOSE,"reading admin passwords '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1;
            l = strtok(l, sep);
            if(l)
            {
                copystring(c.pwd, l);
                par[0] = 0;  // default values
                for(i = 0; i < ADMINPWD_MAXPAR; i++)
                {
                    if((l = strtok(NULL, sep)) != NULL) par[i] = atoi(l);
                    else break;
                }
                //if(i > 0)
                {
                    c.line = line;
                    c.denyadmin = par[0] > 0;
                    adminpwds.add(c);
                    logline(ACLOG_VERBOSE,"line%4d: %s %d", c.line, hiddenpwd(c.pwd), c.denyadmin ? 1 : 0);
                }
            }
            line++;
        }
        DELETEA(buf);
        logline(ACLOG_INFO,"read %d admin passwords from '%s'", adminpwds.length() - staticpasses, filename);
    }

    bool check(const char *name, const char *pwd, int salt, pwddetail *detail = NULL, enet_uint32 address = 0)
    {
        bool found = false;
        if (address && passguy == address)
        {
            if (passtime + 3000 > servmillis || ( passtries > 5 && passtime + 10000 > servmillis ))
            {
                passtries++;
                passtime = servmillis;
                return false;
            }
            else
            {
                if ( passtime + 60000 < servmillis ) passtries = 0;
            }
            passtries++;
        }
        else
        {
            passtries = 0;
        }
        passguy = address;
        passtime = servmillis;
        loopv(adminpwds)
        {
            if(!strcmp(genpwdhash(name, adminpwds[i].pwd, salt), pwd))
            {
                if(detail) *detail = adminpwds[i];
                found = true;
                break;
            }
        }
        return found;
    }
};

// serverinfo_en.txt, motd_en.txt

#define MAXINFOLINELEN 100  // including color codes

struct serverinfofile
{
    struct serverinfotext { const char *type; char lang[3]; char *info; int lastcheck; };
    vector<serverinfotext> serverinfotexts;
    const char *infobase, *motdbase;

    void init(const char *info, const char *motd) { infobase = info; motdbase = motd; }

    char *readinfofile(const char *fnbase, const char *lang)
    {
        defformatstring(fname)("%s_%s.txt", fnbase, lang);
        path(fname);
        int len, n;
        char *c, *s, *t, *buf = loadfile(fname, &len);
        if(!buf) return NULL;
        char *nbuf = new char[len + 2];
        for(t = nbuf, s = strtok(buf, "\n\r"); s; s = strtok(NULL, "\n\r"))
        {
            c = strstr(s, "//");
            if(c) *c = '\0'; // strip comments
            for(n = (int)strlen(s) - 1; n >= 0 && s[n] == ' '; n--) s[n] = '\0'; // strip trailing blanks
            filterrichtext(t, s + strspn(s, " "), MAXINFOLINELEN); // skip leading blanks
            n = (int)strlen(t);
            if(n) t += n + 1;
        }
        *t = '\0';
        DELETEA(buf);
        if(!*nbuf) DELETEA(nbuf);
        logline(ACLOG_DEBUG,"read file \"%s\"", fname);
        return nbuf;
    }

    const char *getinfocache(const char *fnbase, const char *lang)
    {
        serverinfotext sn = { fnbase, { 0, 0, 0 }, NULL, 0} , *s = &sn;
        filterlang(sn.lang, lang);
        if(!sn.lang[0]) return NULL;
        loopv(serverinfotexts)
        {
            serverinfotext &si = serverinfotexts[i];
            if(si.type == s->type && !strcmp(si.lang, s->lang))
            {
                if(servmillis - si.lastcheck > (si.info ? 15 : 45) * 60 * 1000)
                { // re-read existing files after 15 minutes; search missing again after 45 minutes
                    DELETEA(si.info);
                    s = &si;
                }
                else return si.info;
            }
        }
        s->info = readinfofile(fnbase, lang);
        s->lastcheck = servmillis;
        if(s == &sn) serverinfotexts.add(*s);
        if(fnbase == motdbase && s->info)
        {
            char *c = s->info;
            while(*c) { c += strlen(c); if(c[1]) *c++ = '\n'; }
            if(strlen(s->info) > MAXSTRLEN) s->info[MAXSTRLEN] = '\0'; // keep MOTD at sane lengths
        }
        return s->info;
    }

    const char *getinfo(const char *lang)
    {
        return getinfocache(infobase, lang);
    }

    const char *getmotd(const char *lang)
    {
        const char *motd;
        if(*lang && (motd = getinfocache(motdbase, lang))) return motd;
        return getinfocache(motdbase, "en");
    }
};

struct killmessagesfile : serverconfigfile
{
    void init(const char *name) { serverconfigfile::init(name); }
    void read()
    {
        if(getfilesize(filename) == filelen) return;
        if(!load()) return;

        char *l, *s, *p = buf;
        const char *sep = " \"";
        int line = 0;
        logline(ACLOG_VERBOSE,"reading kill messages file '%s'", filename);
        while(p < buf + filelen)
        {
            l = p; p += strlen(p) + 1;
            l = strtok(l, sep);

            char *message;
            if(l)
            {
                s = strtok(NULL, sep);
                bool fragmsg = !strcmp(l, "fragmessage");
                bool gibmsg = !strcmp(l, "gibmessage");
                if(s && (fragmsg || gibmsg))
                {
                    int errors = 0;
                    int gun = atoi(s);

                    s += strlen(s) + 1;
                    while(s[0] == ' ') s++;
                    int hasquotes = strspn(s, "\"");
                    s += hasquotes;
                    message = s;
                    const char *seps = "\" \n", *end = NULL;
                    char cursep;
                    while( (cursep = *seps++) != '\0')
                    {
                        if(cursep == '"' && !hasquotes) continue;
                        end = strchr(message, cursep);
                        if(end) break;
                    }
                    if(end) message[end-message] = '\0';

                    if(gun < 0 || gun >= NUMGUNS)
                    {
                        logline(ACLOG_INFO, " error in line %i, invalid gun : %i", line, gun);
                        errors++;
                    }
                    if(strlen(message)>MAXKILLMSGLEN)
                    {
                        logline(ACLOG_INFO, " error in line %i, too long message : string length is %i, max. allowed is %i", line, strlen(message), MAXKILLMSGLEN);
                        errors++;
                    }
                    if(!errors)
                    {
                        if(fragmsg)
                        {
                            copystring(killmessages[0][gun], message, MAXKILLMSGLEN);
                            logline(ACLOG_VERBOSE, " added msg '%s' for frags with weapon %i ", message, gun);
                        }
                        else
                        {
                            copystring(killmessages[1][gun], message, MAXKILLMSGLEN);
                            logline(ACLOG_VERBOSE, " added msg '%s' for gibs with weapon %i ", message, gun);
                        }
                    }
                    s = NULL;
                    line++;
                }
            }
        }
    }
};
// 2011feb05:ft: quitproc
#include <signal.h>
// config
static servercontroller *svcctrl = NULL;
servercommandline scl;
static servermaprot maprot;
static serveripblacklist ipblacklist;
static servernickblacklist nickblacklist;
static serverforbiddenlist forbiddenlist;
static serverpasswords passwords;
static serverinfofile infofiles;
static killmessagesfile killmsgs;

// server state
static ENetHost *serverhost = NULL;

int laststatus = 0, servmillis = 0, lastfillup = 0;

static vector<client *> clients;
static vector<worldstate *> worldstates;
static vector<savedscore> savedscores;
static vector<ban> bans;
static vector<demofile> demofiles;

static int mastermode = MM_OPEN;
static bool autoteam = true;
static int matchteamsize = 0;

static long int incoming_size = 0;

static bool forceintermission = false;

string servdesc_current;
static ENetAddress servdesc_caller;
static bool custom_servdesc = false;

// current game
static string nextmapname;
int interm = 0;
static int minremain = 0, gamemillis = 0, gamelimit = 0, /*lmsitemtype = 0,*/ nextsendscore = 0;
static vector<server_entity> sents;

int maplayout_factor, testlayout_factor, maplayoutssize;
static servermapbuffer mapbuffer;

// cmod
char *global_name;
int totalclients = 0;
static int cn2boot;
static int servertime = 0, serverlagged = 0;

bool valid_client(int cn)
{
    return clients.inrange(cn) && clients[cn]->type != ST_EMPTY;
}

static void cleanworldstate(ENetPacket *packet)
{
   loopv(worldstates)
   {
       worldstate *ws = worldstates[i];
       if(ws->positions.inbuf(packet->data) || ws->messages.inbuf(packet->data)) ws->uses--;
       else continue;
       if(!ws->uses)
       {
           delete ws;
           worldstates.remove(i);
       }
       break;
   }
}

static void sendpacket(int n, int chan, ENetPacket *packet, int exclude, bool demopacket)
{
    if(n<0)
    {
        recordpacket(chan, packet->data, (int)packet->dataLength);
        loopv(clients) if(i!=exclude && (clients[i]->type!=ST_TCPIP || clients[i]->isauthed)) sendpacket(i, chan, packet, -1, demopacket);
        return;
    }
    switch(clients[n]->type)
    {
        case ST_TCPIP:
        {
            enet_peer_send(clients[n]->peer, chan, packet);
            break;
        }

        case ST_LOCAL:
            localservertoclient(chan, packet->data, (int)packet->dataLength, demopacket);
            break;
    }
}

static bool reliablemessages = false;

static bool buildworldstate()
{
    static struct { int posoff, poslen, msgoff, msglen; } pkt[MAXCLIENTS];
    worldstate &ws = *new worldstate;
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP || !c.isauthed) continue;
        c.overflow = 0;
        if(c.position.empty()) pkt[i].posoff = -1;
        else
        {
            pkt[i].posoff = ws.positions.length();
            ws.positions.put(c.position.getbuf(), c.position.length());
            pkt[i].poslen = ws.positions.length() - pkt[i].posoff;
            c.position.setsize(0);
        }
        if(c.messages.empty()) pkt[i].msgoff = -1;
        else
        {
            pkt[i].msgoff = ws.messages.length();
            putint(ws.messages, SV_CLIENT);
            putint(ws.messages, c.clientnum);
            putuint(ws.messages, c.messages.length());
            ws.messages.put(c.messages.getbuf(), c.messages.length());
            pkt[i].msglen = ws.messages.length() - pkt[i].msgoff;
            c.messages.setsize(0);
        }
    }
    int psize = ws.positions.length(), msize = ws.messages.length();
    if(psize)
    {
        recordpacket(0, ws.positions.getbuf(), psize);
        ucharbuf p = ws.positions.reserve(psize);
        p.put(ws.positions.getbuf(), psize);
        ws.positions.addbuf(p);
    }
    if(msize)
    {
        recordpacket(1, ws.messages.getbuf(), msize);
        ucharbuf p = ws.messages.reserve(msize);
        p.put(ws.messages.getbuf(), msize);
        ws.messages.addbuf(p);
    }
    ws.uses = 0;
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP || !c.isauthed) continue;
        ENetPacket *packet;
        if(psize && (pkt[i].posoff<0 || psize-pkt[i].poslen>0))
        {
            packet = enet_packet_create(&ws.positions[pkt[i].posoff<0 ? 0 : pkt[i].posoff+pkt[i].poslen],
                                        pkt[i].posoff<0 ? psize : psize-pkt[i].poslen,
                                        ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(c.clientnum, 0, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }

        if(msize && (pkt[i].msgoff<0 || msize-pkt[i].msglen>0))
        {
            packet = enet_packet_create(&ws.messages[pkt[i].msgoff<0 ? 0 : pkt[i].msgoff+pkt[i].msglen],
                                        pkt[i].msgoff<0 ? msize : msize-pkt[i].msglen,
                                        (reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(c.clientnum, 1, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }
    }
    reliablemessages = false;
    if(!ws.uses)
    {
        delete &ws;
        return false;
    }
    else
    {
        worldstates.add(&ws);
        return true;
    }
}

static int countclients(int type, bool exclude = false)
{
    int num = 0;
    loopv(clients) if((clients[i]->type!=type)==exclude) num++;
    return num;
}

static int numclients() { return countclients(ST_EMPTY, true); }
static int numlocalclients() { return countclients(ST_LOCAL); }
static int numnonlocalclients() { return countclients(ST_TCPIP); }

static int numauthedclients()
{
    int num = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed) num++;
    return num;
}

static int numactiveclients()
{
    int num = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed && clients[i]->isonrightmap && team_isactive(clients[i]->team)) num++;
    return num;
}

static int *numteamclients(int exclude = -1)
{
    static int num[TEAM_NUM];
    loopi(TEAM_NUM) num[i] = 0;
    loopv(clients) if(i != exclude && clients[i]->type!=ST_EMPTY && clients[i]->isauthed && clients[i]->isonrightmap && team_isvalid(clients[i]->team)) num[clients[i]->team]++;
    return num;
}

static int sendservermode(bool send = true)
{
    int sm = (autoteam ? AT_ENABLED : AT_DISABLED) | ((mastermode & MM_MASK) << 2) | (matchteamsize << 4);
    if(send) sendf(-1, 1, "ri2", SV_SERVERMODE, sm);
    return sm;
}

static void changematchteamsize(int newteamsize)
{
    if(newteamsize < 0) return;
    if(matchteamsize != newteamsize)
    {
        matchteamsize = newteamsize;
        sendservermode();
    }
    if(mastermode == MM_MATCH && matchteamsize && m_teammode)
    {
        int size[2] = { 0 };
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed && clients[i]->isonrightmap)
        {
            if(team_isactive(clients[i]->team))
            {
                if(++size[clients[i]->team] > matchteamsize) updateclientteam(i, team_tospec(clients[i]->team), FTR_SILENTFORCE);
            }
        }
    }
}

static void changemastermode(int newmode)
{
    if(mastermode != newmode)
    {
        mastermode = newmode;
        senddisconnectedscores(-1);
        if(mastermode != MM_MATCH)
        {
            loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed)
            {
                if(clients[i]->team == TEAM_CLA_SPECT || clients[i]->team == TEAM_RVSF_SPECT) updateclientteam(i, TEAM_SPECT, FTR_SILENTFORCE);
            }
        }
        else if(matchteamsize) changematchteamsize(matchteamsize);
    sendservermode();
    }
}

static int findcnbyaddress(ENetAddress *address)
{
    loopv(clients)
    {
        if(clients[i]->type == ST_TCPIP && clients[i]->peer->address.host == address->host && clients[i]->peer->address.port == address->port)
            return i;
    }
    return -1;
}

static savedscore *findscore(client &c, bool insert)
{
    if(c.type!=ST_TCPIP) return NULL;
    enet_uint32 mask = ENET_HOST_TO_NET_32(mastermode == MM_MATCH ? 0xFFFF0000 : 0xFFFFFFFF); // in match mode, reconnecting from /16 subnet is allowed
    if(!insert)
    {
        loopv(clients)
        {
            client &o = *clients[i];
            if(o.type!=ST_TCPIP || !o.isauthed) continue;
            if(o.clientnum!=c.clientnum && o.peer->address.host==c.peer->address.host && !strcmp(o.name, c.name))
            {
                static savedscore curscore;
                curscore.save(o.state, o.team);
                return &curscore;
            }
        }
    }
    loopv(savedscores)
    {
        savedscore &sc = savedscores[i];
        if(!strcmp(sc.name, c.name) && (sc.ip & mask) == (c.peer->address.host & mask)) return &sc;
    }
    if(!insert) return NULL;
    savedscore &sc = savedscores.add();
    copystring(sc.name, c.name);
    sc.ip = c.peer->address.host;
    return &sc;
}

void restoreserverstate(vector<entity> &ents)   // hack: called from savegame code, only works in SP
{
    loopv(sents)
    {
        sents[i].spawned = ents[i].spawned;
        sents[i].spawntime = 0;
    }
}

static void sendf(int cn, int chan, const char *format, ...)
{
    int exclude = -1;
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    packetbuf p(MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'x':
            exclude = va_arg(args, int);
            break;

        case 'v':
        {
            int n = va_arg(args, int);
            int *v = va_arg(args, int *);
            loopi(n) putint(p, v[i]);
            break;
        }

        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'm':
        {
            int n = va_arg(args, int);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);
    sendpacket(cn, chan, p.finalize(), exclude);
}

static void sendextras()
{
    if ( gamemillis < nextsendscore ) return;
    int count = 0, list[MAXCLIENTS];
    loopv(clients)
    {
        client &c = *clients[i];
        if ( c.type!=ST_TCPIP || !c.isauthed || !(c.md.updated && c.md.upmillis < gamemillis) ) continue;
        if ( c.md.combosend )
        {
            sendf(c.clientnum, 1, "ri2", SV_HUDEXTRAS, min(c.md.combo,c.md.combofrags)-1 + HE_COMBO);
            c.md.combosend = false;
        }
        if ( c.md.dpt )
        {
            list[count] = i;
            count++;
        }
    }
    nextsendscore = gamemillis + 160; // about 4 cicles
    if ( !count ) return;

    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_POINTS);
    putint(p,count);
    int *v = list;
    loopi(count)
    {
        client &c = *clients[*v];
        putint(p,c.clientnum); putint(p,c.md.dpt); c.md.updated = false; c.md.upmillis = c.md.dpt = 0;
        v++;
    }

    sendpacket(-1, 1, p.finalize());
}

static void sendservmsg(const char *msg, int cn = -1)
{
    sendf(cn, 1, "ris", SV_SERVMSG, msg);
}

static void sendspawn(client *c)
{
    if(team_isspect(c->team)) return;
    clientstate &gs = c->state;
    gs.respawn();
    gs.spawnstate(smode);
    gs.lifesequence++;
    sendf(c->clientnum, 1, "ri7vv", SV_SPAWNSTATE, gs.lifesequence,
        gs.health, gs.armour,
        gs.primary, gs.gunselect, m_arena ? c->spawnindex : -1,
        NUMGUNS, gs.ammo, NUMGUNS, gs.mag);
    gs.lastspawn = gamemillis;
}

// demo
static stream *demotmp = NULL, *demorecord = NULL, *demoplayback = NULL;
static bool recordpackets = false;
static int nextplayback = 0;

static void writedemo(int chan, void *data, int len)
{
    if(!demorecord) return;
    int stamp[3] = { gamemillis, chan, len };
    lilswap(stamp, 3);
    demorecord->write(stamp, sizeof(stamp));
    demorecord->write(data, len);
}

static void recordpacket(int chan, void *data, int len)
{
    if(recordpackets) writedemo(chan, data, len);
}

void recordpacket(int chan, ENetPacket *packet)
{
    if(recordpackets) writedemo(chan, packet->data, (int)packet->dataLength);
}

#ifdef STANDALONE
const char *currentserver(int i)
{
    static string curSRVinfo;
    string r;
    r[0] = '\0';
    switch(i)
    {
        case 1: { copystring(r, scl.ip[0] ? scl.ip : "local"); break; } // IP
        case 2: { copystring(r, scl.logident[0] ? scl.logident : "local"); break; } // HOST
        case 3: { formatstring(r)("%d", scl.serverport); break; } // PORT
        // the following are used by a client, a server will simply return empty strings for them
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        {
            break;
        }
        default:
        {
            formatstring(r)("%s %d", scl.ip[0] ? scl.ip : "local", scl.serverport);
            break;
        }
    }
    copystring(curSRVinfo, r);
    return curSRVinfo;
}
#endif

// these are actually the values used by the client, the server ones are in "scl".
string demofilenameformat = DEFDEMOFILEFMT;
string demotimestampformat = DEFDEMOTIMEFMT;
int demotimelocal = 0;

#ifdef STANDALONE
#define DEMOFORMAT scl.demofilenameformat
#define DEMOTSFORMAT scl.demotimestampformat
#else
#define DEMOFORMAT demofilenameformat
#define DEMOTSFORMAT demotimestampformat
#endif

const char *getDemoFilename(int gmode, int mplay, int mdrop, int tstamp, char *srvmap)
{
    // we use the following internal mapping of formatchars:
    // %g : gamemode (int)      %G : gamemode (chr)             %F : gamemode (full)
    // %m : minutes remaining   %M : minutes played
    // %s : seconds remaining   %S : seconds played
    // %h : IP of server        %H : hostname of server
    // %n : mapName
    // %w : timestamp "when"
    static string dmofn;
    copystring(dmofn, "");

    int cc = 0;
    int mc = strlen(DEMOFORMAT);

    while(cc<mc)
    {
        switch(DEMOFORMAT[cc])
        {
            case '%':
            {
                if(cc<(mc-1))
                {
                    string cfspp;
                    switch(DEMOFORMAT[cc+1])
                    {
                        case 'F': formatstring(cfspp)("%s", fullmodestr(gmode)); break;
                        case 'g': formatstring(cfspp)("%d", gmode); break;
                        case 'G': formatstring(cfspp)("%s", acronymmodestr(gmode)); break;
                        case 'h': formatstring(cfspp)("%s", currentserver(1)); break; // client/server have different implementations
                        case 'H': formatstring(cfspp)("%s", currentserver(2)); break; // client/server have different implementations
                        case 'm': formatstring(cfspp)("%d", mdrop/60); break;
                        case 'M': formatstring(cfspp)("%d", mplay/60); break;
                        case 'n': formatstring(cfspp)("%s", srvmap); break;
                        case 's': formatstring(cfspp)("%d", mdrop); break;
                        case 'S': formatstring(cfspp)("%d", mplay); break;
                        case 'w':
                        {
                            time_t t = tstamp;
                            struct tm * timeinfo;
                            timeinfo = demotimelocal ? localtime(&t) : gmtime (&t);
                            strftime(cfspp, sizeof(string) - 1, DEMOTSFORMAT, timeinfo);
                            break;
                        }
                        default: logline(ACLOG_INFO, "bad formatstring: demonameformat @ %d", cc); cc-=1; break; // don't drop the bad char
                    }
                    concatstring(dmofn, cfspp);
                }
                else
                {
                    logline(ACLOG_INFO, "trailing %%-sign in demonameformat");
                }
                cc+=1;
                break;
            }
            default:
            {
                defformatstring(fsbuf)("%s%c", dmofn, DEMOFORMAT[cc]);
                copystring(dmofn, fsbuf);
                break;
            }
        }
        cc+=1;
    }
    return dmofn;
}
#undef DEMOFORMAT
#undef DEMOTSFORMAT

static void enddemorecord()
{
    if(!demorecord) return;

    delete demorecord;
    recordpackets = false;
    demorecord = NULL;

    if(!demotmp) return;

    if(gamemillis < DEMO_MINTIME)
    {
        delete demotmp;
        demotmp = NULL;
        logline(ACLOG_INFO, "Demo discarded.");
        return;
    }

    int len = demotmp->size();
    demotmp->seek(0, SEEK_SET);
    if(demofiles.length() >= scl.maxdemos)
    {
        delete[] demofiles[0].data;
        demofiles.remove(0);
    }
    int mr = gamemillis >= gamelimit ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
    demofile &d = demofiles.add();

    //2010oct10:ft: suggests : formatstring(d.info)("%s, %s, %.2f%s", modestr(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB"); // the datetime bit is pretty useless in the servmesg, no?!
    formatstring(d.info)("%s: %s, %s, %.2f%s", asctime(), modestr(gamemode), smapname, len > 1024*1024 ? len/(1024*1024.f) : len/1024.0f, len > 1024*1024 ? "MB" : "kB");
    if(mr) { concatformatstring(d.info, ", %d mr", mr); concatformatstring(d.file, "_%dmr", mr); }
    defformatstring(msg)("Demo \"%s\" recorded\nPress F10 to download it from the server..", d.info);
    sendservmsg(msg);
    logline(ACLOG_INFO, "Demo \"%s\" recorded.", d.info);

    // 2011feb05:ft: previously these two static formatstrings were used ..
    //formatstring(d.file)("%s_%s_%s", timestring(), behindpath(smapname), modestr(gamemode, true)); // 20100522_10.08.48_ac_mines_DM.dmo
    //formatstring(d.file)("%s_%s_%s", modestr(gamemode, true), behindpath(smapname), timestring( true, "%Y.%m.%d_%H%M")); // DM_ac_mines.2010.05.22_1008.dmo
    // .. now we use client-side parseable fileattribs
    int mPLAY = gamemillis >= gamelimit ? gamelimit/1000 : gamemillis/1000;
    int mDROP = gamemillis >= gamelimit ? 0 : (gamelimit - gamemillis)/1000;
    int iTIME = time(NULL);
    const char *mTIME = numtime();
    const char *sMAPN = behindpath(smapname);
    string iMAPN;
    copystring(iMAPN, sMAPN);
    formatstring(d.file)( "%d:%d:%d:%s:%s", gamemode, mPLAY, mDROP, mTIME, iMAPN);

    d.data = new uchar[len];
    d.len = len;
    demotmp->read(d.data, len);
    delete demotmp;
    demotmp = NULL;
    if(scl.demopath[0])
    {
        formatstring(msg)("%s%s.dmo", scl.demopath, getDemoFilename(gamemode, mPLAY, mDROP, iTIME, iMAPN)); //d.file);
        path(msg);
        stream *demo = openfile(msg, "wb");
        if(demo)
        {
            int wlen = (int) demo->write(d.data, d.len);
            delete demo;
            logline(ACLOG_INFO, "demo written to file \"%s\" (%d bytes)", msg, wlen);
        }
        else
        {
            logline(ACLOG_INFO, "failed to write demo to file \"%s\"", msg);
        }
    }
}

static void setupdemorecord()
{
    if(numlocalclients() || !m_mp(gamemode) || gamemode == GMODE_COOPEDIT) return;

    defformatstring(demotmppath)("demos/demorecord_%s_%d", scl.ip[0] ? scl.ip : "local", scl.serverport);
    demotmp = opentempfile(demotmppath, "w+b");
    if(!demotmp) return;

    stream *f = opengzfile(NULL, "wb", demotmp);
    if(!f)
    {
        delete demotmp;
        demotmp = NULL;
        return;
    }

    sendservmsg("recording demo");
    logline(ACLOG_INFO, "Demo recording started.");

    demorecord = f;
    recordpackets = false;

    demoheader hdr;
    memcpy(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic));
    hdr.version = DEMO_VERSION;
    hdr.protocol = SERVER_PROTOCOL_VERSION;
    lilswap(&hdr.version, 1);
    lilswap(&hdr.protocol, 1);
    memset(hdr.desc, 0, DHDR_DESCCHARS);
    defformatstring(desc)("%s, %s, %s %s", modestr(gamemode, false), behindpath(smapname), asctime(), servdesc_current);
    if(strlen(desc) > DHDR_DESCCHARS)
        formatstring(desc)("%s, %s, %s %s", modestr(gamemode, true), behindpath(smapname), asctime(), servdesc_current);
    desc[DHDR_DESCCHARS - 1] = '\0';
    strcpy(hdr.desc, desc);
    memset(hdr.plist, 0, DHDR_PLISTCHARS);
    const char *bl = "";
    loopv(clients)
    {
        client *ci = clients[i];
        if(ci->type==ST_EMPTY) continue;
        if(strlen(hdr.plist) + strlen(ci->name) < DHDR_PLISTCHARS - 2) { strcat(hdr.plist, bl); strcat(hdr.plist, ci->name); }
        bl = " ";
    }
    demorecord->write(&hdr, sizeof(demoheader));

    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    welcomepacket(p, -1);
    writedemo(1, p.buf, p.len);
}

static void listdemos(int cn)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_SENDDEMOLIST);
    putint(p, demofiles.length());
    loopv(demofiles) sendstring(demofiles[i].info, p);
    sendpacket(cn, 1, p.finalize());
}

static void cleardemos(int n)
{
    if(!n)
    {
        loopv(demofiles) delete[] demofiles[i].data;
        demofiles.shrink(0);
        sendservmsg("cleared all demos");
    }
    else if(demofiles.inrange(n-1))
    {
        delete[] demofiles[n-1].data;
        demofiles.remove(n-1);
        defformatstring(msg)("cleared demo %d", n);
        sendservmsg(msg);
    }
}

static bool sending_demo = false;

static void senddemo(int cn, int num)
{
    client *cl = cn>=0 ? clients[cn] : NULL;
    bool is_admin = (cl && cl->role == CR_ADMIN);
    if(scl.demo_interm && (!interm || totalclients > 2) && !is_admin)
    {
        sendservmsg("\f3sorry, but this server only sends demos at intermission.\n wait for the end of this game, please", cn);
        return;
    }
    if(!num) num = demofiles.length();
    if(!demofiles.inrange(num-1))
    {
        if(demofiles.empty()) sendservmsg("no demos available", cn);
        else
        {
            defformatstring(msg)("no demo %d available", num);
            sendservmsg(msg, cn);
        }
        return;
    }
    demofile &d = demofiles[num-1];
    loopv(d.clientssent) if(d.clientssent[i].ip == cl->peer->address.host && d.clientssent[i].clientnum == cl->clientnum)
    {
        sendservmsg("\f3Sorry, you have already downloaded this demo.", cl->clientnum);
        return;
    }
    clientidentity &ci = d.clientssent.add();
    ci.ip = cl->peer->address.host;
    ci.clientnum = cl->clientnum;

    if (interm) sending_demo = true;
    packetbuf p(MAXTRANS + d.len, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_SENDDEMO);
    sendstring(d.file, p);
    putint(p, d.len);
    p.put(d.data, d.len);
    sendpacket(cn, 2, p.finalize());
}

int demoprotocol;
bool watchingdemo = false;

void enddemoplayback()
{
    if(!demoplayback) return;
    delete demoplayback;
    demoplayback = NULL;
    watchingdemo = false;

    loopv(clients) sendf(i, 1, "risi", SV_DEMOPLAYBACK, "", i);

    sendservmsg("demo playback finished");

    loopv(clients) sendwelcome(clients[i]);
}

static void setupdemoplayback()
{
    demoheader hdr;
    string msg;
    msg[0] = '\0';
    defformatstring(file)("demos/%s.dmo", smapname);
    path(file);
    demoplayback = opengzfile(file, "rb");
    if(!demoplayback) formatstring(msg)("could not read demo \"%s\"", file);
    else if(demoplayback->read(&hdr, sizeof(demoheader))!=sizeof(demoheader) || memcmp(hdr.magic, DEMO_MAGIC, sizeof(hdr.magic)))
        formatstring(msg)("\"%s\" is not a demo file", file);
    else
    {
        lilswap(&hdr.version, 1);
        lilswap(&hdr.protocol, 1);
        if(hdr.version!=DEMO_VERSION) formatstring(msg)("demo \"%s\" requires an %s version of AssaultCube", file, hdr.version<DEMO_VERSION ? "older" : "newer");
        else if(hdr.protocol != PROTOCOL_VERSION && !(hdr.protocol < 0 && hdr.protocol == -PROTOCOL_VERSION) && hdr.protocol != 1132) formatstring(msg)("demo \"%s\" requires an %s version of AssaultCube", file, hdr.protocol<PROTOCOL_VERSION ? "older" : "newer");
        else if(hdr.protocol == 1132) sendservmsg("WARNING: using experimental compatibility mode for older demo protocol, expect breakage");
        demoprotocol = hdr.protocol;
    }
    if(msg[0])
    {
        if(demoplayback) { delete demoplayback; demoplayback = NULL; }
        sendservmsg(msg);
        return;
    }

    formatstring(msg)("playing demo \"%s\"", file);
    sendservmsg(msg);
    sendf(-1, 1, "risi", SV_DEMOPLAYBACK, smapname, -1);
    watchingdemo = true;

    if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
    {
        enddemoplayback();
        return;
    }
    lilswap(&nextplayback, 1);
}

static void readdemo()
{
    if(!demoplayback) return;
    while(gamemillis>=nextplayback)
    {
        int chan, len;
        if(demoplayback->read(&chan, sizeof(chan))!=sizeof(chan) ||
           demoplayback->read(&len, sizeof(len))!=sizeof(len))
        {
            enddemoplayback();
            return;
        }
        lilswap(&chan, 1);
        lilswap(&len, 1);
        ENetPacket *packet = enet_packet_create(NULL, len, 0);
        if(!packet || demoplayback->read(packet->data, len)!=len)
        {
            if(packet) enet_packet_destroy(packet);
            enddemoplayback();
            return;
        }
        sendpacket(-1, chan, packet, -1, true);
        if(!packet->referenceCount) enet_packet_destroy(packet);
        if(demoplayback->read(&nextplayback, sizeof(nextplayback))!=sizeof(nextplayback))
        {
            enddemoplayback();
            return;
        }
        lilswap(&nextplayback, 1);
    }
}

struct sflaginfo
{
    int state;
    int actor_cn;
    float pos[3];
    int lastupdate;
    int stolentime;
    short x, y;          // flag entity location

    sflaginfo() { actor_cn = -1; }
} sflaginfos[2];

static void putflaginfo(packetbuf &p, int flag)
{
    sflaginfo &f = sflaginfos[flag];
    putint(p, SV_FLAGINFO);
    putint(p, flag);
    putint(p, f.state);
    switch(f.state)
    {
        case CTFF_STOLEN:
            putint(p, f.actor_cn);
            break;
        case CTFF_DROPPED:
            loopi(3) putuint(p, (int)(f.pos[i]*DMF));
            break;
    }
}

static inline void send_item_list(packetbuf &p)
{
    putint(p, SV_ITEMLIST);
    loopv(sents) if(sents[i].spawned) putint(p, i);
    putint(p, -1);
    if(m_flags) loopi(2) putflaginfo(p, i);
}





static inline bool is_lagging(client *cl)
{
    return ( cl->spj > 50 || cl->ping > 500 || cl->ldt > 80 ); // do not change this except if you really know what are you doing
}

static inline bool outside_border(vec &po)
{
    return (po.x < 0 || po.y < 0 || po.x >= maplayoutssize || po.y >= maplayoutssize);
}

static inline void checkclientpos(client *cl)
{
    vec &po = cl->state.o;
    if( outside_border(po) || maplayout[((int) po.x) + (((int) po.y) << maplayout_factor)] > po.z + 3)
    {
        if(gamemillis > 10000 && (servmillis - cl->connectmillis) > 10000) cl->mapcollisions++;
        if(cl->mapcollisions && !(cl->mapcollisions % 25))
        {
            logline(ACLOG_INFO, "[%s] %s is colliding with the map", cl->hostname, cl->name);
        }
    }
}

#define POW2XY(A,B) (pow2(A.x-B.x)+pow2(A.y-B.y))

static inline void addban(client *cl, int reason, int type = BAN_AUTO)
{
    if(!cl) return;
    ban b = { cl->peer->address, servmillis+scl.ban_time, type };
    bans.add(b);
    disconnect_client(cl->clientnum, reason);
}

#ifdef ACAC
#include "anticheat.h"
#endif

#define MINELINE 50

//FIXME
/* There are smarter ways to implement this function, but most probably they will be very complex */
static int getmaxarea(int inversed_x, int inversed_y, int transposed, int ml_factor, char *ml)
{
    int ls = (1 << ml_factor);
    int xi = 0, oxi = 0, xf = 0, oxf = 0, fx = 0, fy = 0;
    int area = 0, maxarea = 0;
    bool sav_x = false, sav_y = false;

    if (transposed) fx = ml_factor;
    else fy = ml_factor;

    // walk on x for each y
    for ( int y = (inversed_y ? ls-1 : 0); (inversed_y ? y >= 0 : y < ls); (inversed_y ? y-- : y++) ) {

    /* Analyzing each cube of the line */
        for ( int x = (inversed_x ? ls-1 : 0); (inversed_x ? x >= 0 : x < ls); (inversed_x ? x-- : x++) ) {
            if ( ml[ ( x << fx ) + ( y << fy ) ] != 127 ) {      // if it is not solid
                if ( sav_x ) {                                          // if the last cube was saved
                    xf = x;                                             // new end for this line
                }
                else {
                    xi = x;                                             // new begin of the line
                    sav_x = true;                                       // accumulating cubes from now
                }
            } else {                                    // solid
                if ( xf - xi > MINELINE ) break;                        // if the empty line is greater than a minimum, get out
                sav_x = false;                                          // stop the accumulation of cubes
            }
        }

    /* Analyzing this line with the previous one */
        if ( xf - xi > MINELINE ) {                                     // if the line has the minimun threshold of emptiness
            if ( sav_y ) {                                              // if the last line was saved
                if ( 2*oxi + MINELINE < 2*xf &&
                     2*xi + MINELINE < 2*oxf ) {                        // if the last line intersect this one
                    area += xf - xi;
                } else {
                    oxi = xi;                                           // new area vertices
                    oxf = xf;
                }
            }
            else {
                oxi = xi;
                oxf = xf;
                sav_y = true;                                           // accumulating lines from now
            }
        } else {
            sav_y = false;                                              // stop the accumulation of lines
            if (area > maxarea) maxarea = area;                         // new max area
            area=0;
        }

        sav_x = false;                                                  // reset x
        xi = xf = 0;
    }
    return maxarea;
}

int checkarea(int maplayout_factor, char *maplayout)
{
    int area = 0, maxarea = 0;
    for (int i=0; i < 8; i++) {
        area = getmaxarea((i & 1),(i & 2),(i & 4), maplayout_factor, maplayout);
        if ( area > maxarea ) maxarea = area;
    }
    return maxarea;
}

/**
This part is related to medals system. WIP
 */

static const char * medal_messages[] = { "defended the flag", "covered the flag stealer", "defended the dropped flag", "covered the flag keeper", "covered teammate" };
enum { CTFLDEF, CTFLCOV, HTFLDEF, HTFLCOV, COVER, MEDALMESSAGENUM };
static inline void print_medal_messages(client *c, int n)
{
    if (n<0 || n>=MEDALMESSAGENUM) return;
    logline(ACLOG_VERBOSE, "[%s] %s %s", c->hostname, c->name, medal_messages[n]);
}

static inline void addpt(client *c, int points, int n = -1) {
    c->state.points += points;
    c->md.dpt += points;
    c->md.updated = true;
    c->md.upmillis = gamemillis + 240; // about 2 AR shots
    print_medal_messages(c,n);
}

/** cnumber is the number of players in the game, at a max value of 12 */
#define CTFPICKPT     cnumber                      // player picked the flag (ctf)
#define CTFDROPPT    -cnumber                      // player dropped the flag to other player (probably)
#define HTFLOSTPT  -2*cnumber                      // penalty
#define CTFLOSTPT     cnumber*distance/100         // bonus: 1/4 of the flag score bonus
#define CTFRETURNPT   cnumber                      // flag return
#define CTFSCOREPT   (cnumber*distance/25+10)      // flag score
#define HTFSCOREPT   (cnumber*4+10)
#define KTFSCOREPT   (cnumber*2+10)
#define COMBOPT       5                            // player frags with combo
#define REPLYPT       2                            // reply success
#define TWDONEPT      5                            // team work done
#define CTFLDEFPT     cnumber                      // player defended the flag in the base (ctf)
#define CTFLCOVPT     cnumber*2                    // player covered the flag stealer (ctf)
#define HTFLDEFPT     cnumber                      // player defended a droped flag (htf)
#define HTFLCOVPT     cnumber*3                    // player covered the flag keeper (htf)
#define COVERPT       cnumber*2                    // player covered teammate
#define DEATHPT      -4                            // player died
#define BONUSPT       target->state.points/400     // bonus (for killing high level enemies :: beware with exponential behavior!)
#define FLBONUSPT     target->state.points/300     // bonus if flag team mode
#define TMBONUSPT     target->state.points/200     // bonus if team mode (to give some extra reward for playing tdm modes)
#define HTFFRAGPT     cnumber/2                    // player frags while carrying the flag
#define CTFFRAGPT   2*cnumber                      // player frags the flag stealer
#define FRAGPT        10                           // player frags (normal)
#define HEADSHOTPT    15                           // player gibs with head shot
#define KNIFEPT       20                           // player gibs with the knife
#define SHOTGPT       12                           // player gibs with the shotgun
#define TKPT         -20                           // player tks
#define FLAGTKPT     -2*(10+cnumber)               // player tks the flag keeper/stealer

static void flagpoints(client *c, int message)
{
    float distance = 0;
    int cnumber = totalclients < 13 ? totalclients : 12;
    switch (message)
    {
        case FM_PICKUP:
            c->md.flagpos.x = c->state.o.x;
            c->md.flagpos.y = c->state.o.y;
            c->md.flagmillis = servmillis;
            if (m_ctf) addpt(c, CTFPICKPT);
            break;
        case FM_DROP:
            if (m_ctf) addpt(c, CTFDROPPT);
            break;
        case FM_LOST:
            if (m_htf) addpt(c, HTFLOSTPT);
            else if (m_ctf) {
                distance = sqrt(POW2XY(c->state.o,c->md.flagpos));
                if (distance > 200) distance = 200;                   // ~200 is the distance between the flags in ac_depot
                addpt(c, CTFLOSTPT);
            }
            break;
        case FM_RETURN:
            addpt(c, CTFRETURNPT);
            break;
        case FM_SCORE:
            if (m_ctf) {
                distance = sqrt(POW2XY(c->state.o,c->md.flagpos));
                if (distance > 200) distance = 200;
#ifdef ACAC
                if ( validflagscore(distance,c) )
#endif
                    addpt(c, CTFSCOREPT);
            } else addpt(c, HTFSCOREPT);
            break;
        case FM_KTFSCORE:
            addpt(c, KTFSCOREPT);
            break;
        default:
            break;
    }
}


static inline int minhits2combo(int gun)
{
    switch (gun)
    {
        case GUN_SUBGUN:
        case GUN_AKIMBO:
            return 4;
        case GUN_GRENADE:
        case GUN_ASSAULT:
            return 3;
        default:
            return 2;
    }
}

static void checkcombo(client *target, client *actor, int damage, int gun)
{
    int diffhittime = servmillis - actor->md.lasthit;
    actor->md.lasthit = servmillis;
    if ((gun == GUN_SHOTGUN || gun == GUN_GRENADE) && damage < 20)
    {
        actor->md.lastgun = gun;
        return;
    }

    if ( diffhittime < 750 ) {
        if ( gun == actor->md.lastgun )
        {
            if ( diffhittime * 2 < guns[gun].attackdelay * 3 )
            {
                actor->md.combohits++;
                actor->md.combotime+=diffhittime;
                actor->md.combodamage+=damage;
                int mh2c = minhits2combo(gun);
                if ( actor->md.combohits > mh2c && actor->md.combohits % mh2c == 1 )
                {
                    if (actor->md.combo < 5) actor->md.combo++;
                    actor->md.ncombos++;
                }
            }
        }
        else
        {
            switch (gun)
            {
                case GUN_KNIFE:
                case GUN_PISTOL:
                    if ( guns[actor->md.lastgun].isauto ) break;
                case GUN_SNIPER:
                    if ( actor->md.lastgun > GUN_PISTOL ) break;
                default:
                    actor->md.combohits++;
                    actor->md.combotime+=diffhittime;
                    actor->md.combodamage+=damage;
                    if (actor->md.combo < 5) actor->md.combo++;
                    actor->md.ncombos++;
                    break;
            }
        }
    }
    else
    {
        actor->md.combo = 0;
        actor->md.combofrags = 0;
        actor->md.combotime = 0;
        actor->md.combodamage = 0;
        actor->md.combohits = 0;
    }
    actor->md.lastgun = gun;
}

#define COVERDIST 2000 // about 45 cubes
#define REPLYDIST 8000 // about 90 cubes
static float coverdist = COVERDIST;

static int checkteamrequests(int sender)
{
    int dtime, besttime = -1;
    int bestid = -1;
    client *ant = clients[sender];
    loopv(clients) {
        client *prot = clients[i];
        if ( i!=sender && prot->type != ST_EMPTY && prot->team == ant->team &&
             prot->state.state == CS_ALIVE && prot->md.askmillis > gamemillis && prot->md.ask >= 0 ) {
            float dist = POW2XY(ant->state.o,prot->state.o);
            if ( dist < REPLYDIST && (dtime=prot->md.askmillis-gamemillis) > besttime) {
                bestid = i;
                besttime = dtime;
            }
        }
    }
    if ( besttime >= 0 ) return bestid;
    return -1;
}

/** WIP */
static void checkteamplay(int s, int sender)
{
    client *actor = clients[sender];

    if ( actor->state.state != CS_ALIVE ) return;
    switch(s){
        case S_IMONDEFENSE: // informs
            actor->md.linkmillis = gamemillis + 20000;
            actor->md.linkreason = s;
            break;
        case S_COVERME: // demands
        case S_STAYTOGETHER:
        case S_STAYHERE:
            actor->md.ask = s;
            actor->md.askmillis = gamemillis + 5000;
            break;
        case S_AFFIRMATIVE: // replies
        case S_ALLRIGHTSIR:
        case S_YES:
        {
            int id = checkteamrequests(sender);
            if ( id >= 0 ) {
                client *sgt = clients[id];
                actor->md.linked = id;
                if ( actor->md.linkmillis < gamemillis ) addpt(actor,REPLYPT);
                actor->md.linkmillis = gamemillis + 30000;
                actor->md.linkreason = sgt->md.ask;
                sendf(actor->clientnum, 1, "ri2", SV_HUDEXTRAS, HE_NUM+id);
                switch( actor->md.linkreason ) { // check demands
                    case S_STAYHERE:
                        actor->md.pos = sgt->state.o;
                        addpt(sgt,REPLYPT);
                        break;
                }
            }
            break;
        }
    }
}

static void computeteamwork(int team, int exclude) // testing
{
    loopv(clients)
    {
        client *actor = clients[i];
        if ( i == exclude || actor->type == ST_EMPTY || actor->team != team || actor->state.state != CS_ALIVE || actor->md.linkmillis < gamemillis ) continue;
        vec position;
        bool teamworkdone = false;
        switch( actor->md.linkreason )
        {
            case S_IMONDEFENSE:
                position = actor->spawnp;
                teamworkdone = true;
                break;
            case S_STAYTOGETHER:
                if ( valid_client(actor->md.linked) ) position = clients[actor->md.linked]->state.o;
                teamworkdone = true;
                break;
            case S_STAYHERE:
                position = actor->md.pos;
                teamworkdone = true;
                break;
        }
        if ( teamworkdone )
        {
            float dist = POW2XY(actor->state.o,position);
            if (dist < COVERDIST)
            {
                addpt(actor,TWDONEPT);
                sendf(actor->clientnum, 1, "ri2", SV_HUDEXTRAS, HE_TEAMWORK);
            }
        }
    }
}

static float a2c = 0, c2t = 0, a2t = 0; // distances: actor to covered, covered to target and actor to target

static inline bool testcover(int msg, int factor, client *actor)
{
    if ( a2c < coverdist && c2t < coverdist && a2t < coverdist )
    {
        sendf(actor->clientnum, 1, "ri2", SV_HUDEXTRAS, msg);
        addpt(actor, factor);
        actor->md.ncovers++;
        return true;
    }
    return false;
}

#define CALCCOVER(C) \
    a2c = POW2XY(actor->state.o,C);\
    c2t = POW2XY(C,target->state.o);\
    a2t = POW2XY(actor->state.o,target->state.o)

static bool validlink(client *actor, int cn)
{
    return actor->md.linked >= 0 && actor->md.linked == cn && gamemillis < actor->md.linkmillis && valid_client(actor->md.linked);
}

/** WIP */
static void checkcover(client *target, client *actor)
{
    int team = actor->team;
    int oteam = team_opposite(team);

    bool covered = false;
    int coverid = -1;

    int cnumber = totalclients < 13 ? totalclients : 12;

    if ( m_flags ) {
        sflaginfo &f = sflaginfos[team];
        sflaginfo &of = sflaginfos[oteam];

        if ( m_ctf )
        {
            if ( f.state == CTFF_INBASE )
            {
                CALCCOVER(f);
                if ( testcover(HE_FLAGDEFENDED, CTFLDEFPT, actor) ) print_medal_messages(actor,CTFLDEF);
            }
            if ( of.state == CTFF_STOLEN && actor->clientnum != of.actor_cn )
            {
                covered = true; coverid = of.actor_cn;
                CALCCOVER(clients[of.actor_cn]->state.o);
                if ( testcover(HE_FLAGCOVERED, CTFLCOVPT, actor) ) print_medal_messages(actor,CTFLCOV);
            }
        }
        else if ( m_htf )
        {
            if ( actor->clientnum != f.actor_cn )
            {
                if ( f.state == CTFF_DROPPED )
                {
                    struct { short x, y; } nf;
                    nf.x = f.pos[0]; nf.y = f.pos[1];
                    CALCCOVER(nf);
                    if ( testcover(HE_FLAGDEFENDED, HTFLDEFPT, actor) ) print_medal_messages(actor,HTFLDEF);
                }
                if ( f.state == CTFF_STOLEN )
                {
                    covered = true; coverid = f.actor_cn;
                    CALCCOVER(clients[f.actor_cn]->state.o);
                    if ( testcover(HE_FLAGCOVERED, HTFLCOVPT, actor) ) print_medal_messages(actor,HTFLCOV);
                }
            }
        }
    }

    if ( !(covered && actor->md.linked==coverid) && validlink(actor,actor->md.linked) )
    {
        CALCCOVER(clients[actor->md.linked]->state.o);
        if ( testcover(HE_COVER, COVERPT, actor) ) print_medal_messages(actor,COVER);
    }

}

#undef CALCCOVER

/** WiP */
static void checkfrag(client *target, client *actor, int gun, bool gib)
{
    int targethasflag = clienthasflag(target->clientnum);
    int actorhasflag = clienthasflag(actor->clientnum);
    int cnumber = totalclients < 13 ? totalclients : 12;
    addpt(target,DEATHPT);
    if(target!=actor) {
        if(!isteam(target->team, actor->team)) {

            if (m_teammode) {
                if(!m_flags) addpt(actor, TMBONUSPT);
                else addpt(actor, FLBONUSPT);

                checkcover (target, actor);
                if ( m_htf && actorhasflag >= 0 ) addpt(actor, HTFFRAGPT);

                if ( m_ctf && targethasflag >= 0 ) {
                    addpt(actor, CTFFRAGPT);
                }
            }
            else addpt(actor, BONUSPT);

            if (gib && gun != GUN_GRENADE) {
                if ( gun == GUN_SNIPER ) {
                    addpt(actor, HEADSHOTPT);
                    actor->md.nhs++;
                }
                else if ( gun == GUN_KNIFE ) addpt(actor, KNIFEPT);
                else if ( gun == GUN_SHOTGUN ) addpt(actor, SHOTGPT);
            }
            else addpt(actor, FRAGPT);

            if ( actor->md.combo ) {
                actor->md.combofrags++;
                addpt(actor,COMBOPT);
                actor->md.combosend = true;
            }

        } else {

            if ( targethasflag >= 0 ) addpt(actor, FLAGTKPT);
            else addpt(actor, TKPT);

        }
    }
}

static int next_afk_check = 200;

/* this function is managed to the PUBS, id est, many people playing in an open server */
static void check_afk()
{
    next_afk_check = servmillis + 7 * 1000;
    /* if we have few people (like 2x2), or it is not a teammode with the server not full: do nothing! */
    if ( totalclients < 5 || ( totalclients < scl.maxclients && !m_teammode)  ) return;
    loopv(clients)
    {
        client &c = *clients[i];
        if ( c.type != ST_TCPIP || c.connectmillis + 60 * 1000 > servmillis ||
             c.inputmillis + scl.afk_limit > servmillis || clienthasflag(c.clientnum) != -1 ) continue;
        if ( ( c.state.state == CS_DEAD && !m_arena && c.state.lastdeath + 45 * 1000 < gamemillis) ||
             ( c.state.state == CS_ALIVE && c.upspawnp ) /*||
             ( c.state.state == CS_SPECTATE && totalclients >= scl.maxclients )  // only kick spectator if server is full - 2011oct16:flowtron: mmh, that seems reasonable enough .. still, kicking spectators for inactivity seems harsh! disabled ATM, kick them manually if you must.
             */
            )
        {
            logline(ACLOG_INFO, "[%s] %s %s", c.hostname, c.name, "is afk");
            defformatstring(msg)("%s is afk", c.name);
            sendservmsg(msg);
            disconnect_client(c.clientnum, DISC_AFK);
        }
    }
}

/** This function counts how much non-killing-damage the player does to any teammates
    The damage limit is 100 hp per minute, which is about 2 tks per minute in a normal game
    In normal games, the players go over 6 tks only in the worst cases */
static void check_ffire(client *target, client *actor, int damage)
{
    if ( mastermode != MM_OPEN ) return;
    actor->ffire += damage;
    if ( actor->ffire > 300 && actor->ffire * 600 > gamemillis) {
        logline(ACLOG_INFO, "[%s] %s %s", actor->hostname, actor->name, "kicked for excessive friendly fire");
        defformatstring(msg)("%s %s", actor->name, "kicked for excessive friendly fire");
        sendservmsg(msg);
        disconnect_client(actor->clientnum, DISC_FFIRE);
    }
}

static inline int check_pdist(client *c, float & dist) // pick up distance
{
    // ping 1000ms at max velocity can produce an error of 20 cubes
    float delay = 9.0f + (float)c->ping * 0.02f + (float)c->spj * 0.025f; // at pj/ping 40/100, delay = 12
    if ( dist > delay )
    {
        if ( dist < 1.5f * delay ) return 1;
#ifdef ACAC
        pickup_checks(c,dist-delay);
#endif
        return 2;
    }
    return 0;
}

/**
If you read README.txt you must know that AC does not have cheat protection implemented.
However this file is the sketch to a very special kind of cheat detection tools in server side.

This is not based in program tricks, i.e., encryption, secret bytes, nor monitoring/scanning tools.

The idea behind these cheat detections is to check (or reproduce) the client data, and verify if
this data is expected or possible. Also, there is no need to check all clients all time, and
one coding this kind of check must pay a special attention to the lag effect and how it can
affect the data observed. This is not a trivial task, and probably it is the main reason why
such tools were never implemented.

This part is here for compatibility purposes.
If you know nothing about these detections, please, just ignore it.
*/

static inline void checkmove(client *cl)
{
    cl->ldt = gamemillis - cl->lmillis;
    cl->lmillis = gamemillis;
    if ( cl->ldt < 40 ) cl->ldt = 40;
    cl->t += cl->ldt;
    cl->spj = (( 7 * cl->spj + cl->ldt ) >> 3);

    if ( cl->input != cl->f )
    {
        cl->input = cl->f;
        cl->inputmillis = servmillis;
    }

    if(maplayout) checkclientpos(cl);

#ifdef ACAC
    m_engine(cl);
#endif

    if ( !cl->upspawnp )
    {
        cl->spawnp = cl->state.o;
        cl->upspawnp = true;
        cl->spj = cl->ldt = 40;
    }

    return;
}

static inline void checkshoot(int & cn, gameevent & shot, int & hits, int & tcn)
{
#ifdef ACAC
    s_engine(cn, shot, hits, tcn);
#endif
    return;
}

static inline void checkweapon(int & type, int & var)
{
#ifdef ACAC
    w_engine(type,var);
#endif
    return;
}

static bool validdamage(client *&target, client *&actor, int &damage, int &gun, bool &gib)
{
#ifdef ACAC
    if (!d_engine(target, actor, damage, gun, gib)) return false;
#endif
    return true;
}

static inline int checkmessage(client *c, int type)
{
#ifdef ACAC
    type = p_engine(c,type);
#endif
    return type;
}

static bool flagdistance(sflaginfo &f, int cn)
{
    if(!valid_client(cn) || m_demo) return false;
    client &c = *clients[cn];
    vec v(-1, -1, c.state.o.z);
    switch(f.state)
    {
        case CTFF_INBASE:
            v.x = f.x; v.y = f.y;
            break;
        case CTFF_DROPPED:
            v.x = f.pos[0]; v.y = f.pos[1];
            break;
    }
    bool lagging = (c.ping > 1000 || c.spj > 100);
    if(v.x < 0 && !lagging) return true;
    float dist = c.state.o.dist(v);
    int pdist = check_pdist(&c,dist);
    if(pdist)
    {
        c.farpickups++;
        logline(ACLOG_INFO, "[%s] %s %s the %s flag at distance %.2f (%d)",
                c.hostname, c.name, (pdist==2?"tried to touch":"touched"), team_string(&f == sflaginfos + 1), dist, c.farpickups);
        if (pdist==2) return false;
    }
    return lagging ? false : true; // today I found a lag hacker :: Brahma, 19-oct-2010... lets test it a bit
}

static void sendflaginfo(int flag = -1, int cn = -1)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    if(flag >= 0) putflaginfo(p, flag);
    else loopi(2) putflaginfo(p, i);
    sendpacket(cn, 1, p.finalize());
}

static void flagmessage(int flag, int message, int actor, int cn = -1)
{
    if(message == FM_KTFSCORE)
        sendf(cn, 1, "riiiii", SV_FLAGMSG, flag, message, actor, (gamemillis - sflaginfos[flag].stolentime) / 1000);
    else
        sendf(cn, 1, "riiii", SV_FLAGMSG, flag, message, actor);
}

static void flagaction(int flag, int action, int actor)
{
    if(!valid_flag(flag)) return;
    sflaginfo &f = sflaginfos[flag];
    sflaginfo &of = sflaginfos[team_opposite(flag)];
    bool deadactor = valid_client(actor) ? clients[actor]->state.state != CS_ALIVE || team_isspect(clients[actor]->team): true;
    int abort = 0;
    int score = 0;
    int message = -1;

    if(m_ctf || m_htf)
    {
        switch(action)
        {
            case FA_PICKUP:  // ctf: f = enemy team    htf: f = own team
            case FA_STEAL:
            {
                if(deadactor || f.state != (action == FA_STEAL ? CTFF_INBASE : CTFF_DROPPED) || !flagdistance(f, actor)) { abort = 10; break; }
                int team = team_base(clients[actor]->team);
                if(m_ctf) team = team_opposite(team);
                if(team != flag) { abort = 11; break; }
                f.state = CTFF_STOLEN;
                f.actor_cn = actor;
                message = FM_PICKUP;
                break;
            }
            case FA_LOST:
                if(actor == -1) actor = f.actor_cn;
            case FA_DROP:
                if(f.state!=CTFF_STOLEN || f.actor_cn != actor) { abort = 12; break; }
                f.state = CTFF_DROPPED;
                loopi(3) f.pos[i] = clients[actor]->state.o[i];
                message = action == FA_LOST ? FM_LOST : FM_DROP;
                break;
            case FA_RETURN:
                if(f.state!=CTFF_DROPPED || m_htf) { abort = 13; break; }
                f.state = CTFF_INBASE;
                message = FM_RETURN;
                break;
            case FA_SCORE:  // ctf: f = carried by actor flag,  htf: f = hunted flag (run over by actor)
                if(m_ctf)
                {
                    if(f.state != CTFF_STOLEN || f.actor_cn != actor || of.state != CTFF_INBASE || !flagdistance(of, actor)) { abort = 14; break; }
                    score = 1;
                    message = FM_SCORE;
                }
                else // m_htf
                {
                    if(f.state != CTFF_DROPPED || !flagdistance(f, actor)) { abort = 15; break; }
                    score = (of.state == CTFF_STOLEN) ? 1 : 0;
                    message = score ? FM_SCORE : FM_SCOREFAIL;
                    if(of.actor_cn == actor) score *= 2;
                }
                f.state = CTFF_INBASE;
                break;

            case FA_RESET:
                f.state = CTFF_INBASE;
                message = FM_RESET;
                break;
        }
    }
    else if(m_ktf)  // f: active flag, of: idle flag
    {
        switch(action)
        {
            case FA_STEAL:
                if(deadactor || f.state != CTFF_INBASE || !flagdistance(f, actor)) { abort = 20; break; }
                f.state = CTFF_STOLEN;
                f.actor_cn = actor;
                f.stolentime = gamemillis;
                message = FM_PICKUP;
                break;
            case FA_SCORE:  // f = carried by actor flag
                if(actor != -1 || f.state != CTFF_STOLEN) { abort = 21; break; } // no client msg allowed here
                if(valid_client(f.actor_cn) && clients[f.actor_cn]->state.state == CS_ALIVE && !team_isspect(clients[f.actor_cn]->team))
                {
                    actor = f.actor_cn;
                    score = 1;
                    message = FM_KTFSCORE;
                    break;
                }
            case FA_LOST:
                if(actor == -1) actor = f.actor_cn;
            case FA_DROP:
                if(f.actor_cn != actor || f.state != CTFF_STOLEN) { abort = 22; break; }
            case FA_RESET:
                if(f.state == CTFF_STOLEN)
                {
                    actor = f.actor_cn;
                    message = FM_LOST;
                }
                f.state = CTFF_IDLE;
                of.state = CTFF_INBASE;
                sendflaginfo(team_opposite(flag));
                break;
        }
    }
    if(abort)
    {
        logline(ACLOG_DEBUG,"aborting flagaction(flag %d, action %d, actor %d), reason %d, resending flag states", flag, action, actor, abort);  // FIXME: remove this logline after some time - it will only show a few bad ping effects
        sendflaginfo();
        return;
    }
    if(score)
    {
        client *c = clients[actor];
        c->state.flagscore += score;
        sendf(-1, 1, "riii", SV_FLAGCNT, actor, c->state.flagscore);
        if (m_teammode) computeteamwork(c->team, c->clientnum); /** WIP */
    }
    if(valid_client(actor))
    {
        client &c = *clients[actor];
        switch(message)
        {
            case FM_PICKUP:
                logline(ACLOG_INFO,"[%s] %s %s the flag", c.hostname, c.name, action == FA_STEAL ? "stole" : "picked up");
                break;
            case FM_DROP:
            case FM_LOST:
                logline(ACLOG_INFO,"[%s] %s %s the flag", c.hostname, c.name, message == FM_LOST ? "lost" : "dropped");
                break;
            case FM_RETURN:
                logline(ACLOG_INFO,"[%s] %s returned the flag", c.hostname, c.name);
                break;
            case FM_SCORE:
                if(m_htf)
                    logline(ACLOG_INFO, "[%s] %s hunted the flag for %s, new score %d", c.hostname, c.name, team_string(c.team), c.state.flagscore);
                else
                   logline(ACLOG_INFO, "[%s] %s scored with the flag for %s, new score %d", c.hostname, c.name, team_string(c.team), c.state.flagscore);
                break;
            case FM_KTFSCORE:
                logline(ACLOG_INFO, "[%s] %s scored, carrying for %d seconds, new score %d", c.hostname, c.name, (gamemillis - f.stolentime) / 1000, c.state.flagscore);
                break;
            case FM_SCOREFAIL:
                logline(ACLOG_INFO, "[%s] %s failed to score", c.hostname, c.name);
                break;
            default:
                logline(ACLOG_INFO, "flagaction %d, actor %d, flag %d, message %d", action, actor, flag, message);
                break;
        }
        flagpoints (&c, message);
    }
    else
    {
        switch(message)
        {
            case FM_RESET:
                logline(ACLOG_INFO,"the server reset the flag for team %s", team_string(flag));
                break;
            default:
                logline(ACLOG_INFO, "flagaction %d with invalid actor cn %d, flag %d, message %d", action, actor, flag, message);
                break;
        }
    }

    f.lastupdate = gamemillis;
    sendflaginfo(flag);
    if(message >= 0)
        flagmessage(flag, message, valid_client(actor) ? actor : -1);
}

static int clienthasflag(int cn)
{
    if(m_flags && valid_client(cn))
    {
        loopi(2) { if(sflaginfos[i].state==CTFF_STOLEN && sflaginfos[i].actor_cn==cn) return i; }
    }
    return -1;
}

static void ctfreset()
{
    int idleflag = m_ktf ? rnd(2) : -1;
    loopi(2)
    {
        sflaginfos[i].actor_cn = -1;
        sflaginfos[i].state = i == idleflag ? CTFF_IDLE : CTFF_INBASE;
        sflaginfos[i].lastupdate = -1;
    }
}

static void sdropflag(int cn)
{
    int fl = clienthasflag(cn);
    if(fl >= 0) flagaction(fl, FA_LOST, cn);
}

static void resetflag(int cn)
{
    int fl = clienthasflag(cn);
    if(fl >= 0) flagaction(fl, FA_RESET, -1);
}

static void htf_forceflag(int flag)
{
    sflaginfo &f = sflaginfos[flag];
    int besthealth = 0;
    vector<int> clientnumbers;
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if(clients[i]->state.state == CS_ALIVE && team_base(clients[i]->team) == flag)
        {
            if(clients[i]->state.health == besthealth)
                clientnumbers.add(i);
            else
            {
                if(clients[i]->state.health > besthealth)
                {
                    besthealth = clients[i]->state.health;
                    clientnumbers.shrink(0);
                    clientnumbers.add(i);
                }
            }
        }
    }

    if(clientnumbers.length())
    {
        int pick = rnd(clientnumbers.length());
        client *cl = clients[clientnumbers[pick]];
        f.state = CTFF_STOLEN;
        f.actor_cn = cl->clientnum;
        sendflaginfo(flag);
        flagmessage(flag, FM_PICKUP, cl->clientnum);
        logline(ACLOG_INFO,"[%s] %s got forced to pickup the flag", cl->hostname, cl->name);
    }
    f.lastupdate = gamemillis;
}

static int arenaround = 0, arenaroundstartmillis = 0;

struct twoint { int index, value; };
static int cmpscore(const int *a, const int *b) { return clients[*a]->at3_score - clients[*b]->at3_score; }
static int cmptwoint(const struct twoint *a, const struct twoint *b) { return a->value - b->value; }
static vector<int> tdistrib;
static vector<twoint> sdistrib;

static void distributeteam(int team)
{
    int numsp = team == 100 ? smapstats.spawns[2] : smapstats.spawns[team];
    if(!numsp) numsp = 30; // no spawns: try to distribute anyway
    twoint ti;
    tdistrib.shrink(0);
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if(team == 100 || team == clients[i]->team)
        {
            tdistrib.add(i);
            clients[i]->at3_score = rnd(0x1000000);
        }
    }
    tdistrib.sort(cmpscore); // random player order
    sdistrib.shrink(0);
    loopi(numsp)
    {
        ti.index = i;
        ti.value = rnd(0x1000000);
        sdistrib.add(ti);
    }
    sdistrib.sort(cmptwoint); // random spawn order
    int x = 0;
    loopv(tdistrib)
    {
        clients[tdistrib[i]]->spawnindex = sdistrib[x++].index;
        x %= sdistrib.length();
    }
}

static void distributespawns()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        clients[i]->spawnindex = -1;
    }
    if(m_teammode)
    {
        distributeteam(0);
        distributeteam(1);
    }
    else
    {
        distributeteam(100);
    }
}

static bool items_blocked = false;
static bool free_items(int n)
{
    client *c = clients[n];
    int waitspawn = min(c->ping,200) + c->state.spawn; // flowtron to Brahma: can't this be removed now? (re: rev. 5270)
    return !items_blocked && (waitspawn < gamemillis);
}

static void checkitemspawns(int diff)
{
    if(!diff) return;
    loopv(sents) if(sents[i].spawntime)
    {
        sents[i].spawntime -= diff;
        if(sents[i].spawntime<=0)
        {
            sents[i].spawntime = 0;
            sents[i].spawned = true;
            sendf(-1, 1, "ri2", SV_ITEMSPAWN, i);
        }
    }
}

static void arenacheck()
{
    if(!m_arena || interm || gamemillis<arenaround || !numactiveclients()) return;

    if(arenaround)
    {   // start new arena round
        arenaround = 0;
        arenaroundstartmillis = gamemillis;
        distributespawns();
        checkitemspawns(60*1000); // the server will respawn all items now
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed)
        {
            if(clients[i]->isonrightmap && team_isactive(clients[i]->team))
                sendspawn(clients[i]);
        }
        items_blocked = false;
        return;
    }

#ifndef STANDALONE
    if(m_botmode && clients[0]->type==ST_LOCAL)
    {
        int enemies = 0, alive_enemies = 0;
        playerent *alive = NULL;
        loopv(players) if(players[i] && (!m_teammode || players[i]->team == team_opposite(player1->team)))
        {
            enemies++;
            if(players[i]->state == CS_ALIVE)
            {
                alive = players[i];
                alive_enemies++;
            }
        }
        if(player1->state != CS_DEAD) alive = player1;
        if(enemies && (!alive_enemies || player1->state == CS_DEAD))
        {
            sendf(-1, 1, "ri2", SV_ARENAWIN, m_teammode ? (alive ? alive->clientnum : -1) : (alive && alive->type == ENT_BOT ? -2 : player1->state == CS_ALIVE ? player1->clientnum : -1));
            arenaround = gamemillis+5000;
        }
        return;
    }
#endif
    client *alive = NULL;
    bool dead = false;
    int lastdeath = 0;
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY || !c.isauthed || !c.isonrightmap || team_isspect(c.team)) continue;
        if(c.state.state==CS_ALIVE || ((c.state.state==CS_DEAD || c.state.state==CS_SPECTATE) && c.state.lastspawn>=0))
        {
            if(!alive) alive = &c;
            else if(!m_teammode || alive->team != c.team) return;
        }
        else if(c.state.state==CS_DEAD || c.state.state==CS_SPECTATE)
        {
            dead = true;
            lastdeath = max(lastdeath, c.state.lastdeath);
        }
    }

    if(autoteam && m_teammode && mastermode != MM_MATCH)
    {
        int *ntc = numteamclients();
        if((!ntc[0] || !ntc[1]) && (ntc[0] > 1 || ntc[1] > 1)) refillteams(true, FTR_AUTOTEAM);
    }
    if(!dead || gamemillis < lastdeath + 500) return;
    items_blocked = true;
    sendf(-1, 1, "ri2", SV_ARENAWIN, alive ? alive->clientnum : -1);
    arenaround = gamemillis+5000;
    if(autoteam && m_teammode) refillteams(true);
}

#define SPAMREPEATINTERVAL  20   // detect doubled lines only if interval < 20 seconds
#define SPAMMAXREPEAT       3    // 4th time is SPAM
#define SPAMCHARPERMINUTE   220  // good typist
#define SPAMCHARINTERVAL    30   // allow 20 seconds typing at maxspeed

static bool spamdetect(client *cl, char *text) // checks doubled lines and average typing speed
{
    if(cl->type != ST_TCPIP || cl->role == CR_ADMIN) return false;
    bool spam = false;
    int pause = servmillis - cl->lastsay;
    if(pause < 0 || pause > 90*1000) pause = 90*1000;
    cl->saychars -= (SPAMCHARPERMINUTE * pause) / (60*1000);
    cl->saychars += (int)strlen(text);
    if(cl->saychars < 0) cl->saychars = 0;
    if(text[0] && !strcmp(text, cl->lastsaytext) && servmillis - cl->lastsay < SPAMREPEATINTERVAL*1000)
    {
        spam = ++cl->spamcount > SPAMMAXREPEAT;
    }
    else
    {
         copystring(cl->lastsaytext, text);
         cl->spamcount = 0;
    }
    cl->lastsay = servmillis;
    if(cl->saychars > (SPAMCHARPERMINUTE * SPAMCHARINTERVAL) / 60)
        spam = true;
    return spam;
}

// chat message distribution matrix:
//
// /------------------------ common chat          C c C c c C C c C
// |/----------------------- RVSF chat            T
// ||/---------------------- CLA chat                 T
// |||/--------------------- spect chat             t   t t T   t T
// ||||                                           | | | | | | | | |
// ||||                                           | | | | | | | | |      C: normal chat
// ||||   team modes:                chat goes to | | | | | | | | |      T: team chat
// XX     -->   RVSF players                >-----/ | | | | | | | |      c: normal chat in all mastermodes except 'match'
// XX X   -->   RVSF spect players          >-------/ | | | | | | |      t: all chat in mastermode 'match', otherwise only team chat
// X X    -->   CLA players                 >---------/ | | | | | |
// X XX   -->   CLA spect players           >-----------/ | | | | |
// X  X   -->   SPECTATORs                  >-------------/ | | | |
// XXXX   -->   SPECTATORs (admin)          >---------------/ | | |
//        ffa modes:                                          | | |
// X      -->   any player (ffa mode)       >-----------------/ | |
// X  X   -->   any spectator (ffa mode)    >-------------------/ |
// X  X   -->   admin spectator             >---------------------/

// purpose:
//  a) give spects a possibility to chat without annoying the players (even in ffa),
//  b) no hidden messages from spects to active teams,
//  c) no spect talk to players during 'match'

static void sendteamtext(char *text, int sender, int msgtype)
{
    if(!valid_client(sender) || clients[sender]->team == TEAM_VOID) return;
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, msgtype);
    putint(p, sender);
    sendstring(text, p);
    ENetPacket *packet = p.finalize();
    recordpacket(1, packet);
    int &st = clients[sender]->team;
    loopv(clients) if(i!=sender)
    {
        int &rt = clients[i]->team;
        if((rt == TEAM_SPECT && clients[i]->role == CR_ADMIN) ||  // spect-admin reads all
           (team_isactive(st) && st == team_group(rt)) ||         // player to own team + own spects
           (team_isspect(st) && team_isspect(rt)))                // spectator to other spectators
            sendpacket(i, 1, packet);
    }
}

static void sendvoicecomteam(int sound, int sender)
{
    if(!valid_client(sender) || clients[sender]->team == TEAM_VOID) return;
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_VOICECOMTEAM);
    putint(p, sender);
    putint(p, sound);
    ENetPacket *packet = p.finalize();
    loopv(clients) if(i!=sender)
    {
        if(clients[i]->team == clients[sender]->team || !m_teammode)
            sendpacket(i, 1, packet);
    }
}

static int numplayers()
{
    int count = 1;
#ifdef STANDALONE
    count = numclients();
#else
    if(m_botmode)
    {
        extern vector<botent *> bots;
        loopv(bots) if(bots[i]) count++;
    }
    if(m_demo)
    {
        count = numclients();
    }
#endif
    return count;
}

static int spawntime(int type)
{
    int np = numplayers();
    np = np<3 ? 4 : (np>4 ? 2 : 3);    // Some spawn times are dependent on the number of players.
    int sec = 0;
    switch(type)
    {
    // Please update ./ac_website/htdocs/docs/introduction.html if these times change.
        case I_CLIPS:
        case I_AMMO: sec = np*2; break;
        case I_GRENADE: sec = np + 5; break;
        case I_HEALTH: sec = np*5; break;
        case I_HELMET:
        case I_ARMOUR: sec = 25; break;
        case I_AKIMBO: sec = 60; break;
    }
    return sec*1000;
}

bool serverpickup(int i, int sender)         // server side item pickup, acknowledge first client that gets it
{
    const char *hn = sender >= 0 && clients[sender]->type == ST_TCPIP ? clients[sender]->hostname : NULL;
    if(!sents.inrange(i))
    {
        if(hn && !m_coop) logline(ACLOG_INFO, "[%s] tried to pick up entity #%d - doesn't exist on this map", hn, i);
        return false;
    }
    server_entity &e = sents[i];
    if(!e.spawned)
    {
        if(!e.legalpickup && hn && !m_demo) logline(ACLOG_INFO, "[%s] tried to pick up entity #%d (%s) - can't be picked up in this gamemode or at all", hn, i, entnames[e.type]);
        return false;
    }
    if(sender>=0)
    {
        client *cl = clients[sender];
        if(cl->type==ST_TCPIP)
        {
            if( cl->state.state!=CS_ALIVE || !cl->state.canpickup(e.type) || ( m_arena && !free_items(sender) ) ) return false;
            vec v(e.x, e.y, cl->state.o.z);
            float dist = cl->state.o.dist(v);
            int pdist = check_pdist(cl,dist);
            if (pdist)
            {
                cl->farpickups++;
                if (!m_demo) logline(ACLOG_INFO, "[%s] %s %s up entity #%d (%s), distance %.2f (%d)",
                     cl->hostname, cl->name, (pdist==2?"tried to pick":"picked"), i, entnames[e.type], dist, cl->farpickups);
                if (pdist==2) return false;
            }
        }
        sendf(-1, 1, "ri3", SV_ITEMACC, i, sender);
        cl->state.pickup(sents[i].type);
        if (m_lss && sents[i].type == I_GRENADE) cl->state.pickup(sents[i].type); // get two nades at lss
    }
    e.spawned = false;
    if(!m_lms) e.spawntime = spawntime(e.type);
    return true;
}

static void serverdamage(client *target, client *actor, int damage, int gun, bool gib, const vec &hitpush = vec(0, 0, 0))
{
    if (!m_demo && !m_coop && !validdamage(target, actor, damage, gun, gib)) return;
    if ( m_arena && gun == GUN_GRENADE && arenaroundstartmillis + 2000 > gamemillis && target != actor ) return;
    clientstate &ts = target->state;
    ts.dodamage(damage, gun);
    if(damage < INT_MAX)
    {
        actor->state.damage += damage;
        sendf(-1, 1, "ri7", gib ? SV_GIBDAMAGE : SV_DAMAGE, target->clientnum, actor->clientnum, gun, damage, ts.armour, ts.health);
        if(target!=actor)
        {
            checkcombo (target, actor, damage, gun);
            if(!hitpush.iszero())
            {
                vec v(hitpush);
                if(!v.iszero()) v.normalize();
                sendf(target->clientnum, 1, "ri6", SV_HITPUSH, gun, damage,
                      int(v.x*DNF), int(v.y*DNF), int(v.z*DNF));
            }
        }
    }
    if(ts.health<=0)
    {
        int targethasflag = clienthasflag(target->clientnum);
        bool tk = false, suic = false;
        target->state.deaths++;
        checkfrag(target, actor, gun, gib);
        if(target!=actor)
        {
            if(!isteam(target->team, actor->team)) actor->state.frags += gib && gun != GUN_GRENADE && gun != GUN_SHOTGUN ? 2 : 1;
            else
            {
                actor->state.frags--;
                actor->state.teamkills++;
                tk = true;
            }
        }
        else
        { // suicide
            actor->state.frags--;
            suic = true;
            logline(ACLOG_INFO, "[%s] %s suicided", actor->hostname, actor->name);
        }
        sendf(-1, 1, "ri5", gib ? SV_GIBDIED : SV_DIED, target->clientnum, actor->clientnum, actor->state.frags, gun);
        if((suic || tk) && (m_htf || m_ktf) && targethasflag >= 0)
        {
            actor->state.flagscore--;
            sendf(-1, 1, "riii", SV_FLAGCNT, actor->clientnum, actor->state.flagscore);
        }
        target->position.setsize(0);
        ts.state = CS_DEAD;
        ts.lastdeath = gamemillis;
        if(!suic) logline(ACLOG_INFO, "[%s] %s %s%s %s", actor->hostname, actor->name, killmessage(gun, gib), tk ? " their teammate" : "", target->name);
        if(m_flags && targethasflag >= 0)
        {
            if(m_ctf)
                flagaction(targethasflag, tk ? FA_RESET : FA_LOST, -1);
            else if(m_htf)
                flagaction(targethasflag, FA_LOST, -1);
            else // ktf || tktf
                flagaction(targethasflag, FA_RESET, -1);
        }
        // don't issue respawn yet until DEATHMILLIS has elapsed
        // ts.respawn();

        if(isdedicated && actor->type == ST_TCPIP && tk)
        {
            if( actor->state.frags < scl.banthreshold ||
                /** teamkilling more than 6 (defaults), more than 2 per minute and less than 4 frags per tk */
                ( actor->state.teamkills >= -scl.banthreshold &&
                  actor->state.teamkills * 30 * 1000 > gamemillis &&
                  actor->state.frags < 4 * actor->state.teamkills ) )
            {
                addban(actor, DISC_AUTOBAN);
            }
            else if( actor->state.frags < scl.kickthreshold ||
                     /** teamkilling more than 5 (defaults), more than 1 tk per minute and less than 4 frags per tk */
                     ( actor->state.teamkills >= -scl.kickthreshold &&
                       actor->state.teamkills * 60 * 1000 > gamemillis &&
                       actor->state.frags < 4 * actor->state.teamkills ) ) disconnect_client(actor->clientnum, DISC_AUTOKICK);
        }
    } else if ( target!=actor && isteam(target->team, actor->team) ) check_ffire (target, actor, damage); // friendly fire counter
}

static void processevent(client *c, explodeevent &e)
{
    clientstate &gs = c->state;
    switch(e.gun)
    {
        case GUN_GRENADE:
            if(!gs.grenades.remove(e.id)) return;
            break;

        default:
            return;
    }
    for(int i = 1; i<c->events.length() && c->events[i].type==GE_HIT; i++)
    {
        hitevent &h = c->events[i].hit;
        if(!clients.inrange(h.target)) continue;
        client *target = clients[h.target];
        if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence || h.dist<0 || h.dist>EXPDAMRAD) continue;

        int j = 1;
        for(j = 1; j<i; j++) if(c->events[j].hit.target==h.target) break;
        if(j<i) continue;

        int damage = int(guns[e.gun].damage*(1-h.dist/EXPDAMRAD));
        bool chk_gun = e.gun==GUN_GRENADE;
        bool chk_dir = h.dir[0]+h.dir[1]+h.dir[2]==0;
        bool chk_dst = h.dist < 2.0f;
        bool chk_cnr = c->clientnum == target->clientnum;
        if(chk_gun && chk_dir && chk_dst && chk_cnr) damage = INT_MAX; // nade suicide
        serverdamage(target, c, damage, e.gun, true, h.dir);
    }
}

static void processevent(client *c, shotevent &e)
{
    clientstate &gs = c->state;
    int wait = e.millis - gs.lastshot;
    if(!gs.isalive(gamemillis) ||
       e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
       wait<gs.gunwait[e.gun] ||
       gs.mag[e.gun]<=0)
        return;
    if(e.gun!=GUN_KNIFE) gs.mag[e.gun]--;
    loopi(NUMGUNS) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (e.millis-gs.lastshot), 0);
    gs.lastshot = e.millis;
    gs.gunwait[e.gun] = attackdelay(e.gun);
    if(e.gun==GUN_PISTOL && gs.akimbomillis>gamemillis) gs.gunwait[e.gun] /= 2;
    sendf(-1, 1, "ri6x", SV_SHOTFX, c->clientnum, e.gun,
//         int(e.from[0]*DMF), int(e.from[1]*DMF), int(e.from[2]*DMF),
        int(e.to[0]*DMF), int(e.to[1]*DMF), int(e.to[2]*DMF),
        c->clientnum);
    gs.shotdamage += guns[e.gun].damage*(e.gun==GUN_SHOTGUN ? SGMAXDMGLOC : 1); // 2011jan17:ft: so accuracy stays correct, since SNIPER:headshot also "exceeds expectations" we use SGMAXDMGLOC instead of SGMAXDMGABS!
    switch(e.gun)
    {
        case GUN_GRENADE: gs.grenades.add(e.id); break;
        default:
        {
            int totalrays = 0, maxrays = e.gun==GUN_SHOTGUN ? 3*SGRAYS: 1;
            int tothits_c = 0, tothits_m = 0, tothits_o = 0; // sgrays
            for(int i = 1; i<c->events.length() && c->events[i].type==GE_HIT; i++)
            {
                hitevent &h = c->events[i].hit;
                if(!clients.inrange(h.target)) continue;
                client *target = clients[h.target];
                if(target->type==ST_EMPTY || target->state.state!=CS_ALIVE || h.lifesequence!=target->state.lifesequence) continue;

                int rays = 1, damage = 0;
                bool gib = false;
                if(e.gun == GUN_SHOTGUN)
                {
                    h.info = isbigendian() ? endianswap(h.info) : h.info;
                    int bonusdist = h.info&0xFF;
                    int numhits_c = (h.info & 0x0000FF00) >> 8, numhits_m = (h.info & 0x00FF0000) >> 16, numhits_o = (h.info & 0xFF000000) >> 24;
                    tothits_c += numhits_c; tothits_m += numhits_m; tothits_o += numhits_o;
                    rays = numhits_c + numhits_m + numhits_o;

                    if(rays < 1 || tothits_c > SGRAYS || tothits_m > SGRAYS || tothits_o > SGRAYS || bonusdist > SGDMGBONUS) continue;

                    gib = rays == maxrays;
                    float fdamage = (SGDMGTOTAL/(21*100.0f)) * (numhits_o * SGCOdmg/10.0f + numhits_m * SGCMdmg/10.0f + numhits_c * SGCCdmg/10.0f);
                    fdamage += (float)bonusdist;
                    damage = (int)ceil(fdamage);
#ifdef ACAC
                    if (!sg_engine(target, c, numhits_c, numhits_m, numhits_o, bonusdist)) continue;
#endif
                }
                else
                {
                    damage = rays*guns[e.gun].damage;
                    gib = e.gun == GUN_KNIFE;
                    if(e.gun == GUN_SNIPER && h.info != 0)
                    {
                        gib = true;
                        damage *= 3;
                    }
                }
                totalrays += rays;

                if(totalrays>maxrays) continue;
                serverdamage(target, c, damage, e.gun, gib, h.dir);
            }
            break;
        }
    }
}

static void processevent(client *c, suicideevent &e)
{
    serverdamage(c, c, INT_MAX, GUN_KNIFE, false);
}

static void processevent(client *c, pickupevent &e)
{
    clientstate &gs = c->state;
    if(m_mp(gamemode) && !gs.isalive(gamemillis)) return;
    serverpickup(e.ent, c->clientnum);
}

static void processevent(client *c, reloadevent &e)
{
    clientstate &gs = c->state;
    if(!gs.isalive(gamemillis) ||
       e.gun<GUN_KNIFE || e.gun>=NUMGUNS ||
       !reloadable_gun(e.gun) ||
       gs.ammo[e.gun]<=0)
        return;

    bool akimbo = e.gun==GUN_PISTOL && gs.akimbomillis>e.millis;
    int mag = (akimbo ? 2 : 1) * magsize(e.gun), numbullets = min(gs.ammo[e.gun], mag - gs.mag[e.gun]);
    if(numbullets<=0) return;

    gs.mag[e.gun] += numbullets;
    gs.ammo[e.gun] -= numbullets;

    int wait = e.millis - gs.lastshot;
    sendf(-1, 1, "ri3", SV_RELOAD, c->clientnum, e.gun);
    if(gs.gunwait[e.gun] && wait<gs.gunwait[e.gun]) gs.gunwait[e.gun] += reloadtime(e.gun);
    else
    {
        loopi(NUMGUNS) if(gs.gunwait[i]) gs.gunwait[i] = max(gs.gunwait[i] - (e.millis-gs.lastshot), 0);
        gs.lastshot = e.millis;
        gs.gunwait[e.gun] += reloadtime(e.gun);
    }
}

static void processevent(client *c, akimboevent &e)
{
    clientstate &gs = c->state;
    if(!gs.isalive(gamemillis) || gs.akimbomillis) return;
    gs.akimbomillis = e.millis+30000;
}

static void clearevent(client *c)
{
    int n = 1;
    while(n<c->events.length() && c->events[n].type==GE_HIT) n++;
    c->events.remove(0, n);
}

static void processevents()
{
    loopv(clients)
    {
        client *c = clients[i];
        if(c->type==ST_EMPTY) continue;
        if(c->state.akimbomillis && c->state.akimbomillis < gamemillis) { c->state.akimbomillis = 0; c->state.akimbo = false; }
        while(c->events.length())
        {
            gameevent &e = c->events[0];
            if(e.type<GE_SUICIDE)
            {
                if(e.shot.millis>gamemillis) break;
                if(e.shot.millis<c->lastevent) { clearevent(c); continue; }
                c->lastevent = e.shot.millis;
            }
            switch(e.type)
            {
                case GE_SHOT: processevent(c, e.shot); break;
                case GE_EXPLODE: processevent(c, e.explode); break;
                case GE_AKIMBO: processevent(c, e.akimbo); break;
                case GE_RELOAD: processevent(c, e.reload); break;
                // untimed events
                case GE_SUICIDE: processevent(c, e.suicide); break;
                case GE_PICKUP: processevent(c, e.pickup); break;
            }
            clearevent(c);
        }
    }
}

static bool updatedescallowed(void) { return scl.servdesc_pre[0] || scl.servdesc_suf[0]; }

static void updatesdesc(const char *newdesc, ENetAddress *caller = NULL)
{
    if(!newdesc || !newdesc[0] || !updatedescallowed())
    {
        copystring(servdesc_current, scl.servdesc_full);
        custom_servdesc = false;
    }
    else
    {
        formatstring(servdesc_current)("%s%s%s", scl.servdesc_pre, newdesc, scl.servdesc_suf);
        custom_servdesc = true;
        if(caller) servdesc_caller = *caller;
    }
}

static int canspawn(client *c)   // beware: canspawn() doesn't check m_arena!
{
    if(!c || c->type == ST_EMPTY || !c->isauthed || !team_isvalid(c->team) ||
        (c->type == ST_TCPIP && (c->state.lastdeath > 0 ? gamemillis - c->state.lastdeath : servmillis - c->connectmillis) < (m_arena ? 0 : (m_flags ? 5000 : 2000))) ||
        (c->type == ST_TCPIP && (servmillis - c->connectmillis < 1000 + c->state.reconnections * 2000 &&
          gamemillis > 10000 && totalclients > 3 && !team_isspect(c->team)))) return SP_OK_NUM; // equivalent to SP_DENY
    if(!c->isonrightmap) return SP_WRONGMAP;
    if(mastermode == MM_MATCH && matchteamsize)
    {
        if(c->team == TEAM_SPECT || (team_isspect(c->team) && !m_teammode)) return SP_SPECT;
        if(c->team == TEAM_CLA_SPECT || c->team == TEAM_RVSF_SPECT)
        {
            if(numteamclients()[team_base(c->team)] >= matchteamsize) return SP_SPECT;
            else return SP_REFILLMATCH;
        }
    }
    return SP_OK;
}

static void autospawncheck()
{
    if(mastermode != MM_MATCH || !m_autospawn || interm) return;
    
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isauthed && team_isactive(clients[i]->team))
    {
        client *cl = clients[i];
        if((cl->state.state == CS_DEAD || cl->state.state == CS_SPECTATE)
            && canspawn(cl) == SP_OK && !cl->autospawn)
        {
            sendspawn(cl);
            cl->autospawn = true;
        }
    }
}

/** FIXME: this function is unnecessarily complicated */
static bool updateclientteam(int cln, int newteam, int ftr)
{
    if(!valid_client(cln)) return false;
    if(!team_isvalid(newteam) && newteam != TEAM_ANYACTIVE) newteam = TEAM_SPECT;
    client &cl = *clients[cln];
    if(cl.team == newteam && ftr != FTR_AUTOTEAM) return true; // no change
    int *teamsizes = numteamclients(cln);
    if( mastermode == MM_OPEN && cl.state.forced && ftr == FTR_PLAYERWISH &&
        newteam < TEAM_SPECT && team_base(cl.team) != team_base(newteam) ) return false; // no free will changes to forced people
    if(newteam == TEAM_ANYACTIVE) // when spawning from spect
    {
        if(mastermode == MM_MATCH && cl.team < TEAM_SPECT)
        {
            newteam = team_base(cl.team);
        }
        else
        {
            if(autoteam && teamsizes[TEAM_CLA] != teamsizes[TEAM_RVSF]) newteam = teamsizes[TEAM_CLA] < teamsizes[TEAM_RVSF] ? TEAM_CLA : TEAM_RVSF;
            else
            { // join weaker team
                int teamscore[2] = {0, 0}, sum = calcscores();
                loopv(clients) if(clients[i]->type!=ST_EMPTY && i != cln && clients[i]->isauthed && clients[i]->team != TEAM_SPECT)
                {
                    teamscore[team_base(clients[i]->team)] += clients[i]->at3_score;
                }
                newteam = sum > 200 ? (teamscore[TEAM_CLA] < teamscore[TEAM_RVSF] ? TEAM_CLA : TEAM_RVSF) : rnd(2);
            }
        }
    }
    if(ftr == FTR_PLAYERWISH)
    {
        if(mastermode == MM_MATCH && matchteamsize && m_teammode)
        {
            if(newteam != TEAM_SPECT && (team_base(newteam) != team_base(cl.team) || !m_teammode)) return false; // no switching sides in match mode when teamsize is set
        }
        if(team_isactive(newteam))
        {
            if(!m_teammode && cl.state.state == CS_ALIVE) return false;  // no comments
            if(mastermode == MM_MATCH)
            {
                if(m_teammode && matchteamsize && teamsizes[newteam] >= matchteamsize) return false;  // ensure maximum team size
            }
            else
            {
                if(m_teammode && autoteam && teamsizes[newteam] > teamsizes[team_opposite(newteam)]) return false; // don't switch to an already bigger team
            }
        }
        else if(mastermode != MM_MATCH || !m_teammode) newteam = TEAM_SPECT; // only match mode (team) has more than one spect team
    }
    if(cl.team == newteam && ftr != FTR_AUTOTEAM) return true; // no change
    if(cl.team != newteam) sdropflag(cl.clientnum);
    if(ftr != FTR_INFO && (team_isspect(newteam) || (team_isactive(newteam) && team_isactive(cl.team)))) forcedeath(&cl);
    sendf(-1, 1, "riii", SV_SETTEAM, cln, newteam | ((ftr == FTR_SILENTFORCE ? FTR_INFO : ftr) << 4));
    if(ftr != FTR_INFO && !team_isspect(newteam) && team_isspect(cl.team)) sendspawn(&cl);
    if (team_isspect(newteam)) {
        cl.state.state = CS_SPECTATE;
        cl.state.lastdeath = gamemillis;
    }
    cl.team = newteam;
    return true;
}

static int calcscores() // skill eval
{
    int sum = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        clientstate &cs = clients[i]->state;
        sum += clients[i]->at3_score = cs.points > 0 ? ufSqrt((float)cs.points) : -ufSqrt((float)-cs.points);
    }
    return sum;
}

static vector<int> shuffle;

static void shuffleteams(int ftr = FTR_AUTOTEAM)
{
    int numplayers = numclients();
    int team, sums = calcscores();
    if(gamemillis < 2 * 60 *1000)
    { // random
        int teamsize[2] = {0, 0};
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isonrightmap && !team_isspect(clients[i]->team)) // only shuffle active players
        {
            sums += rnd(1000);
            team = sums & 1;
            if(teamsize[team] >= numplayers/2) team = team_opposite(team);
            updateclientteam(i, team, ftr);
            teamsize[team]++;
            sums >>= 1;
        }
    }
    else
    { // skill sorted
        shuffle.shrink(0);
        sums /= 4 * numplayers + 2;
        team = rnd(2);
        loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->isonrightmap && !team_isspect(clients[i]->team))
        {
            clients[i]->at3_score += rnd(sums | 1);
            shuffle.add(i);
        }
        shuffle.sort(cmpscore);
        loopi(shuffle.length())
        {
            updateclientteam(shuffle[i], team, ftr);
            team = !team;
        }
    }

    if(m_ctf || m_htf)
    {
        ctfreset();
        sendflaginfo();
    }
}

static bool balanceteams(int ftr)  // pro vs noobs never more
{
    if(mastermode != MM_OPEN || totalclients < 3 ) return true;
    int tsize[2] = {0, 0}, tscore[2] = {0, 0};
    int totalscore = 0, nplayers = 0;
    int flagmult = (m_ctf ? 50 : (m_htf ? 25 : 12));

    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        client *c = clients[i];
        if(c->isauthed && team_isactive(c->team))
        {
            int time = servmillis - c->connectmillis + 5000;
            if ( time > gamemillis ) time = gamemillis + 5000;
            tsize[c->team]++;
            // effective score per minute, thanks to wtfthisgame for the nice idea
            // in a normal game, normal players will do 500 points in 10 minutes
            c->eff_score = c->state.points * 60 * 1000 / time + c->state.points / 6 + c->state.flagscore * flagmult;
            tscore[c->team] += c->eff_score;
            nplayers++;
            totalscore += c->state.points;
        }
    }

    int h = 0, l = 1;
    if ( tscore[1] > tscore[0] ) { h = 1; l = 0; }
    if ( 2 * tscore[h] < 3 * tscore[l] || totalscore < nplayers * 100 ) return true;
    if ( tscore[h] > 3 * tscore[l] && tscore[h] > 150 * nplayers )
    {
//        sendf(-1, 1, "ri2", SV_SERVERMODE, sendservermode(false) | AT_SHUFFLE);
        shuffleteams();
        return true;
    }

    float diffscore = tscore[h] - tscore[l];

    int besth = 0, hid = -1;
    int bestdiff = 0, bestpair[2] = {-1, -1};
    if ( tsize[h] - tsize[l] > 0 ) // the h team has more players, so we will force only one player
    {
        loopv(clients) if( clients[i]->type!=ST_EMPTY )
        {
            client *c = clients[i]; // loop for h
            // client from the h team, not forced in this game, and without the flag
            if( c->isauthed && c->team == h && !c->state.forced && clienthasflag(i) < 0 )
            {
                // do not exchange in the way that weaker team becomes the stronger or the change is less than 20% effective
                if ( 2 * c->eff_score <= diffscore && 10 * c->eff_score >= diffscore && c->eff_score > besth )
                {
                    besth = c->eff_score;
                    hid = i;
                }
            }
        }
        if ( hid >= 0 )
        {
            updateclientteam(hid, l, ftr);
            clients[hid]->at3_lastforce = gamemillis;
            clients[hid]->state.forced = true;
            return true;
        }
    } else { // the h score team has less or the same player number, so, lets exchange
        loopv(clients) if(clients[i]->type!=ST_EMPTY)
        {
            client *c = clients[i]; // loop for h
            if( c->isauthed && c->team == h && !c->state.forced && clienthasflag(i) < 0 )
            {
                loopvj(clients) if(clients[j]->type!=ST_EMPTY && j != i )
                {
                    client *cj = clients[j]; // loop for l
                    if( cj->isauthed && cj->team == l && !cj->state.forced && clienthasflag(j) < 0 )
                    {
                        int pairdiff = 2 * (c->eff_score - cj->eff_score);
                        if ( pairdiff <= diffscore && 5 * pairdiff >= diffscore && pairdiff > bestdiff )
                        {
                            bestdiff = pairdiff;
                            bestpair[h] = i;
                            bestpair[l] = j;
                        }
                    }
                }
            }
        }
        if ( bestpair[h] >= 0 && bestpair[l] >= 0 )
        {
            updateclientteam(bestpair[h], l, ftr);
            updateclientteam(bestpair[l], h, ftr);
            clients[bestpair[h]]->at3_lastforce = clients[bestpair[l]]->at3_lastforce = gamemillis;
            clients[bestpair[h]]->state.forced = clients[bestpair[l]]->state.forced = true;
            return true;
        }
    }
    return false;
}

static int lastbalance = 0, waitbalance = 2 * 60 * 1000;

static bool refillteams(bool now, int ftr)  // force only minimal amounts of players
{
    if(mastermode == MM_MATCH) return false;
    static int lasttime_eventeams = 0;
    int teamsize[2] = {0, 0}, teamscore[2] = {0, 0}, moveable[2] = {0, 0};
    bool switched = false;

    calcscores();
    loopv(clients) if(clients[i]->type!=ST_EMPTY)     // playerlist stocktaking
    {
        client *c = clients[i];
        c->at3_dontmove = true;
        if(c->isauthed)
        {
            if(team_isactive(c->team)) // only active players count
            {
                teamsize[c->team]++;
                teamscore[c->team] += c->at3_score;
                if(clienthasflag(i) < 0)
                {
                    c->at3_dontmove = false;
                    moveable[c->team]++;
                }
            }
        }
    }
    int bigteam = teamsize[1] > teamsize[0];
    int allplayers = teamsize[0] + teamsize[1];
    int diffnum = teamsize[bigteam] - teamsize[!bigteam];
    int diffscore = teamscore[bigteam] - teamscore[!bigteam];
    if(lasttime_eventeams > gamemillis) lasttime_eventeams = 0;
    if(diffnum > 1)
    {
        if(now || gamemillis - lasttime_eventeams > 8000 + allplayers * 1000 || diffnum > 2 + allplayers / 10)
        {
            // time to even out teams
            loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->team != bigteam) clients[i]->at3_dontmove = true;  // dont move small team players
            while(diffnum > 1 && moveable[bigteam] > 0)
            {
                // pick best fitting cn
                int pick = -1;
                int bestfit = 1000000000;
                int targetscore = diffscore / (diffnum & ~1);
                loopv(clients) if(clients[i]->type!=ST_EMPTY && !clients[i]->at3_dontmove) // try all still movable players
                {
                    int fit = targetscore - clients[i]->at3_score;
                    if(fit < 0 ) fit = -(fit * 15) / 10;       // avoid too good players
                    int forcedelay = clients[i]->at3_lastforce ? (1000 - (gamemillis - clients[i]->at3_lastforce) / (5 * 60)) : 0;
                    if(forcedelay > 0) fit += (fit * forcedelay) / 600;   // avoid lately forced players
                    if(fit < bestfit + fit * rnd(100) / 400)   // search 'almost' best fit
                    {
                        bestfit = fit;
                        pick = i;
                    }
                }
                if(pick < 0) break; // should really never happen
                // move picked player
                clients[pick]->at3_dontmove = true;
                moveable[bigteam]--;
                if(updateclientteam(pick, !bigteam, ftr))
                {
                    diffnum -= 2;
                    diffscore -= 2 * clients[pick]->at3_score;
                    clients[pick]->at3_lastforce = gamemillis;  // try not to force this player again for the next 5 minutes
                    switched = true;
                }
            }
        }
    }
    if(diffnum < 2)
    {
        if ( ( gamemillis - lastbalance ) > waitbalance && ( gamelimit - gamemillis ) > 4*60*1000 )
        {
            if ( balanceteams (ftr) )
            {
                waitbalance = 2 * 60 * 1000 + gamemillis / 3;
                switched = true;
            }
            else waitbalance = 20 * 1000;
            lastbalance = gamemillis;
        }
        else if ( lastbalance > gamemillis )
        {
            lastbalance = 0;
            waitbalance = 2 * 60 * 1000;
        }
        lasttime_eventeams = gamemillis;
    }
    return switched;
}

static void resetserver(const char *newname, int newmode, int newtime)
{
    if(m_demo) enddemoplayback();
    else enddemorecord();

    smode = newmode;
    copystring(smapname, newname);

    minremain = newtime > 0 ? newtime : defaultgamelimit(newmode);
    gamemillis = 0;
    gamelimit = minremain*60000;
    arenaround = arenaroundstartmillis = 0;
    memset(&smapstats, 0, sizeof(smapstats));

    interm = nextsendscore = 0;
    lastfillup = servmillis;
    sents.shrink(0);
    if(mastermode == MM_PRIVATE)
    {
        loopv(savedscores) savedscores[i].valid = false;
    }
    else savedscores.shrink(0);
    ctfreset();

    nextmapname[0] = '\0';
    forceintermission = false;
}

static void startdemoplayback(const char *newname)
{
    if(isdedicated) return;
    resetserver(newname, GMODE_DEMO, -1);
    setupdemoplayback();
}

static void startgame(const char *newname, int newmode, int newtime, bool notify)
{
    if(!newname || !*newname || (newmode == GMODE_DEMO && isdedicated)) fatal("startgame() abused");
    if(newmode == GMODE_DEMO)
    {
        startdemoplayback(newname);
    }
    else
    {
        bool lastteammode = m_teammode;
        resetserver(newname, newmode, newtime);   // beware: may clear *newname

        if(custom_servdesc && findcnbyaddress(&servdesc_caller) < 0)
        {
            updatesdesc(NULL);
            if(notify)
            {
                sendservmsg("server description reset to default");
                logline(ACLOG_INFO, "server description reset to '%s'", servdesc_current);
            }
        }

        int maploc = MAP_VOID;
        mapstats *ms = getservermapstats(smapname, isdedicated, &maploc);
        mapbuffer.clear();
        if(isdedicated && distributablemap(maploc)) mapbuffer.load();
        if(ms)
        {
            smapstats = *ms;
            loopi(2)
            {
                sflaginfo &f = sflaginfos[i];
                if(smapstats.flags[i] == 1)    // don't check flag positions, if there is more than one flag per team
                {
                    short *fe = smapstats.entposs + smapstats.flagents[i] * 3;
                    f.x = *fe;
                    fe++;
                    f.y = *fe;
                }
                else f.x = f.y = -1;
            }
            if (smapstats.flags[0] == 1 && smapstats.flags[1] == 1)
            {
                sflaginfo &f0 = sflaginfos[0], &f1 = sflaginfos[1];
                FlagFlag = pow2(f0.x - f1.x) + pow2(f0.y - f1.y);
                coverdist = FlagFlag > 6 * COVERDIST ? COVERDIST : FlagFlag / 6;
            }
            entity e;
            loopi(smapstats.hdr.numents)
            {
                e.type = smapstats.enttypes[i];
                e.transformtype(smode);
                server_entity se = { e.type, false, false, false, 0, smapstats.entposs[i * 3], smapstats.entposs[i * 3 + 1]};
                sents.add(se);
                if(e.fitsmode(smode)) sents[i].spawned = sents[i].legalpickup = true;
            }
            mapbuffer.setrevision();
            logline(ACLOG_INFO, "Map height density information for %s: H = %.2f V = %d, A = %d and MA = %d", smapname, Mheight, Mvolume, Marea, Mopen);
            items_blocked = false;
        }
        else if(isdedicated) sendservmsg("\f3server error: map not found - please start another map or send this map to the server");
        if(notify)
        {
            // change map
            sendf(-1, 1, "risiii", SV_MAPCHANGE, smapname, smode, mapbuffer.available(), mapbuffer.revision);
            if(smode>1 || (smode==0 && numnonlocalclients()>0)) sendf(-1, 1, "ri3", SV_TIMEUP, gamemillis, gamelimit);
        }
        packetbuf q(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        send_item_list(q); // always send the item list when a game starts
        sendpacket(-1, 1, q.finalize());
        defformatstring(gsmsg)("Game start: %s on %s, %d players, %d minutes, mastermode %d, ", modestr(smode), smapname, numclients(), minremain, mastermode);
        if(mastermode == MM_MATCH) concatformatstring(gsmsg, "teamsize %d, ", matchteamsize);
        if(ms) concatformatstring(gsmsg, "(map rev %d/%d, %s, 'getmap' %sprepared)", smapstats.hdr.maprevision, smapstats.cgzsize, maplocstr[maploc], mapbuffer.available() ? "" : "not ");
        else concatformatstring(gsmsg, "error: failed to preload map (map: %s)", maplocstr[maploc]);
        logline(ACLOG_INFO, "\n%s", gsmsg);
        if(m_arena) distributespawns();
        if(notify)
        {
            // shuffle if previous mode wasn't a team-mode
            if(m_teammode)
            {
                if(!lastteammode)
                    shuffleteams(FTR_INFO);
                else if(autoteam)
                    refillteams(true, FTR_INFO);
            }
            // prepare spawns; players will spawn, once they've loaded the correct map
            loopv(clients) if(clients[i]->type!=ST_EMPTY)
            {
                client *c = clients[i];
                c->mapchange();
                forcedeath(c);
            }
        }
        if(numnonlocalclients() > 0) setupdemorecord();
        if(notify && m_ktf) sendflaginfo();
        if(notify) senddisconnectedscores(-1);
    }
}

struct gbaninfo
{
    enet_uint32 ip, mask;
};

static vector<gbaninfo> gbans;

static void cleargbans()
{
    gbans.shrink(0);
}

static bool checkgban(uint ip)
{
    loopv(gbans) if((ip & gbans[i].mask) == gbans[i].ip) return true;
    return false;
}

static void addgban(const char *name)
{
    union { uchar b[sizeof(enet_uint32)]; enet_uint32 i; } ip, mask;
    ip.i = 0;
    mask.i = 0;
    loopi(4)
    {
        char *end = NULL;
        int n = strtol(name, &end, 10);
        if(!end) break;
        if(end > name) { ip.b[i] = n; mask.b[i] = 0xFF; }
        name = end;
        while(*name && *name++ != '.');
    }
    gbaninfo &ban = gbans.add();
    ban.ip = ip.i;
    ban.mask = mask.i;

    loopvrev(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP) continue;
        if(checkgban(c.peer->address.host)) disconnect_client(c.clientnum, DISC_BANREFUSE);
    }
}

static int getbantype(int cn)
{
    if(!valid_client(cn)) return BAN_NONE;
    client &c = *clients[cn];
    if(c.type==ST_LOCAL) return BAN_NONE;
    if(checkgban(c.peer->address.host)) return BAN_MASTER;
    if(ipblacklist.check(c.peer->address.host)) return BAN_BLACKLIST;
    loopv(bans)
    {
        ban &b = bans[i];
        if(b.millis < servmillis) { bans.remove(i--); }
        if(b.address.host == c.peer->address.host) return b.type;
    }
    return BAN_NONE;
}

static int serveroperator()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->role > CR_DEFAULT) return i;
    return -1;
}

static void sendserveropinfo(int receiver)
{
    int op = serveroperator();
    sendf(receiver, 1, "riii", SV_SERVOPINFO, op, op >= 0 ? clients[op]->role : -1);
}

enum { EE_LOCAL_SERV = 1, EE_DED_SERV = 1<<1 }; // execution environment

static int roleconf(int key)
{ // current defaults: "fkbMASRCDEPtw"
    if(strchr(scl.voteperm, tolower(key))) return CR_DEFAULT;
    if(strchr(scl.voteperm, toupper(key))) return CR_ADMIN;
    return (key) == tolower(key) ? CR_DEFAULT : CR_ADMIN;
}

struct serveraction
{
    int role; // required client role
    int area; // only on ded servers
    string desc;

    virtual void perform() = 0;
    virtual bool isvalid() { return true; }
    virtual bool isdisabled() { return false; }
    serveraction() : role(CR_DEFAULT), area(EE_DED_SERV) { desc[0] = '\0'; }
    virtual ~serveraction() { }
};

static void kick_abuser(int cn, int &cmillis, int &count, int limit)
{
    if ( cmillis + 30000 > servmillis ) count++;
    else {
        count -= count > 0 ? (servmillis - cmillis)/30000 : 0;
        if ( count <= 0 ) count = 1;
    }
    cmillis = servmillis;
    if( count >= limit ) disconnect_client(cn, DISC_ABUSE);
}

struct mapaction : serveraction
{
    char *map;
    int mode, time;
    bool mapok, queue;
    void perform()
    {
        if(queue)
        {
            nextgamemode = mode;
            copystring(nextmapname, map);
        }
        else if(isdedicated && numclients() > 2 && smode >= 0 && smode != 1 && ( gamemillis > gamelimit/4 || scl.demo_interm ))
        {
            forceintermission = true;
            nextgamemode = mode;
            copystring(nextmapname, map);
        }
        else
        {
            startgame(map, mode, time);
        }
    }
    bool isvalid() { return serveraction::isvalid() && mode != GMODE_DEMO && map[0] && mapok && !(isdedicated && !m_mp(mode)); }
    bool isdisabled() { return maprot.current() && !maprot.current()->vote; }
    mapaction(char *map, int mode, int time, int caller, bool q) : map(map), mode(mode), time(time), queue(q)
    {
        if(isdedicated)
        {
            bool notify = valid_client(caller);
            int maploc = MAP_NOTFOUND;
            mapstats *ms = map[0] ? getservermapstats(map, false, &maploc) : NULL;
            bool validname = validmapname(map);
            mapok = (ms != NULL) && validname && ( (mode != GMODE_COOPEDIT && mapisok(ms)) || (mode == GMODE_COOPEDIT && !readonlymap(maploc)) );
            if(!mapok)
            {
                if(notify)
                {
                    if(!validname)
                        sendservmsg("invalid map name", caller);
                    else
                    {
                        sendservmsg(ms ?
                            ( mode == GMODE_COOPEDIT ? "this map cannot be coopedited in this server" : "sorry, but this map does not satisfy some quality requisites to be played in MultiPlayer Mode" ) :
                            "the server does not have this map",
                            caller);
                    }
                }
            }
            else
            { // check, if map supports mode
                if(mode == GMODE_COOPEDIT && !strchr(scl.voteperm, 'e')) role = CR_ADMIN;
                bool romap = mode == GMODE_COOPEDIT && readonlymap(maploc);
                int smode = mode;  // 'borrow' the mode macros by replacing a global by a local var
                bool spawns = mode == GMODE_COOPEDIT || (m_teammode && !m_ktf ? ms->hasteamspawns : ms->hasffaspawns);
                bool flags = mode != GMODE_COOPEDIT && m_flags && !m_htf ? ms->hasflags : true;
                if(!spawns || !flags || romap)
                { // unsupported mode
                    if(strchr(scl.voteperm, 'P')) role = CR_ADMIN;
                    else if(!strchr(scl.voteperm, 'p')) mapok = false; // default: no one can vote for unsupported mode/map combinations
                    defformatstring(msg)("\f3map \"%s\" does not support \"%s\": ", behindpath(map), modestr(mode, false));
                    if(romap) concatstring(msg, "map is readonly");
                    else
                    {
                        if(!spawns) concatstring(msg, "player spawns");
                        if(!spawns && !flags) concatstring(msg, " and ");
                        if(!flags) concatstring(msg, "flag bases");
                        concatstring(msg, " missing");
                    }
                    if(notify) sendservmsg(msg, caller);
                    logline(ACLOG_INFO, "%s", msg);
                }
            }
            loopv(scl.adminonlymaps)
            {
                const char *s = scl.adminonlymaps[i], *h = strchr(s, '#'), *m = behindpath(map);
                size_t sl = strlen(s);
                if(h)
                {
                    if(h != s)
                    {
                        sl = h - s;
                        if(mode != atoi(h + 1)) continue;
                    }
                    else
                    {
                        if(mode == atoi(h+1))
                        {
                            role = CR_ADMIN;
                            break;
                        }
                    }
                }
                if(sl == strlen(m) && !strncmp(m, scl.adminonlymaps[i], sl)) role = CR_ADMIN;
            }
        }
        else
        {
            int mp = findmappath(map);
            mapok = mp == MAP_LOCAL || mp == MAP_OFFICIAL;    // clients can only load from packages/maps and packages/maps/official
#ifndef STANDALONE
            if(!mapok) conoutf("\f3map '%s' not found", map);
#endif
        }
        area |= EE_LOCAL_SERV; // local too
        formatstring(desc)("load map '%s' in mode '%s'", map, modestr(mode));
        if(q) concatstring(desc, " (in the next game)");
    }
    ~mapaction() { DELETEA(map); }
};

struct demoplayaction : serveraction
{
    char *demofilename;
    void perform() { startdemoplayback(demofilename); }
    demoplayaction(char *demofilename) : demofilename(demofilename)
    {
        area = EE_LOCAL_SERV; // only local
    }

    ~demoplayaction() { DELETEA(demofilename); }
};

struct playeraction : serveraction
{
    int cn;
    ENetAddress address;
    void disconnect(int reason)
    {
        int i = findcnbyaddress(&address);
        if(i >= 0) disconnect_client(i, reason);
    }
    virtual bool isvalid() { return valid_client(cn) && clients[cn]->role != CR_ADMIN; } // actions can't be done on admins
    playeraction(int cn) : cn(cn)
    {
        if(isvalid()) address = clients[cn]->peer->address;
    };
};

struct forceteamaction : playeraction
{
    int team;
    void perform() { updateclientteam(cn, team, FTR_SILENTFORCE); }
    virtual bool isvalid() { return valid_client(cn) && team_isvalid(team) && team != clients[cn]->team; }
    forceteamaction(int cn, int caller, int team) : playeraction(cn), team(team)
    {
        if(cn != caller) role = roleconf('f');
        if(isvalid() && !(clients[cn]->state.forced && clients[caller]->role != CR_ADMIN)) formatstring(desc)("force player %s to team %s", clients[cn]->name, teamnames[team]);
    }
};

struct giveadminaction : playeraction
{
    void perform() { changeclientrole(cn, CR_ADMIN, NULL, true); }
    giveadminaction(int cn) : playeraction(cn)
    {
        role = CR_ADMIN;
//        role = roleconf('G');
    }
};

struct kickaction : playeraction
{
    bool wasvalid;
    void perform()  { disconnect(DISC_MKICK); }
    virtual bool isvalid() { return wasvalid || playeraction::isvalid(); }
    kickaction(int cn, char *reason) : playeraction(cn)
    {
        wasvalid = false;
        role = roleconf('k');
        if(isvalid())
        {
            wasvalid = true;
            formatstring(desc)("kick player %s, reason: %s", clients[cn]->name, reason);
        }
    }
};

struct banaction : playeraction
{
    bool wasvalid;
    void perform()
    {
        int i = findcnbyaddress(&address);
        if(i >= 0) addban(clients[i], DISC_MBAN, BAN_VOTE);
    }
    virtual bool isvalid() { return wasvalid || playeraction::isvalid(); }
    banaction(int cn, char *reason) : playeraction(cn)
    {
        wasvalid = false;
        role = roleconf('b');
        if(isvalid())
        {
            wasvalid = true;
            formatstring(desc)("ban player %s, reason: %s", clients[cn]->name, reason);
        }
    }
};

struct removebansaction : serveraction
{
    void perform() { bans.shrink(0); }
    removebansaction()
    {
        role = roleconf('b');
        copystring(desc, "remove all bans");
    }
};

struct mastermodeaction : serveraction
{
    int mode;
    void perform() { changemastermode(mode); }
    bool isvalid() { return mode >= 0 && mode < MM_NUM; }
    mastermodeaction(int mode) : mode(mode)
    {
        role = roleconf('M');
        if(isvalid()) formatstring(desc)("change mastermode to '%s'", mmfullname(mode));
    }
};

struct enableaction : serveraction
{
    bool enable;
    enableaction(bool enable) : enable(enable) {}
};

struct autoteamaction : enableaction
{
    void perform()
    {
        autoteam = enable;
        sendservermode();
        if(m_teammode && enable) refillteams(true);
    }
    autoteamaction(bool enable) : enableaction(enable)
    {
        role = roleconf('A');
        if(isvalid()) formatstring(desc)("%s autoteam", enable ? "enable" : "disable");
    }
};

struct shuffleteamaction : serveraction
{
    void perform()
    {
        sendf(-1, 1, "ri2", SV_SERVERMODE, sendservermode(false) | AT_SHUFFLE);
        shuffleteams();
    }
    bool isvalid() { return serveraction::isvalid() && m_teammode; }
    shuffleteamaction()
    {
        role = roleconf('S');
        if(isvalid()) copystring(desc, "shuffle teams");
    }
};

struct recorddemoaction : enableaction            // TODO: remove completely
{
    void perform() { }
    bool isvalid() { return serveraction::isvalid(); }
    recorddemoaction(bool enable) : enableaction(enable)
    {
        role = roleconf('R');
        if(isvalid()) formatstring(desc)("%s demorecord", enable ? "enable" : "disable");
    }
};

struct cleardemosaction : serveraction
{
    int demo;
    void perform() { cleardemos(demo); }
    cleardemosaction(int demo) : demo(demo)
    {
        role = roleconf('C');
        if(isvalid()) formatstring(desc)("clear demo %d", demo);
    }
};

struct serverdescaction : serveraction
{
    char *sdesc;
    int cn;
    ENetAddress address;
    void perform() { updatesdesc(sdesc, &address); }
    bool isvalid() { return serveraction::isvalid() && updatedescallowed() && valid_client(cn); }
    serverdescaction(char *sdesc, int cn) : sdesc(sdesc), cn(cn)
    {
        role = roleconf('D');
        formatstring(desc)("set server description to '%s'", sdesc);
        if(isvalid()) address = clients[cn]->peer->address;
    }
    ~serverdescaction() { DELETEA(sdesc); }
};


struct voteinfo
{
    int boot;
    int owner, callmillis, result, num1, num2, type;
    char text[MAXTRANS];
    serveraction *action;
    bool gonext;
    enet_uint32 host;

    voteinfo() : boot(0), owner(0), callmillis(0), result(VOTE_NEUTRAL), action(NULL), gonext(false), host(0) {}
    ~voteinfo() { DELETEP(action); }

    void end(int result)
    {
        if(action && !action->isvalid()) result = VOTE_NO; // don't perform() invalid votes
        sendf(-1, 1, "ri2", SV_VOTERESULT, result);
        this->result = result;
        if(result == VOTE_YES)
        {
            if(valid_client(owner)) clients[owner]->lastvotecall = 0;
            if(action) action->perform();
        }
        loopv(clients) clients[i]->vote = VOTE_NEUTRAL;
    }

    bool isvalid() { return valid_client(owner) && action != NULL && action->isvalid(); }
    bool isalive() { return servmillis - callmillis < 30*1000; }

    void evaluate(bool forceend = false)
    {
        if(result!=VOTE_NEUTRAL) return; // block double action
        if(action && !action->isvalid()) end(VOTE_NO);
        int stats[VOTE_NUM] = {0};
        int adminvote = VOTE_NEUTRAL;
        loopv(clients)
            if(clients[i]->type!=ST_EMPTY /*&& clients[i]->connectmillis < callmillis*/) // new connected people will vote now
            {
                stats[clients[i]->vote]++;
                if(clients[i]->role==CR_ADMIN) adminvote = clients[i]->vote;
            };

        bool admin = clients[owner]->role==CR_ADMIN || (!isdedicated && clients[owner]->type==ST_LOCAL);
        int total = stats[VOTE_NO]+stats[VOTE_YES]+stats[VOTE_NEUTRAL];
        const float requiredcount = 0.51f;
        bool min_time = servmillis - callmillis > 10*1000;
#define yes_condition ((min_time && stats[VOTE_YES] - stats[VOTE_NO] > 0.34f*total && totalclients > 4) || stats[VOTE_YES] > requiredcount*total)
#define no_condition (forceend || !valid_client(owner) || stats[VOTE_NO] >= stats[VOTE_YES]+stats[VOTE_NEUTRAL] || adminvote == VOTE_NO)
#define boot_condition (!boot || (boot && valid_client(num1) && clients[num1]->peer->address.host == host))
        if( (yes_condition || admin || adminvote == VOTE_YES) && boot_condition ) end(VOTE_YES);
        else if( no_condition || (min_time && !boot_condition)) end(VOTE_NO);
        else return;
#undef boot_condition
#undef no_condition
#undef yes_condition
    }
};

static voteinfo *server_curvote = NULL;

static bool svote(int sender, int vote, ENetPacket *msg) // true if the vote was placed successfully
{
    if(!server_curvote || !valid_client(sender) || vote < VOTE_YES || vote > VOTE_NO) return false;
    if(clients[sender]->vote != VOTE_NEUTRAL)
    {
        sendf(sender, 1, "ri2", SV_CALLVOTEERR, VOTEE_MUL);
        return false;
    }
    else
    {
        sendpacket(-1, 1, msg, sender);

        clients[sender]->vote = vote;
        logline(ACLOG_DEBUG,"[%s] client %s voted %s", clients[sender]->hostname, clients[sender]->name, vote == VOTE_NO ? "no" : "yes");
        server_curvote->evaluate();
        return true;
    }
}

static void scallvotesuc(voteinfo *v)
{
    if(!v->isvalid()) return;
    DELETEP(server_curvote);
    server_curvote = v;
    clients[v->owner]->lastvotecall = servmillis;
    clients[v->owner]->nvotes--; // successful votes do not count as abuse
    sendf(v->owner, 1, "ri", SV_CALLVOTESUC);
    logline(ACLOG_INFO, "[%s] client %s called a vote: %s", clients[v->owner]->hostname, clients[v->owner]->name, v->action && v->action->desc ? v->action->desc : "[unknown]");
}

static void scallvoteerr(voteinfo *v, int error)
{
    if(!valid_client(v->owner)) return;
    sendf(v->owner, 1, "ri2", SV_CALLVOTEERR, error);
    logline(ACLOG_INFO, "[%s] client %s failed to call a vote: %s (%s)", clients[v->owner]->hostname, clients[v->owner]->name, v->action && v->action->desc ? v->action->desc : "[unknown]", voteerrorstr(error));
}

static bool map_queued = false;
static void callvotepacket (int, voteinfo *);

static bool scallvote(voteinfo *v, ENetPacket *msg) // true if a regular vote was called
{
    if (!v) return false;
    int area = isdedicated ? EE_DED_SERV : EE_LOCAL_SERV;
    int error = -1;
    client *c = clients[v->owner], *b = ( v->boot && valid_client(cn2boot) ? clients[cn2boot] : NULL );
    v->host = v->boot && b ? b->peer->address.host : 0;

    int time = servmillis - c->lastvotecall;
    if ( c->nvotes > 0 && time > 4*60*1000 ) c->nvotes -= time/(4*60*1000);
    if ( c->nvotes < 0 || c->role == CR_ADMIN ) c->nvotes = 0;
    c->nvotes++;

    if( !v || !v->isvalid() || (v->boot && (!b || cn2boot == v->owner) ) ) error = VOTEE_INVALID;
    else if( v->action->role > c->role ) error = VOTEE_PERMISSION;
    else if( !(area & v->action->area) ) error = VOTEE_AREA;
    else if( server_curvote && server_curvote->result==VOTE_NEUTRAL ) error = VOTEE_CUR;
    else if( v->type == SA_MAP && v->num1 >= GMODE_NUM && map_queued ) error = VOTEE_NEXT;
    else if( c->role == CR_DEFAULT && v->action->isdisabled() ) error = VOTEE_DISABLED;
    else if( (c->lastvotecall && servmillis - c->lastvotecall < 60*1000 && c->role != CR_ADMIN && numclients()>1) || c->nvotes > 3 ) error = VOTEE_MAX;
    else if( ( ( v->boot == 1 && c->role < roleconf('w') ) || ( v->boot == 2 && c->role < roleconf('X') ) )
                  && !is_lagging(b) && !b->mute && b->spamcount < 2 )
    {
        /** not same team, with low ratio, not lagging, and not spamming... so, why to kick? */
        if ( !isteam(c->team, b->team) && b->state.frags < ( b->state.deaths > 0 ? b->state.deaths : 1 ) * 3 ) error = VOTEE_WEAK;
        /** same team, with low tk, not lagging, and not spamming... so, why to kick? */
        else if ( isteam(c->team, b->team) && b->state.teamkills < c->state.teamkills ) error = VOTEE_WEAK;
    }

    if(error>=0)
    {
        scallvoteerr(v, error);
        return false;
    }
    else
    {
        if ( v->type == SA_MAP && v->num1 >= GMODE_NUM ) map_queued = true;
        if (!v->gonext) sendpacket(-1, 1, msg, v->owner); // FIXME in fact, all votes should go to the server, registered, and then go back to the clients
        else callvotepacket (-1, v);                      // also, no vote should exclude the caller... these would provide many code advantages/facilities
        scallvotesuc(v);                                  // but we cannot change the vote system now for compatibility issues... so, TODO
        return true;
    }
}

static void callvotepacket (int cn, voteinfo *v = server_curvote)
{ // FIXME, it would be far smart if the msg buffer from SV_CALLVOTE was simply saved
    int n_yes = 0, n_no = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if ( clients[i]->vote == VOTE_YES ) n_yes++;
        else if ( clients[i]->vote == VOTE_NO ) n_no++;
    }

    packetbuf q(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(q, SV_CALLVOTE);
    putint(q, -1);
    putint(q, v->owner);
    putint(q, n_yes);
    putint(q, n_no);
    putint(q, v->type);
    switch(v->type)
    {
        case SA_KICK:
        case SA_BAN:
            putint(q, v->num1);
            sendstring(v->text, q);
            break;
        case SA_MAP:
            sendstring(v->text, q);
            putint(q, v->num1);
            putint(q, v->num2);
            break;
        case SA_SERVERDESC:
            sendstring(v->text, q);
            break;
        case SA_STOPDEMO:
            // compatibility
            break;
        case SA_REMBANS:
        case SA_SHUFFLETEAMS:
            break;
        case SA_FORCETEAM:
            putint(q, v->num1);
            putint(q, v->num2);
            break;
        default:
            putint(q, v->num1);
            break;
    }
    sendpacket(cn, 1, q.finalize());
}

// TODO: use AUTH code
static void changeclientrole(int client, int role, char *pwd, bool force)
{
    pwddetail pd;
    if(!isdedicated || !valid_client(client)) return;
    pd.line = -1;
    if(force || role == CR_DEFAULT || (role == CR_ADMIN && pwd && pwd[0] && passwords.check(clients[client]->name, pwd, clients[client]->salt, &pd) && !pd.denyadmin))
    {
        if(role == clients[client]->role) return;
        if(role > CR_DEFAULT)
        {
            loopv(clients) clients[i]->role = CR_DEFAULT;
        }
        clients[client]->role = role;
        sendserveropinfo(-1);
        if(pd.line > -1)
            logline(ACLOG_INFO,"[%s] player %s used admin password in line %d", clients[client]->hostname, clients[client]->name[0] ? clients[client]->name : "[unnamed]", pd.line);
        logline(ACLOG_INFO,"[%s] set role of player %s to %s", clients[client]->hostname, clients[client]->name[0] ? clients[client]->name : "[unnamed]", role == CR_ADMIN ? "admin" : "normal player"); // flowtron : connecting players haven't got a name yet (connectadmin)
        if(role > CR_DEFAULT) sendiplist(client);
    }
    else if(pwd && pwd[0]) disconnect_client(client, DISC_SOPLOGINFAIL); // avoid brute-force
    if(server_curvote) server_curvote->evaluate();
}

static void senddisconnectedscores(int cn)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_DISCSCORES);
    if(mastermode == MM_MATCH)
    {
        loopv(savedscores)
        {
            savedscore &sc = savedscores[i];
            if(sc.valid)
            {
                putint(p, sc.team);
                sendstring(sc.name, p);
                putint(p, sc.flagscore);
                putint(p, sc.frags);
                putint(p, sc.deaths);
                putint(p, sc.points);
            }
        }
    }
    putint(p, -1);
    sendpacket(cn, 1, p.finalize());
}

const char *disc_reason(int reason)
{
    static const char *disc_reasons[] = { "normal", "error - end of packet", "error - client num", "vote-kicked from the server", "vote-banned from the server", "error - tag type", "connection refused - you have been banned from this server", "incorrect password", "unsuccessful administrator login", "the server is FULL - try again later", "servers mastermode is \"private\" - wait until the servers mastermode is \"open\"", "auto-kick - your score dropped below the servers threshold", "auto-ban - your score dropped below the servers threshold", "duplicate connection", "inappropriate nickname", "error - packet flood", "auto-kick - excess spam detected", "auto-kick - inactivity detected", "auto-kick - team killing detected", "auto-kick - abnormal client behavior detected" };
    return reason >= 0 && (size_t)reason < sizeof(disc_reasons)/sizeof(disc_reasons[0]) ? disc_reasons[reason] : "unknown";
}

static void disconnect_client(int n, int reason)
{
    if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
    sdropflag(n);
    client &c = *clients[n];
    c.state.lastdisc = servmillis;
    const char *scoresaved = "";
    if(c.haswelcome)
    {
        savedscore *sc = findscore(c, true);
        if(sc)
        {
            sc->save(c.state, c.team);
            scoresaved = ", score saved";
        }
    }
    int sp = (servmillis - c.connectmillis) / 1000;
    if(reason>=0) logline(ACLOG_INFO, "[%s] disconnecting client %s (%s) cn %d, %d seconds played%s", c.hostname, c.name, disc_reason(reason), n, sp, scoresaved);
    else logline(ACLOG_INFO, "[%s] disconnected client %s cn %d, %d seconds played%s", c.hostname, c.name, n, sp, scoresaved);
    totalclients--;
    c.peer->data = (void *)-1;
    if(reason>=0) enet_peer_disconnect(c.peer, reason);
    clients[n]->zap();
    sendf(-1, 1, "rii", SV_CDIS, n);
    if(server_curvote) server_curvote->evaluate();
    if(*scoresaved && mastermode == MM_MATCH) senddisconnectedscores(-1);
}

// for AUTH: WIP

static client *findauth(uint id)
{
    loopv(clients) if(clients[i]->authreq == id) return clients[i];
    return NULL;
}

static void authfailed(uint id)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
}

static void authsucceeded(uint id)
{
    client *cl = findauth(id);
    if(!cl) return;
    cl->authreq = 0;
    logline(ACLOG_INFO, "player authenticated: %s", cl->name);
    defformatstring(auth4u)("player authenticated: %s", cl->name);
    sendf(-1, 1, "ris", SV_SERVMSG, auth4u);
    //setmaster(cl, true, "", ci->authname);//TODO? compare to sauerbraten
}

static void authchallenged(uint id, const char *val)
{
    client *cl = findauth(id);
    if(!cl) return;
    sendf(cl->clientnum, 1, "risis", SV_AUTHCHAL, "", id, val);
}

static uint nextauthreq = 0;

static void tryauth(client *cl, const char *user)
{
    extern bool requestmasterf(const char *fmt, ...);
    if(!nextauthreq) nextauthreq = 1;
    cl->authreq = nextauthreq++;
    filtertext(cl->authname, user, FTXT__AUTH, 100);
    if(!requestmasterf("reqauth %u %s\n", cl->authreq, cl->authname))
    {
        cl->authreq = 0;
        sendf(cl->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
    }
}

static void answerchallenge(client *cl, uint id, char *val)
{
    if(cl->authreq != id) return;
    extern bool requestmasterf(const char *fmt, ...);
    for(char *s = val; *s; s++)
    {
        if(!isxdigit(*s)) { *s = '\0'; break; }
    }
    if(!requestmasterf("confauth %u %s\n", id, val))
    {
        cl->authreq = 0;
        sendf(cl->clientnum, 1, "ris", SV_SERVMSG, "not connected to authentication server");
    }
}

// :for AUTH

static void sendiplist(int receiver, int cn)
{
    if(!valid_client(receiver)) return;
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putint(p, SV_IPLIST);
    loopv(clients) if(valid_client(i) && clients[i]->type == ST_TCPIP && i != receiver
        && (clients[i]->clientnum == cn || cn == -1))
    {
        putint(p, i);
        putint(p, isbigendian() ? endianswap(clients[i]->peer->address.host) : clients[i]->peer->address.host);
    }
    putint(p, -1);
    sendpacket(receiver, 1, p.finalize());
}

static void sendresume(client &c, bool broadcast)
{
    sendf(broadcast ? -1 : c.clientnum, 1, "ri3i9ivvi", SV_RESUME,
            c.clientnum,
            c.state.state,
            c.state.lifesequence,
            c.state.primary,
            c.state.gunselect,
            c.state.flagscore,
            c.state.frags,
            c.state.deaths,
            c.state.health,
            c.state.armour,
            c.state.points,
            c.state.teamkills,
            NUMGUNS, c.state.ammo,
            NUMGUNS, c.state.mag,
            -1);
}

static bool restorescore(client &c)
{
    //if(ci->local) return false;
    savedscore *sc = findscore(c, false);
    if(sc && sc->valid)
    {
        sc->restore(c.state);
        sc->valid = false;
        if ( c.connectmillis - c.state.lastdisc < 5000 ) c.state.reconnections++;
        else if ( c.state.reconnections ) c.state.reconnections--;
        return true;
    }
    return false;
}

static void sendservinfo(client &c)
{
    sendf(c.clientnum, 1, "ri5", SV_SERVINFO, c.clientnum, isdedicated ? SERVER_PROTOCOL_VERSION : PROTOCOL_VERSION, c.salt, scl.serverpassword[0] ? 1 : 0);
}

static void putinitclient(client &c, packetbuf &p)
{
    putint(p, SV_INITCLIENT);
    putint(p, c.clientnum);
    sendstring(c.name, p);
    putint(p, c.skin[TEAM_CLA]);
    putint(p, c.skin[TEAM_RVSF]);
    putint(p, c.team);
    enet_uint32 ip = 0;
    if(c.type == ST_TCPIP) ip = c.peer->address.host & 0xFFFFFF;
    putint(p, isbigendian() ? endianswap(ip) : ip);
}

static void sendinitclient(client &c)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    putinitclient(c, p);
    sendpacket(-1, 1, p.finalize(), c.clientnum);
}

static void welcomeinitclient(packetbuf &p, int exclude = -1)
{
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP || !c.isauthed || c.clientnum == exclude) continue;
        putinitclient(c, p);
    }
}

static void welcomepacket(packetbuf &p, int n)
{
    if(!smapname[0]) maprot.next(false);

    client *c = valid_client(n) ? clients[n] : NULL;
    int numcl = numclients();

    putint(p, SV_WELCOME);
    putint(p, smapname[0] && !m_demo ? numcl : -1);
    if(smapname[0] && !m_demo)
    {
        putint(p, SV_MAPCHANGE);
        sendstring(smapname, p);
        putint(p, smode);
        putint(p, mapbuffer.available());
        putint(p, mapbuffer.revision);
        if(smode>1 || (smode==0 && numnonlocalclients()>0))
        {
            putint(p, SV_TIMEUP);
            putint(p, (gamemillis>=gamelimit || forceintermission) ? gamelimit : gamemillis);
            putint(p, gamelimit);
            //putint(p, minremain*60);
        }
        send_item_list(p); // this includes the flags
    }
    savedscore *sc = NULL;
    if(c)
    {
        if(c->type == ST_TCPIP && serveroperator() != -1) sendserveropinfo(n);
        c->team = mastermode == MM_MATCH && sc ? team_tospec(sc->team) : TEAM_SPECT;
        putint(p, SV_SETTEAM);
        putint(p, n);
        putint(p, c->team | (FTR_INFO << 4));

        putint(p, SV_FORCEDEATH);
        putint(p, n);
        sendf(-1, 1, "ri2x", SV_FORCEDEATH, n, n);
    }
    if(!c || clients.length()>1)
    {
        putint(p, SV_RESUME);
        loopv(clients)
        {
            client &c = *clients[i];
            if(c.type!=ST_TCPIP || c.clientnum==n) continue;
            putint(p, c.clientnum);
            putint(p, c.state.state);
            putint(p, c.state.lifesequence);
            putint(p, c.state.primary);
            putint(p, c.state.gunselect);
            putint(p, c.state.flagscore);
            putint(p, c.state.frags);
            putint(p, c.state.deaths);
            putint(p, c.state.health);
            putint(p, c.state.armour);
            putint(p, c.state.points);
            putint(p, c.state.teamkills);
            loopi(NUMGUNS) putint(p, c.state.ammo[i]);
            loopi(NUMGUNS) putint(p, c.state.mag[i]);
        }
        putint(p, -1);
        welcomeinitclient(p, n);
    }
    putint(p, SV_SERVERMODE);
    putint(p, sendservermode(false));
    const char *motd = scl.motd[0] ? scl.motd : infofiles.getmotd(c ? c->lang : "");
    if(motd)
    {
        putint(p, SV_TEXT);
        sendstring(motd, p);
    }
}

static void sendwelcome(client *cl, int chan)
{
    packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    welcomepacket(p, cl->clientnum);
    sendpacket(cl->clientnum, chan, p.finalize());
    cl->haswelcome = true;
}

static void forcedeath(client *cl)
{
    sdropflag(cl->clientnum);
    cl->state.state = CS_DEAD;
    cl->state.respawn();
    sendf(-1, 1, "rii", SV_FORCEDEATH, cl->clientnum);
}

static int checktype(int type, client *cl)
{
    if(cl && cl->type==ST_LOCAL) return type;
    if(type < 0 || type >= SV_NUM) return -1;
    // server only messages
    static int servtypes[] = { SV_SERVINFO, SV_WELCOME, SV_INITCLIENT, SV_POSN, SV_CDIS, SV_GIBDIED, SV_DIED,
                        SV_GIBDAMAGE, SV_DAMAGE, SV_HITPUSH, SV_SHOTFX, SV_AUTHREQ, SV_AUTHCHAL,
                        SV_SPAWNSTATE, SV_SPAWNDENY, SV_FORCEDEATH, SV_RESUME,
                        SV_DISCSCORES, SV_TIMEUP, SV_ITEMACC, SV_MAPCHANGE, SV_ITEMSPAWN, SV_PONG,
                        SV_SERVMSG, SV_ITEMLIST, SV_FLAGINFO, SV_FLAGMSG, SV_FLAGCNT,
                        SV_ARENAWIN, SV_SERVOPINFO,
                        SV_CALLVOTESUC, SV_CALLVOTEERR, SV_VOTERESULT,
                        SV_SETTEAM, SV_TEAMDENY, SV_SERVERMODE, SV_IPLIST,
                        SV_SENDDEMOLIST, SV_SENDDEMO, SV_DEMOPLAYBACK,
                        SV_CLIENT, SV_HUDEXTRAS, SV_POINTS };
    // only allow edit messages in coop-edit mode
    static int edittypes[] = { SV_EDITENT, SV_EDITH, SV_EDITT, SV_EDITS, SV_EDITD, SV_EDITE, SV_NEWMAP };
    if(cl)
    {
        loopi(sizeof(servtypes)/sizeof(int)) if(type == servtypes[i]) return -1;
        loopi(sizeof(edittypes)/sizeof(int)) if(type == edittypes[i]) return smode==GMODE_COOPEDIT ? type : -1;
        if(++cl->overflow >= 200) return -2;
    }
    return type;
}

// server side processing of updates: does very little and most state is tracked client only
// could be extended to move more gameplay to server (at expense of lag)

static void process(ENetPacket *packet, int sender, int chan)
{
    ucharbuf p(packet->data, packet->dataLength);
    char text[MAXTRANS];
    client *cl = sender>=0 ? clients[sender] : NULL;
    pwddetail pd;
    int type;

    if(cl && !cl->isauthed)
    {
        int clientrole = CR_DEFAULT;

        if(chan==0) return;
        else if(chan!=1 || getint(p)!=SV_CONNECT) disconnect_client(sender, DISC_TAGT);
        else
        {
            cl->acversion = getint(p);
            cl->acbuildtype = getint(p);
            defformatstring(tags)(", AC: %d|%x", cl->acversion, cl->acbuildtype);
            getstring(text, p);
            filtertext(text, text, FTXT__PLAYERNAME, MAXNAMELEN);
            if(!text[0]) copystring(text, "unarmed");
            copystring(cl->name, text, MAXNAMELEN+1);
            getstring(text, p);
            copystring(cl->pwd, text);
            getstring(text, p);
            filterlang(cl->lang, text);
            int wantrole = getint(p);
            cl->state.nextprimary = getint(p);
            loopi(2) cl->skin[i] = getint(p);
            int bantype = getbantype(sender);
            bool banned = bantype > BAN_NONE;
            bool srvfull = numnonlocalclients() > scl.maxclients;
            bool srvprivate = mastermode == MM_PRIVATE || mastermode == MM_MATCH;
            bool matchreconnect = mastermode == MM_MATCH && findscore(*cl, false);
            int bl = 0, wl = nickblacklist.checkwhitelist(*cl);
            if(wl == NWL_PASS) concatstring(tags, ", nickname whitelist match");
            if(wl == NWL_UNLISTED) bl = nickblacklist.checkblacklist(cl->name);
            if(matchreconnect && !banned)
            { // former player reconnecting to a server in match mode
                cl->isauthed = true;
                logline(ACLOG_INFO, "[%s] %s logged in (reconnect to match)%s", cl->hostname, cl->name, tags);
            }
            else if(wl == NWL_IPFAIL || wl == NWL_PWDFAIL)
            { // nickname matches whitelist, but IP is not in the required range or PWD doesn't match
                logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong %s%s", cl->hostname, cl->name, wl == NWL_IPFAIL ? "IP" : "PWD", tags);
                disconnect_client(sender, DISC_BADNICK);
            }
            else if(bl > 0)
            { // nickname matches blacklist
                logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d%s", cl->hostname, cl->name, bl, tags);
                disconnect_client(sender, DISC_BADNICK);
            }
            else if(passwords.check(cl->name, cl->pwd, cl->salt, &pd, (cl->type==ST_TCPIP ? cl->peer->address.host : 0)) && (!pd.denyadmin || (banned && !srvfull && !srvprivate)) && bantype != BAN_MASTER) // pass admins always through
            { // admin (or deban) password match
                cl->isauthed = true;
                if(!pd.denyadmin && wantrole == CR_ADMIN) clientrole = CR_ADMIN;
                if(bantype == BAN_VOTE)
                {
                    loopv(bans) if(bans[i].address.host == cl->peer->address.host) { bans.remove(i); concatstring(tags, ", ban removed"); break; } // remove admin bans
                }
                if(srvfull)
                {
                    loopv(clients) if(i != sender && clients[i]->type==ST_TCPIP)
                    {
                        disconnect_client(i, DISC_MAXCLIENTS); // disconnect someone else to fit maxclients again
                        break;
                    }
                }
                logline(ACLOG_INFO, "[%s] %s logged in using the admin password in line %d%s", cl->hostname, cl->name, pd.line, tags);
            }
            else if(scl.serverpassword[0] && !(srvprivate || srvfull || banned))
            { // server password required
                if(!strcmp(genpwdhash(cl->name, scl.serverpassword, cl->salt), cl->pwd))
                {
                    cl->isauthed = true;
                    logline(ACLOG_INFO, "[%s] %s client logged in (using serverpassword)%s", cl->hostname, cl->name, tags);
                }
                else disconnect_client(sender, DISC_WRONGPW);
            }
            else if(srvprivate) disconnect_client(sender, DISC_MASTERMODE);
            else if(srvfull) disconnect_client(sender, DISC_MAXCLIENTS);
            else if(banned) disconnect_client(sender, DISC_BANREFUSE);
            else
            {
                cl->isauthed = true;
                logline(ACLOG_INFO, "[%s] %s logged in (default)%s", cl->hostname, cl->name, tags);
            }
        }
        if(!cl->isauthed) return;

        if(cl->type==ST_TCPIP)
        {
            loopv(clients) if(i != sender)
            {
                client *dup = clients[i];
                if(dup->type==ST_TCPIP && dup->peer->address.host==cl->peer->address.host && dup->peer->address.port==cl->peer->address.port)
                    disconnect_client(i, DISC_DUP);
            }
        }

        sendwelcome(cl);
        if(restorescore(*cl)) { sendresume(*cl, true); senddisconnectedscores(-1); }
        else if(cl->type==ST_TCPIP) senddisconnectedscores(sender);
        sendinitclient(*cl);
        if(clientrole != CR_DEFAULT) changeclientrole(sender, clientrole, NULL, true);
        if( server_curvote && server_curvote->result == VOTE_NEUTRAL ) callvotepacket (cl->clientnum);

        // send full IPs to admins
        loopv(clients)
        {
            if(clients[i] && clients[i]->clientnum != cl->clientnum && (clients[i]->role == CR_ADMIN || clients[i]->type == ST_LOCAL))
                sendiplist(clients[i]->clientnum, cl->clientnum);
        }
    }

    if(!cl) { logline(ACLOG_ERROR, "<NULL> client in process()"); return; }  // should never happen anyway

    if(packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;

    #define QUEUE_MSG { if(cl->type==ST_TCPIP) while(curmsg<p.length()) cl->messages.add(p.buf[curmsg++]); }
    #define QUEUE_BUF(body) \
    { \
        if(cl->type==ST_TCPIP) \
        { \
            curmsg = p.length(); \
            { body; } \
        } \
    }
    #define QUEUE_INT(n) QUEUE_BUF(putint(cl->messages, n))
    #define QUEUE_UINT(n) QUEUE_BUF(putuint(cl->messages, n))
    #define QUEUE_STR(text) QUEUE_BUF(sendstring(text, cl->messages))
    #define MSG_PACKET(packet) \
        packetbuf buf(16 + p.length() - curmsg, ENET_PACKET_FLAG_RELIABLE); \
        putint(buf, SV_CLIENT); \
        putint(buf, cl->clientnum); \
        putuint(buf, p.length() - curmsg); \
        buf.put(&p.buf[curmsg], p.length() - curmsg); \
        ENetPacket *packet = buf.finalize();

    int curmsg;
    while((curmsg = p.length()) < p.maxlen)
    {
        type = checktype(getint(p), cl);

        #ifdef _DEBUG
        if(type!=SV_POS && type!=SV_POSC && type!=SV_CLIENTPING && type!=SV_PING && type!=SV_CLIENT)
        {
            DEBUGVAR(cl->name);
            if(type >= 0) { DEBUGVAR(messagenames[type]); }
            protocoldebug(DEBUGCOND);
        }
        else protocoldebug(false);
        #endif

        type = checkmessage(cl,type);
        switch(type)
        {
            case SV_TEAMTEXTME:
            case SV_TEAMTEXT:
                getstring(text, p);
                filtertext(text, text, FTXT__CHAT);
                trimtrailingwhitespace(text);
                if(*text)
                {
                    bool canspeech = forbiddenlist.canspeech(text);
                    if(!spamdetect(cl, text) && canspeech) // team chat
                    {
                        logline(ACLOG_INFO, "[%s] %s%s says to team %s: '%s'", cl->hostname, type == SV_TEAMTEXTME ? "(me) " : "", cl->name, team_string(cl->team), text);
                        sendteamtext(text, sender, type);
                    }
                    else
                    {
                        logline(ACLOG_INFO, "[%s] %s%s says to team %s: '%s', %s", cl->hostname, type == SV_TEAMTEXTME ? "(me) " : "",
                                cl->name, team_string(cl->team), text, canspeech ? "SPAM detected" : "Forbidden speech");
                        if (canspeech)
                        {
                            sendservmsg("\f3Please do not spam; your message was not delivered.", sender);
                            if ( cl->spamcount > SPAMMAXREPEAT + 2 ) disconnect_client(cl->clientnum, DISC_ABUSE);
                        }
                        else
                        {
                            sendservmsg("\f3Watch your language! Your message was not delivered.", sender);
                            kick_abuser(cl->clientnum, cl->badmillis, cl->badspeech, 3);
                        }
                    }
                }
                break;

            case SV_TEXTME:
            case SV_TEXT:
            {
                int mid1 = curmsg, mid2 = p.length();
                getstring(text, p);
                filtertext(text, text, FTXT__CHAT);
                trimtrailingwhitespace(text);
                if(*text)
                {
                    bool canspeech = forbiddenlist.canspeech(text);
                    if(!spamdetect(cl, text) && canspeech)
                    {
                        if(mastermode != MM_MATCH || !matchteamsize || team_isactive(cl->team) || (cl->team == TEAM_SPECT && cl->role == CR_ADMIN)) // common chat
                        {
                            logline(ACLOG_INFO, "[%s] %s%s says: '%s'", cl->hostname, type == SV_TEXTME ? "(me) " : "", cl->name, text);
                            if(cl->type==ST_TCPIP) while(mid1<mid2) cl->messages.add(p.buf[mid1++]);
                            QUEUE_STR(text);
                        }
                        else // spect chat
                        {
                            logline(ACLOG_INFO, "[%s] %s%s says to team %s: '%s'", cl->hostname, type == SV_TEAMTEXTME ? "(me) " : "", cl->name, team_string(cl->team), text);
                            sendteamtext(text, sender, type == SV_TEXTME ? SV_TEAMTEXTME : SV_TEAMTEXT);
                        }
                    }
                    else
                    {
                        logline(ACLOG_INFO, "[%s] %s%s says: '%s', %s", cl->hostname, type == SV_TEXTME ? "(me) " : "",
                                cl->name, text, canspeech ? "SPAM detected" : "Forbidden speech");
                        if (canspeech)
                        {
                            sendservmsg("\f3Please do not spam; your message was not delivered.", sender);
                            if ( cl->spamcount > SPAMMAXREPEAT + 2 ) disconnect_client(cl->clientnum, DISC_ABUSE);
                        }
                        else
                        {
                            sendservmsg("\f3Watch your language! Your message was not delivered.", sender);
                            kick_abuser(cl->clientnum, cl->badmillis, cl->badspeech, 3);
                        }
                    }
                }
                break;
            }

            case SV_TEXTPRIVATE:
            {
                int targ = getint(p);
                getstring(text, p);
                filtertext(text, text, FTXT__CHAT);
                trimtrailingwhitespace(text);

                if(!valid_client(targ)) break;
                client *target = clients[targ];

                if(*text)
                {
                    bool canspeech = forbiddenlist.canspeech(text);
                    if(!spamdetect(cl, text) && canspeech)
                    {
                        bool allowed = !(mastermode == MM_MATCH && cl->team != target->team) && cl->role >= roleconf('t');
                        logline(ACLOG_INFO, "[%s] %s says to %s: '%s' (%s)", cl->hostname, cl->name, target->name, text, allowed ? "allowed":"disallowed");
                        if(allowed) sendf(target->clientnum, 1, "riis", SV_TEXTPRIVATE, cl->clientnum, text);
                    }
                    else
                    {
                        logline(ACLOG_INFO, "[%s] %s says to %s: '%s', %s", cl->hostname, cl->name, target->name, text, canspeech ? "SPAM detected" : "Forbidden speech");
                        if (canspeech)
                        {
                            sendservmsg("\f3Please do not spam; your message was not delivered.", sender);
                            if ( cl->spamcount > SPAMMAXREPEAT + 2 ) disconnect_client(cl->clientnum, DISC_ABUSE);
                        }
                        else
                        {
                            sendservmsg("\f3Watch your language! Your message was not delivered.", sender);
                            kick_abuser(cl->clientnum, cl->badmillis, cl->badspeech, 3);
                        }
                    }
                }
            }
            break;

            case SV_VOICECOM:
            case SV_VOICECOMTEAM:
            {
                int s = getint(p);
                /* spam filter */
                if ( servmillis > cl->mute ) // client is not muted
                {
                    if( s < S_AFFIRMATIVE || s >= S_NULL ) cl->mute = servmillis + 10000; // vc is invalid
                    else if ( cl->lastvc + 4000 < servmillis ) { if ( cl->spam > 0 ) cl->spam -= (servmillis - cl->lastvc) / 4000; } // no vc in the last 4 seconds
                    else cl->spam++; // the guy is spamming
                    if ( cl->spam < 0 ) cl->spam = 0;
                    cl->lastvc = servmillis; // register
                    if ( cl->spam > 4 ) { cl->mute = servmillis + 10000; break; } // 5 vcs in less than 20 seconds... shut up please
                    if ( m_teammode ) checkteamplay(s,sender); // finally here we check the teamplay comm
                    if ( type == SV_VOICECOM ) { QUEUE_MSG; }
                    else sendvoicecomteam(s, sender);
                }
            }
            break;

            case SV_MAPIDENT:
            {
                int gzs = getint(p);
                int rev = getint(p);
                if(!isdedicated || (smapstats.cgzsize == gzs && smapstats.hdr.maprevision == rev))
                { // here any game really starts for a client: spawn, if it's a new game - don't spawn if the game was already running
                    cl->isonrightmap = true;
                    int sp = canspawn(cl);
                    sendf(sender, 1, "rii", SV_SPAWNDENY, sp);
                    cl->spawnperm = sp;
                    if(cl->loggedwrongmap) logline(ACLOG_INFO, "[%s] %s is now on the right map: revision %d/%d", cl->hostname, cl->name, rev, gzs);
                    bool spawn = false;
                    if(team_isspect(cl->team))
                    {
                        if(numclients() < 2 && !m_demo && mastermode != MM_MATCH) // spawn on empty servers
                        {
                            spawn = updateclientteam(cl->clientnum, TEAM_ANYACTIVE, FTR_INFO);
                        }
                    }
                    else
                    {
                        if((cl->freshgame || numclients() < 2) && !m_demo) spawn = true;
                    }
                    cl->freshgame = false;
                    if(spawn) sendspawn(cl);

                }
                else
                {
                    forcedeath(cl);
                    logline(ACLOG_INFO, "[%s] %s is on the wrong map: revision %d/%d", cl->hostname, cl->name, rev, gzs);
                    cl->loggedwrongmap = true;
                    sendf(sender, 1, "rii", SV_SPAWNDENY, SP_WRONGMAP);
                }
                QUEUE_MSG;
                break;
            }

            case SV_ITEMPICKUP:
            {
                int n = getint(p);
                if(!arenaround || arenaround - gamemillis > 2000)
                {
                    gameevent &pickup = cl->addevent();
                    pickup.type = GE_PICKUP;
                    pickup.pickup.ent = n;
                }
                break;
            }

            case SV_WEAPCHANGE:
            {
                int gunselect = getint(p);
                if(gunselect<0 || gunselect>=NUMGUNS || gunselect == GUN_CPISTOL) break;
                if(!m_demo && !m_coop) checkweapon(type,gunselect);
                cl->state.gunselect = gunselect;
                QUEUE_MSG;
                break;
            }

            case SV_PRIMARYWEAP:
            {
                int nextprimary = getint(p);
                if (nextprimary < 0 || nextprimary >= NUMGUNS) break;
                cl->state.nextprimary = nextprimary;
                break;
            }

            case SV_SWITCHNAME:
            {
                QUEUE_MSG;
                getstring(text, p);
                filtertext(text, text, FTXT__PLAYERNAME, MAXNAMELEN);
                if(!text[0]) copystring(text, "unarmed");
                QUEUE_STR(text);
                bool namechanged = strcmp(cl->name, text) != 0;
                if(namechanged) logline(ACLOG_INFO,"[%s] %s changed name to %s", cl->hostname, cl->name, text);
                copystring(cl->name, text, MAXNAMELEN+1);
                if(namechanged)
                {
                    // very simple spam detection (possible FIXME: centralize spam detection)
                    if(cl->type==ST_TCPIP && (servmillis - cl->lastprofileupdate < 1000))
                    {
                        ++cl->fastprofileupdates;
                        if(cl->fastprofileupdates == 3) sendservmsg("\f3Please do not spam", sender);
                        if(cl->fastprofileupdates >= 5) { disconnect_client(sender, DISC_ABUSE); break; }
                    }
                    else if(servmillis - cl->lastprofileupdate > 10000) cl->fastprofileupdates = 0;
                    cl->lastprofileupdate = servmillis;

                    switch(nickblacklist.checkwhitelist(*cl))
                    {
                        case NWL_PWDFAIL:
                        case NWL_IPFAIL:
                            logline(ACLOG_INFO, "[%s] '%s' matches nickname whitelist: wrong IP/PWD", cl->hostname, cl->name);
                            disconnect_client(sender, DISC_BADNICK);
                            break;

                        case NWL_UNLISTED:
                        {
                            int l = nickblacklist.checkblacklist(cl->name);
                            if(l >= 0)
                            {
                                logline(ACLOG_INFO, "[%s] '%s' matches nickname blacklist line %d", cl->hostname, cl->name, l);
                                disconnect_client(sender, DISC_BADNICK);
                            }
                            break;
                        }
                    }
                }
                break;
            }

            case SV_SWITCHTEAM:
            {
                int t = getint(p);
                if(!updateclientteam(sender, team_isvalid(t) ? t : TEAM_SPECT, FTR_PLAYERWISH)) sendf(sender, 1, "rii", SV_TEAMDENY, t);
                break;
            }

            case SV_SWITCHSKIN:
            {
                loopi(2) cl->skin[i] = getint(p);
                QUEUE_MSG;

                if(cl->type==ST_TCPIP && (servmillis - cl->lastprofileupdate < 1000))
                {
                    ++cl->fastprofileupdates;
                    if(cl->fastprofileupdates == 3) sendservmsg("\f3Please do not spam", sender);
                    if(cl->fastprofileupdates >= 5) disconnect_client(sender, DISC_ABUSE);
                }
                else if(servmillis - cl->lastprofileupdate > 10000) cl->fastprofileupdates = 0;
                cl->lastprofileupdate = servmillis;
                break;
            }

            case SV_TRYSPAWN:
            {
                int sp = canspawn(cl);
                if(team_isspect(cl->team) && sp < SP_OK_NUM)
                {
                    updateclientteam(sender, TEAM_ANYACTIVE, FTR_PLAYERWISH);
                    sp = canspawn(cl);
                }
                if( !m_arena && sp < SP_OK_NUM && gamemillis > cl->state.lastspawn + 1000 ) sendspawn(cl);
                break;
            }

            case SV_SPAWN:
            {
                int ls = getint(p), gunselect = getint(p);
                if((cl->state.state!=CS_ALIVE && cl->state.state!=CS_DEAD && cl->state.state!=CS_SPECTATE) ||
                    ls!=cl->state.lifesequence || cl->state.lastspawn<0 || gunselect<0 || gunselect>=NUMGUNS || gunselect == GUN_CPISTOL) break;
                cl->state.lastspawn = -1;
                cl->state.spawn = gamemillis;
				cl->autospawn = false;
                cl->upspawnp = false;
                cl->state.state = CS_ALIVE;
                cl->state.gunselect = gunselect;
                QUEUE_BUF(
                {
                    putint(cl->messages, SV_SPAWN);
                    putint(cl->messages, cl->state.lifesequence);
                    putint(cl->messages, cl->state.health);
                    putint(cl->messages, cl->state.armour);
                    putint(cl->messages, cl->state.gunselect);
                    loopi(NUMGUNS) putint(cl->messages, cl->state.ammo[i]);
                    loopi(NUMGUNS) putint(cl->messages, cl->state.mag[i]);
                });
                break;
            }

            case SV_SUICIDE:
            {
                gameevent &suicide = cl->addevent();
                suicide.type = GE_SUICIDE;
                break;
            }

            case SV_SHOOT:
            {
                gameevent &shot = cl->addevent();
                shot.type = GE_SHOT;
                #define seteventmillis(event) \
                { \
                    event.id = getint(p); \
                    if(!cl->timesync || (cl->events.length()==1 && cl->state.waitexpired(gamemillis))) \
                    { \
                        cl->timesync = true; \
                        cl->gameoffset = gamemillis - event.id; \
                        event.millis = gamemillis; \
                    } \
                    else event.millis = cl->gameoffset + event.id; \
                }
                seteventmillis(shot.shot);
                shot.shot.gun = getint(p);
                loopk(3) { shot.shot.from[k] = cl->state.o.v[k] + ( k == 2 ? (((cl->f>>7)&1)?2.2f:4.2f) : 0); }
                loopk(3) { float v = getint(p)/DMF; shot.shot.to[k] = ((k<2 && v<0.0f)?0.0f:v); }
                int hits = getint(p);
                int tcn = -1;
                loopk(hits)
                {
                    gameevent &hit = cl->addevent();
                    hit.type = GE_HIT;
                    tcn = hit.hit.target = getint(p);
                    hit.hit.lifesequence = getint(p);
                    hit.hit.info = getint(p);
                    loopk(3) hit.hit.dir[k] = getint(p)/DNF;
                }
                if(!m_demo && !m_coop) checkshoot(sender, shot, hits, tcn);
                break;
            }

            case SV_EXPLODE: // Brahma says: FIXME handle explosion by location and deal damage from server
            {
                gameevent &exp = cl->addevent();
                exp.type = GE_EXPLODE;
                seteventmillis(exp.explode);
                exp.explode.gun = getint(p);
                exp.explode.id = getint(p);
                int hits = getint(p);
                loopk(hits)
                {
                    gameevent &hit = cl->addevent();
                    hit.type = GE_HIT;
                    hit.hit.target = getint(p);
                    hit.hit.lifesequence = getint(p);
                    hit.hit.dist = getint(p)/DMF;
                    loopk(3) hit.hit.dir[k] = getint(p)/DNF;
                }
                break;
            }

            case SV_AKIMBO:
            {
                gameevent &akimbo = cl->addevent();
                akimbo.type = GE_AKIMBO;
                seteventmillis(akimbo.akimbo);
                break;
            }

            case SV_RELOAD:
            {
                gameevent &reload = cl->addevent();
                reload.type = GE_RELOAD;
                seteventmillis(reload.reload);
                reload.reload.gun = getint(p);
                break;
            }

            // for AUTH:

            case SV_AUTHTRY:
            {
                string desc, name;
                getstring(desc, p, sizeof(desc)); // unused for now
                getstring(name, p, sizeof(name));
                if(!desc[0]) tryauth(cl, name);
                break;
            }

            case SV_AUTHANS:
            {
                string desc, ans;
                getstring(desc, p, sizeof(desc)); // unused for now
                uint id = (uint)getint(p);
                getstring(ans, p, sizeof(ans));
                if(!desc[0]) answerchallenge(cl, id, ans);
                break;
            }


            case SV_AUTHT:
            {
/*                int n = getint(p);
                loopi(n) getint(p);*/
//                 if (cl) disconnect_client(cl->clientnum, DISC_TAGT); // remove this in the future, when auth is complete
                break;
            }
            // :for AUTH

            case SV_PING:
                sendf(sender, 1, "ii", SV_PONG, getint(p));
                break;

            case SV_CLIENTPING:
            {
                int ping = getint(p);
                if(cl) cl->ping = cl->ping == 9999 ? ping : (cl->ping * 4 + ping) / 5;
                QUEUE_MSG;
                break;
            }

            case SV_POS:
            {
                int cn = getint(p);
                if(cn!=sender)
                {
                    disconnect_client(sender, DISC_CN);
    #ifndef STANDALONE
                    conoutf("ERROR: invalid client (msg %i)", type);
    #endif
                    return;
                }
                loopi(3) cl->state.o[i] = getuint(p)/DMF;
                cl->y = getuint(p);
                cl->p = getint(p);
                cl->g = getuint(p);
                loopi(4) if ( (cl->g >> i) & 1 ) getint(p);
                cl->f = getuint(p);
                if(!cl->isonrightmap) break;
                if(cl->type==ST_TCPIP && (cl->state.state==CS_ALIVE || cl->state.state==CS_EDITING))
                {
                    cl->position.setsize(0);
                    while(curmsg<p.length()) cl->position.add(p.buf[curmsg++]);
                }
                if(!m_demo && !m_coop) checkmove(cl);
                break;
            }

            case SV_POSC:
            {
                bitbuf<ucharbuf> q(p);
                int cn = q.getbits(5);
                if(cn!=sender)
                {
                    disconnect_client(sender, DISC_CN);
    #ifndef STANDALONE
                    conoutf("ERROR: invalid client (msg %i)", type);
    #endif
                    return;
                }
                int usefactor = q.getbits(2) + 7;
                int xt = q.getbits(usefactor + 4);
                int yt = q.getbits(usefactor + 4);
                cl->y = (q.getbits(9)*360)/512;
                cl->p = ((q.getbits(8)-128)*90)/127;
                if(!q.getbits(1)) q.getbits(6);
                if(!q.getbits(1)) q.getbits(4 + 4 + 4);
                cl->f = q.getbits(8);
                int negz = q.getbits(1);
                int zfull = q.getbits(1);
                int s = q.rembits();
                if(s < 3) s += 8;
                if(zfull) s = 11;
                int zt = q.getbits(s);
                if(negz) zt = -zt;
                int g1 = q.getbits(1); // scoping
                int g2 = q.getbits(1); // shooting
                cl->g = (g1<<4) | (g2<<5);

                if(!cl->isonrightmap || p.remaining() || p.overread()) { p.flags = 0; break; }
                if(((cl->f >> 6) & 1) != (cl->state.lifesequence & 1) || usefactor != (smapstats.hdr.sfactor < 7 ? 7 : smapstats.hdr.sfactor)) break;
                cl->state.o[0] = xt / DMF;
                cl->state.o[1] = yt / DMF;
                cl->state.o[2] = zt / DMF;
                if(cl->type==ST_TCPIP && (cl->state.state==CS_ALIVE || cl->state.state==CS_EDITING))
                {
                    cl->position.setsize(0);
                    while(curmsg<p.length()) cl->position.add(p.buf[curmsg++]);
                }
                if(!m_demo && !m_coop) checkmove(cl);
                break;
            }

            case SV_SENDMAP:
            {
                getstring(text, p);
                filtertext(text, text, FTXT__MAPNAME);
                const char *sentmap = behindpath(text), *reject = NULL;
                int mapsize = getint(p);
                int cfgsize = getint(p);
                int cfgsizegz = getint(p);
                int revision = getint(p);
                if(p.remaining() < mapsize + cfgsizegz || MAXMAPSENDSIZE < mapsize + cfgsizegz)
                {
                    p.forceoverread();
                    break;
                }
                int mp = findmappath(sentmap);
                if(readonlymap(mp))
                {
                    reject = "map is ro";
                    defformatstring(msg)("\f3map upload rejected: map %s is readonly", sentmap);
                    sendservmsg(msg, sender);
                }
                else if( scl.incoming_limit && ( scl.incoming_limit << 20 ) < incoming_size + mapsize + cfgsizegz )
                {
                    reject = "server incoming reached its limits";
                    sendservmsg("\f3server does not support more incomings: limit reached", sender);
                }
                else if(mp == MAP_NOTFOUND && strchr(scl.mapperm, 'C') && cl->role < CR_ADMIN)
                {
                    reject = "no permission for initial upload";
                    sendservmsg("\f3initial map upload rejected: you need to be admin", sender);
                }
                else if(mp == MAP_TEMP && revision >= mapbuffer.revision && !strchr(scl.mapperm, 'u') && cl->role < CR_ADMIN) // default: only admins can update maps
                {
                    reject = "no permission to update";
                    sendservmsg("\f3map update rejected: you need to be admin", sender);
                }
                else if(mp == MAP_TEMP && revision < mapbuffer.revision && !strchr(scl.mapperm, 'r') && cl->role < CR_ADMIN) // default: only admins can revert maps to older revisions
                {
                    reject = "no permission to revert revision";
                    sendservmsg("\f3map revert to older revision rejected: you need to be admin to upload an older map", sender);
                }
                else
                {
                    if(mapbuffer.sendmap(sentmap, mapsize, cfgsize, cfgsizegz, &p.buf[p.len]))
                    {
                        incoming_size += mapsize + cfgsizegz;
                        logline(ACLOG_INFO,"[%s] %s sent map %s, rev %d, %d + %d(%d) bytes written",
                                    clients[sender]->hostname, clients[sender]->name, sentmap, revision, mapsize, cfgsize, cfgsizegz);
                        defformatstring(msg)("%s (%d) up%sed map %s, rev %d%s", clients[sender]->name, sender, mp == MAP_NOTFOUND ? "load": "dat", sentmap, revision,
                            /*strcmp(sentmap, behindpath(smapname)) || smode == GMODE_COOPEDIT ? "" :*/ "\f3 (restart game to use new map version)");
                        sendservmsg(msg);
                    }
                    else
                    {
                        reject = "write failed (no 'incoming'?)";
                        sendservmsg("\f3map upload failed", sender);
                    }
                }
                if (reject)
                {
                    logline(ACLOG_INFO,"[%s] %s sent map %s rev %d, rejected: %s",
                                clients[sender]->hostname, clients[sender]->name, sentmap, revision, reject);
                }
                p.len += mapsize + cfgsizegz;
                break;
            }

            case SV_RECVMAP:
            {
                if(mapbuffer.available())
                {
                    resetflag(cl->clientnum); // drop ctf flag
                    savedscore *sc = findscore(*cl, true); // save score
                    if(sc) sc->save(cl->state, cl->team);
                    mapbuffer.sendmap(cl, 2);
                    cl->mapchange(true);
                    sendwelcome(cl, 2); // resend state properly
                }
                else sendservmsg("no map to get", cl->clientnum);
                break;
            }

            case SV_REMOVEMAP:
            {
                getstring(text, p);
                filtertext(text, text, FTXT__MAPNAME);
                string filename;
                const char *rmmap = behindpath(text), *reject = NULL;
                int mp = findmappath(rmmap);
                int reqrole = strchr(scl.mapperm, 'D') ? CR_ADMIN : (strchr(scl.mapperm, 'd') ? CR_DEFAULT : CR_ADMIN + 100);
                if(cl->role < reqrole) reject = "no permission";
                else if(readonlymap(mp)) reject = "map is readonly";
                else if(mp == MAP_NOTFOUND) reject = "map not found";
                else
                {
                    formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cgz", rmmap);
                    remove(filename);
                    formatstring(filename)(SERVERMAP_PATH_INCOMING "%s.cfg", rmmap);
                    remove(filename);
                    defformatstring(msg)("map '%s' deleted", rmmap);
                    sendservmsg(msg, sender);
                    logline(ACLOG_INFO,"[%s] deleted map %s", clients[sender]->hostname, rmmap);
                }
                if (reject)
                {
                    logline(ACLOG_INFO,"[%s] deleting map %s failed: %s", clients[sender]->hostname, rmmap, reject);
                    defformatstring(msg)("\f3can't delete map '%s', %s", rmmap, reject);
                    sendservmsg(msg, sender);
                }
                break;
            }

            case SV_FLAGACTION:
            {
                int action = getint(p);
                int flag = getint(p);
                if(!m_flags || flag < 0 || flag > 1 || action < 0 || action > FA_NUM) break;
                flagaction(flag, action, sender);
                break;
            }

            case SV_SETADMIN:
            {
                bool claim = getint(p) != 0;
                if(claim)
                {
                    getstring(text, p);
                    changeclientrole(sender, CR_ADMIN, text);
                } else changeclientrole(sender, CR_DEFAULT);
                break;
            }

            case SV_CALLVOTE:
            {
                voteinfo *vi = new voteinfo;
                vi->boot = 0;
                vi->type = getint(p);
                switch(vi->type)
                {
                    case SA_MAP:
                    {
                        getstring(text, p);
                        int mode = getint(p), time = getint(p);
                        vi->gonext = text[0]=='+' && text[1]=='1';
                        if(m_isdemo(mode)) filtertext(text, text, FTXT__DEMONAME);
                        else filtertext(text, behindpath(text), FTXT__MAPNAME);
                        if(time <= 0) time = -1;
                        time = min(time, 60);
                        if (vi->gonext)
                        {
                            int ccs = rnd(maprot.configsets.length());
                            configset *c = maprot.get(ccs);
                            if(c)
                            {
                                strcpy(vi->text,c->mapname);
                                mode = vi->num1 = c->mode;
                                time = vi->num2 = c->time;
                            }
                            else fatal("unable to get next map in maprot");
                        }
                        else
                        {
                            strncpy(vi->text,text,MAXTRANS-1);
                            vi->num1 = mode;
                            vi->num2 = time;
                        }
                        int qmode = (mode >= GMODE_NUM ? mode - GMODE_NUM : mode);
                        if(mode==GMODE_DEMO) vi->action = new demoplayaction(newstring(text));
                        else
                        {
                            char *vmap = newstring(vi->text ? behindpath(vi->text) : "");
                            vi->action = new mapaction(vmap, qmode, time, sender, qmode!=mode);
                        }
                        break;
                    }
                    case SA_KICK:
                    {
                        vi->num1 = cn2boot = getint(p);
                        getstring(text, p);
                        text[61] = '\0';
                        strncpy(vi->text,text,128);
                        filtertext(text, text, FTXT__KICKBANREASON);
                        trimtrailingwhitespace(text);
                        if(strlen(text) > 3 && !strstr(text, "   ")) vi->action = new kickaction(cn2boot, text);
                        vi->boot = 1;
                        break;
                    }
                    case SA_BAN:
                    {
                        vi->num1 = cn2boot = getint(p);
                        getstring(text, p);
                        text[61] = '\0';
                        strncpy(vi->text,text,128);
                        filtertext(text, text, FTXT__KICKBANREASON);
                        trimtrailingwhitespace(text);
                        if(strlen(text) > 3 && !strstr(text, "   ")) vi->action = new banaction(cn2boot, text);
                        vi->boot = 2;
                        break;
                    }
                    case SA_REMBANS:
                        vi->action = new removebansaction();
                        break;
                    case SA_MASTERMODE:
                        vi->action = new mastermodeaction(vi->num1 = getint(p));
                        break;
                    case SA_AUTOTEAM:
                        vi->action = new autoteamaction((vi->num1 = getint(p)) > 0);
                        break;
                    case SA_SHUFFLETEAMS:
                        vi->action = new shuffleteamaction();
                        break;
                    case SA_FORCETEAM:
                        vi->num1 = getint(p);
                        vi->num2 = getint(p);
                        vi->action = new forceteamaction(vi->num1, sender, vi->num2);
                        break;
                    case SA_GIVEADMIN:
                        vi->action = new giveadminaction(vi->num1 = getint(p));
                        break;
                    case SA_RECORDDEMO:
                        vi->action = new recorddemoaction((vi->num1 = getint(p))!=0);
                        break;
                    case SA_STOPDEMO:
                        // compatibility
                        break;
                    case SA_CLEARDEMOS:
                        vi->action = new cleardemosaction(vi->num1 = getint(p));
                        break;
                    case SA_SERVERDESC:
                        getstring(text, p);
                        strncpy(vi->text,text,MAXTRANS-1);
                        filtertext(text, text, FTXT__SERVDESC);
                        vi->action = new serverdescaction(newstring(text), sender);
                        break;
                }
                vi->owner = sender;
                vi->callmillis = servmillis;
                MSG_PACKET(msg);
                if(!scallvote(vi, msg)) delete vi;
                break;
            }

            case SV_VOTE:
            {
                int n = getint(p);
                MSG_PACKET(msg);
                ++msg->referenceCount; // need to increase reference count in case a vote disconnects a player after packet is queued to prevent double-freeing by packetbuf
                svote(sender, n, msg);
                --msg->referenceCount;
                break;
            }

            case SV_LISTDEMOS:
                listdemos(sender);
                break;

            case SV_GETDEMO:
                senddemo(sender, getint(p));
                break;

            case SV_EXTENSION:
            {
                // AC server extensions
                //
                // rules:
                // 1. extensions MUST NOT modify gameplay or the behavior of the game in any way
                // 2. extensions may ONLY be used to extend or automate server administration tasks
                // 3. extensions may ONLY operate on the server and must not send any additional data to the connected clients
                // 4. extensions not adhering to these rules may cause the hosting server being banned from the masterserver
                //
                // also note that there is no guarantee that custom extensions will work in future AC versions


                getstring(text, p, 64);
                char *ext = text;   // extension specifier in the form of OWNER::EXTENSION, see sample below
                int n = getint(p);  // length of data after the specifier
                if(n < 0 || n > 50) return;

                // sample
                if(!strcmp(ext, "driAn::writelog"))
                {
                    // owner:       driAn - root@sprintf.org
                    // extension:   writelog - WriteLog v1.0
                    // description: writes a custom string to the server log
                    // access:      requires admin privileges
                    // usage:       /serverextension driAn::writelog "your log message here.."
                    // note:        There is a 49 character limit. The server will ignore messages with 50+ characters.

                    getstring(text, p, n);
                    if(valid_client(sender) && clients[sender]->role==CR_ADMIN)
                    {
                        logline(ACLOG_INFO, "[%s] %s writes to log: %s", cl->hostname, cl->name, text);
                        sendservmsg("your message has been logged", sender);
                    }
                }
                else if(!strcmp(ext, "set::teamsize"))
                {
                    // intermediate solution to set the teamsize (will be voteable)

                    getstring(text, p, n);
                    if(valid_client(sender) && clients[sender]->role==CR_ADMIN && mastermode == MM_MATCH)
                    {
                        changematchteamsize(atoi(text));
                        defformatstring(msg)("match team size set to %d", matchteamsize);
                        sendservmsg(msg, -1);
                    }
                }
                // else if()

                // add other extensions here

                else for(; n > 0; n--) getint(p); // ignore unknown extensions

                break;
            }

            case -1:
                disconnect_client(sender, DISC_TAGT);
                return;

            case -2:
                disconnect_client(sender, DISC_OVERFLOW);
                return;

            default:
            {
                int size = msgsizelookup(type);
                if(size<=0) { if(sender>=0) disconnect_client(sender, DISC_TAGT); return; }
                loopi(size-1) getint(p);
                QUEUE_MSG;
                break;
            }
        }
    }

    if(p.overread() && sender>=0) disconnect_client(sender, DISC_EOP);

    #ifdef _DEBUG
    protocoldebug(false);
    #endif
}

void localclienttoserver(int chan, ENetPacket *packet)
{
    process(packet, 0, chan);
}

static client &addclient()
{
    client *c = NULL;
    loopv(clients) if(clients[i]->type==ST_EMPTY) { c = clients[i]; break; }
    if(!c)
    {
        c = new client;
        c->clientnum = clients.length();
        clients.add(c);
    }
    c->reset();
    return *c;
}

static void checkintermission()
{
    if(minremain>0)
    {
        minremain = (gamemillis>=gamelimit || forceintermission) ? 0 : (gamelimit - gamemillis + 60000 - 1)/60000;
        sendf(-1, 1, "ri3", SV_TIMEUP, (gamemillis>=gamelimit || forceintermission) ? gamelimit : gamemillis, gamelimit);
    }
    if(!interm && minremain<=0) interm = gamemillis+10000;
    forceintermission = false;
}

static void resetserverifempty()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY) return;
    resetserver("", 0, 10);
    matchteamsize = 0;
    autoteam = true;
    changemastermode(MM_OPEN);
    nextmapname[0] = '\0';
}

static void sendworldstate()
{
    static enet_uint32 lastsend = 0;
    if(clients.empty()) return;
    enet_uint32 curtime = enet_time_get()-lastsend;
    if(curtime<40) return;
    bool flush = buildworldstate();
    lastsend += curtime - (curtime%40);
    if(flush) enet_host_flush(serverhost);
    if(demorecord) recordpackets = true; // enable after 'old' worldstate is sent
}

static void rereadcfgs(void)
{
    maprot.read();
    ipblacklist.read();
    nickblacklist.read();
    forbiddenlist.read();
    passwords.read();
    killmsgs.read();
}

static void loggamestatus(const char *reason)
{
    int fragscore[2] = {0, 0}, flagscore[2] = {0, 0}, pnum[2] = {0, 0};
    string text;
    formatstring(text)("%d minutes remaining", minremain);
    logline(ACLOG_INFO, "");
    logline(ACLOG_INFO, "Game status: %s on %s, %s, %s, %d clients%c %s",
                      modestr(gamemode), smapname, reason ? reason : text, mmfullname(mastermode), totalclients, custom_servdesc ? ',' : '\0', servdesc_current);
    if(!scl.loggamestatus) return;
    logline(ACLOG_INFO, "cn name             %s%s score frag death %sping role    host", m_teammode ? "team " : "", m_flags ? "flag " : "", m_teammode ? "tk " : "");
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type == ST_EMPTY || !c.name[0]) continue;
        formatstring(text)("%2d %-16s ", c.clientnum, c.name);                 // cn name
        if(m_teammode) concatformatstring(text, "%-4s ", team_string(c.team, true)); // teamname (abbreviated)
        if(m_flags) concatformatstring(text, "%4d ", c.state.flagscore);             // flag
        concatformatstring(text, "%6d ", c.state.points);                            // score
        concatformatstring(text, "%4d %5d", c.state.frags, c.state.deaths);          // frag death
        if(m_teammode) concatformatstring(text, " %2d", c.state.teamkills);          // tk
        logline(ACLOG_INFO, "%s%5d %s  %s", text, c.ping, c.role == CR_ADMIN ? "admin " : "normal", c.hostname);
        if(c.team != TEAM_SPECT)
        {
            int t = team_base(c.team);
            flagscore[t] += c.state.flagscore;
            fragscore[t] += c.state.frags;
            pnum[t] += 1;
        }
    }
    if(mastermode == MM_MATCH)
    {
        loopv(savedscores)
        {
            savedscore &sc = savedscores[i];
            if(sc.valid)
            {
                formatstring(text)(m_teammode ? "%-4s " : "", team_string(sc.team, true));
                if(m_flags) concatformatstring(text, "%4d ", sc.flagscore);
                logline(ACLOG_INFO, "   %-16s %s%4d %5d%s    - disconnected", sc.name, text, sc.frags, sc.deaths, m_teammode ? "  -" : "");
                if(sc.team != TEAM_SPECT)
                {
                    int t = team_base(sc.team);
                    flagscore[t] += sc.flagscore;
                    fragscore[t] += sc.frags;
                    pnum[t] += 1;
                }
            }
        }
    }
    if(m_teammode)
    {
        loopi(2) logline(ACLOG_INFO, "Team %4s:%3d players,%5d frags%c%5d flags", team_string(i), pnum[i], fragscore[i], m_flags ? ',' : '\0', flagscore[i]);
    }
    logline(ACLOG_INFO, "");
}

static unsigned char chokelog[MAXCLIENTS + 1] = { 0 };

static void linequalitystats(int elapsed)
{
    static unsigned int chokes[MAXCLIENTS + 1] = { 0 }, spent[MAXCLIENTS + 1] = { 0 }, chokes_raw[MAXCLIENTS + 1] = { 0 }, spent_raw[MAXCLIENTS + 1] = { 0 };
    if(elapsed)
    { // collect data
        int c1 = 0, c2 = 0, r1 = 0, numc = 0;
        loopv(clients)
        {
            client &c = *clients[i];
            if(c.type != ST_TCPIP) continue;
            numc++;
            enet_uint32 &rtt = c.peer->lastRoundTripTime, &throttle = c.peer->packetThrottle;
            if(rtt < c.bottomRTT + c.bottomRTT / 3)
            {
                if(servmillis - c.connectmillis < 5000)
                    c.bottomRTT = rtt;
                else
                    c.bottomRTT = (c.bottomRTT * 15 + rtt) / 16; // simple IIR
            }
            if(throttle < 22) c1++;
            if(throttle < 11) c2++;
            if(rtt > c.bottomRTT * 2 && rtt - c.bottomRTT > 300) r1++;
        }
        spent_raw[numc] += elapsed;
        int t = numc < 7 ? numc : (numc + 1) / 2 + 3;
        chokes_raw[numc] +=  ((c1 >= t ? c1 + c2 : 0) + (r1 >= t ? r1 : 0)) * elapsed;
    }
    else
    { // calculate compressed statistics
        defformatstring(msg)("Uplink quality [ ");
        int ncs = 0;
        loopj(scl.maxclients)
        {
            int i = j + 1;
            int nc = chokes_raw[i] / 1000 / i;
            chokes[i] += nc;
            ncs += nc;
            spent[i] += spent_raw[i] / 1000;
            chokes_raw[i] = spent_raw[i] = 0;
            int s = 0, c = 0;
            if(spent[i])
            {
                frexp((double)spent[i] / 30, &s);
                if(s < 0) s = 0;
                if(s > 15) s = 15;
                if(chokes[i])
                {
                    frexp(((double)chokes[i]) / spent[i], &c);
                    c = 15 + c;
                    if(c < 0) c = 0;
                    if(c > 15) c = 15;
                }
            }
            chokelog[i] = (s << 4) + c;
            concatformatstring(msg, "%02X ", chokelog[i]);
        }
        logline(ACLOG_DEBUG, "%s] +%d", msg, ncs);
    }
}

void serverslice(uint timeout)   // main server update, called from cube main loop in sp, or dedicated server loop
{
    static int msend = 0, mrec = 0, csend = 0, crec = 0, mnum = 0, cnum = 0;
#ifdef STANDALONE
    int nextmillis = (int)enet_time_get();
    if(svcctrl) svcctrl->keepalive();
#else
    int nextmillis = isdedicated ? (int)enet_time_get() : lastmillis;
#endif
    int diff = nextmillis - servmillis;
    gamemillis += diff;
    servmillis = nextmillis;
    servertime = ((diff + 3 * servertime)>>2);
    if (servertime > 40) serverlagged = servmillis;

#ifndef STANDALONE
    if(m_demo)
    {
        readdemo();
        extern void silenttimeupdate(int milliscur, int millismax);
        silenttimeupdate(gamemillis, gametimemaximum);
    }
#endif

    if(minremain>0)
    {
        processevents();
        checkitemspawns(diff);
        bool ktfflagingame = false;
        if(m_flags) loopi(2)
        {
            sflaginfo &f = sflaginfos[i];
            if(f.state == CTFF_DROPPED && gamemillis-f.lastupdate > (m_ctf ? 30000 : 10000)) flagaction(i, FA_RESET, -1);
            if(m_htf && f.state == CTFF_INBASE && gamemillis-f.lastupdate > (smapstats.hasflags ? 10000 : 1000))
            {
                htf_forceflag(i);
            }
            if(m_ktf && f.state == CTFF_STOLEN && gamemillis-f.lastupdate > 15000)
            {
                flagaction(i, FA_SCORE, -1);
            }
            if(f.state == CTFF_INBASE || f.state == CTFF_STOLEN) ktfflagingame = true;
        }
        if(m_ktf && !ktfflagingame) flagaction(rnd(2), FA_RESET, -1); // ktf flag watchdog
        if(m_arena) arenacheck();
        else if(m_autospawn) autospawncheck();
//        if(m_lms) lmscheck();
        sendextras();
        if ( scl.afk_limit && mastermode == MM_OPEN && next_afk_check < servmillis && gamemillis > 20 * 1000 ) check_afk();
    }

    if(server_curvote)
    {
        if(!server_curvote->isalive()) server_curvote->evaluate(true);
        if(server_curvote->result!=VOTE_NEUTRAL) DELETEP(server_curvote);
    }

    int nonlocalclients = numnonlocalclients();

    if(forceintermission || ((smode>1 || (gamemode==0 && nonlocalclients)) && gamemillis-diff>0 && gamemillis/60000!=(gamemillis-diff)/60000))
        checkintermission();
    if(m_demo && !demoplayback) maprot.restart();
    else if(interm && ( (scl.demo_interm && sending_demo) ? gamemillis>(interm<<1) : gamemillis>interm ) )
    {
        sending_demo = false;
        loggamestatus("game finished");
        if(demorecord) enddemorecord();
        interm = nextsendscore = 0;

        //start next game
        if(nextmapname[0]) startgame(nextmapname, nextgamemode);
        else maprot.next();
        nextmapname[0] = '\0';
        map_queued = false;
    }

    resetserverifempty();

    if(!isdedicated) return;     // below is network only

    serverms(smode, numclients(), minremain, smapname, servmillis, serverhost->address, &mnum, &msend, &mrec, &cnum, &csend, &crec, SERVER_PROTOCOL_VERSION);

    if(autoteam && m_teammode && !m_arena && !interm && servmillis - lastfillup > 5000 && refillteams()) lastfillup = servmillis;

    static unsigned int lastThrottleEpoch = 0;
    if(serverhost->bandwidthThrottleEpoch != lastThrottleEpoch)
    {
        if(lastThrottleEpoch) linequalitystats(serverhost->bandwidthThrottleEpoch - lastThrottleEpoch);
        lastThrottleEpoch = serverhost->bandwidthThrottleEpoch;
    }

    if(servmillis - laststatus > 60 * 1000)   // display bandwidth stats, useful for server ops
    {
        laststatus = servmillis;
        rereadcfgs();
        if(nonlocalclients || serverhost->totalSentData || serverhost->totalReceivedData)
        {
            if(nonlocalclients) loggamestatus(NULL);
            logline(ACLOG_INFO, "Status at %s: %d remote clients, %.1f send, %.1f rec (K/sec);"
                                         " Ping: #%d|%d|%d; CSL: #%d|%d|%d (bytes)",
                                          timestring(true, "%d-%m-%Y %H:%M:%S"), nonlocalclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024,
                                          mnum, msend, mrec, cnum, csend, crec);
            mnum = msend = mrec = cnum = csend = crec = 0;
            linequalitystats(0);
        }
        serverhost->totalSentData = serverhost->totalReceivedData = 0;
    }

    ENetEvent event;
    bool serviced = false;
    while(!serviced)
    {
        if(enet_host_check_events(serverhost, &event) <= 0)
        {
            if(enet_host_service(serverhost, &event, timeout) <= 0) break;
            serviced = true;
        }
        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                client &c = addclient();
                c.type = ST_TCPIP;
                c.peer = event.peer;
                c.peer->data = (void *)(size_t)c.clientnum;
                c.connectmillis = servmillis;
                c.state.state = CS_SPECTATE;
                c.salt = rnd(0x1000000)*((servmillis%1000)+1);
                char hn[1024];
                copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
                logline(ACLOG_INFO,"[%s] client connected", c.hostname);
                sendservinfo(c);
                totalclients++;
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE:
            {
                int cn = (int)(size_t)event.peer->data;
                if(valid_client(cn)) process(event.packet, cn, event.channelID);
                if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
            {
                int cn = (int)(size_t)event.peer->data;
                if(!valid_client(cn)) break;
                disconnect_client(cn);
                break;
            }

            default:
                break;
        }
    }
    sendworldstate();
}

void cleanupserver()
{
    if(serverhost) { enet_host_destroy(serverhost); serverhost = NULL; }
    if(svcctrl)
    {
        svcctrl->stop();
        DELETEP(svcctrl);
    }
    exitlogging();
}

int getpongflags(enet_uint32 ip)
{
    int flags = mastermode << PONGFLAG_MASTERMODE;
    flags |= scl.serverpassword[0] ? 1 << PONGFLAG_PASSWORD : 0;
    loopv(bans) if(bans[i].address.host == ip) { flags |= 1 << PONGFLAG_BANNED; break; }
    flags |= ipblacklist.check(ip) ? 1 << PONGFLAG_BLACKLIST : 0;
    return flags;
}

void extping_namelist(ucharbuf &p)
{
    loopv(clients)
    {
        if(clients[i]->type == ST_TCPIP && clients[i]->isauthed) sendstring(clients[i]->name, p);
    }
    sendstring("", p);
}

void extping_serverinfo(ucharbuf &pi, ucharbuf &po)
{
    char lang[3];
    lang[0] = tolower(getint(pi)); lang[1] = tolower(getint(pi)); lang[2] = '\0';
    const char *reslang = lang, *buf = infofiles.getinfo(lang); // try client language
    if(!buf) buf = infofiles.getinfo(reslang = "en");     // try english
    sendstring(buf ? reslang : "", po);
    if(buf)
    {
        for(const char *c = buf; *c && po.remaining() > MAXINFOLINELEN + 10; c += strlen(c) + 1) sendstring(c, po);
        sendstring("", po);
    }
}

void extping_maprot(ucharbuf &po)
{
    putint(po, CONFIG_MAXPAR);
    string text;
    bool abort = false;
    loopv(maprot.configsets)
    {
        if(po.remaining() < 100) abort = true;
        configset &c = maprot.configsets[i];
        filtertext(text, c.mapname, FTXT__MAPNAME);
        text[30] = '\0';
        sendstring(abort ? "-- list truncated --" : text, po);
        loopi(CONFIG_MAXPAR) putint(po, c.par[i]);
        if(abort) break;
    }
    sendstring("", po);
}

void extping_uplinkstats(ucharbuf &po)
{
    if(scl.maxclients > 3)
        po.put(chokelog + 4, scl.maxclients - 3); // send logs for 4..n used slots
}

void extinfo_cnbuf(ucharbuf &p, int cn)
{
    if(cn == -1) // add all available player ids
    {
        loopv(clients) if(clients[i]->type != ST_EMPTY)
            putint(p,clients[i]->clientnum);
    }
    else if(valid_client(cn)) // add single player only
    {
        putint(p,clients[cn]->clientnum);
    }
}

void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len, int *csend)
{
    loopv(clients)
    {
        if(clients[i]->type != ST_TCPIP) continue;
        if(pid>-1 && clients[i]->clientnum!=pid) continue;

        bool ismatch = mastermode == MM_MATCH;
        putint(p,EXT_PLAYERSTATS_RESP_STATS);  // send player stats following
        putint(p,clients[i]->clientnum);  //add player id
        putint(p,clients[i]->ping);             //Ping
        sendstring(clients[i]->name,p);         //Name
        sendstring(team_string(clients[i]->team),p); //Team
        // "team_string(clients[i]->team)" sometimes return NULL according to RK, causing the server to crash. WTF ?
        putint(p,clients[i]->state.frags);      //Frags
        putint(p,clients[i]->state.flagscore);  //Flagscore
        putint(p,clients[i]->state.deaths);     //Death
        putint(p,clients[i]->state.teamkills);  //Teamkills
        putint(p,ismatch ? 0 : clients[i]->state.damage*100/max(clients[i]->state.shotdamage,1)); //Accuracy
        putint(p,ismatch ? 0 : clients[i]->state.health);     //Health
        putint(p,ismatch ? 0 : clients[i]->state.armour);     //Armour
        putint(p,ismatch ? 0 : clients[i]->state.gunselect);  //Gun selected
        putint(p,clients[i]->role);             //Role
        putint(p,clients[i]->state.state);      //State (Alive,Dead,Spawning,Lagged,Editing)
        uint ip = clients[i]->peer->address.host; // only 3 byte of the ip address (privacy protected)
        p.put((uchar*)&ip,3);

        buf.dataLength = len + p.length();
        enet_socket_send(pongsock, &addr, &buf, 1);
        *csend += (int)buf.dataLength;

        if(pid>-1) break;
        p.len=bpos;
    }
}

void extinfo_teamscorebuf(ucharbuf &p)
{
    putint(p, m_teammode ? EXT_ERROR_NONE : EXT_ERROR);
    putint(p, gamemode);
    putint(p, minremain); // possible TODO: use gamemillis, gamelimit here too?
    if(!m_teammode) return;

    int teamsizes[TEAM_NUM] = { 0 }, fragscores[TEAM_NUM] = { 0 }, flagscores[TEAM_NUM] = { 0 };
    loopv(clients) if(clients[i]->type!=ST_EMPTY && team_isvalid(clients[i]->team))
    {
        teamsizes[clients[i]->team] += 1;
        fragscores[clients[i]->team] += clients[i]->state.frags;
        flagscores[clients[i]->team] += clients[i]->state.flagscore;
    }

    loopi(TEAM_NUM) if(teamsizes[i])
    {
        sendstring(team_string(i), p); // team name
        putint(p, fragscores[i]); // add fragscore per team
        putint(p, m_flags ? flagscores[i] : -1); // add flagscore per team
        putint(p, -1); // ?
    }
}


#ifndef STANDALONE
void localdisconnect()
{
    loopv(clients) if(clients[i]->type==ST_LOCAL) clients[i]->zap();
}

void localconnect()
{
    modprotocol = false;
    servstate.reset();
    client &c = addclient();
    c.type = ST_LOCAL;
    c.role = CR_ADMIN;
    c.salt = 0;
    copystring(c.hostname, "local");
    sendservinfo(c);
}
#endif

void processmasterinput(const char *cmd, int cmdlen, const char *args)
{
// AUTH WiP
    uint id;
    string val;
    if(sscanf(cmd, "failauth %u", &id) == 1) authfailed(id);
    else if(sscanf(cmd, "succauth %u", &id) == 1) authsucceeded(id);
    else if(sscanf(cmd, "chalauth %u %s", &id, val) == 2) authchallenged(id, val);
    else if(!strncmp(cmd, "cleargbans", cmdlen)) cleargbans();
    else if(sscanf(cmd, "addgban %s", val) == 1) addgban(val);
}

static string server_name = "unarmed server";

static void quitproc(int param)
{
    // this triggers any "atexit"-calls:
    exit(param == 2 ? EXIT_SUCCESS : EXIT_FAILURE); // 3 is the only reply on Win32 apparently, SIGINT == 2 == Ctrl-C
}

void initserver(bool dedicated, int argc, char **argv)
{
    const char *service = NULL;

    for(int i = 1; i<argc; i++)
    {
        if(!scl.checkarg(argv[i]))
        {
            char *a = &argv[i][2];
            if(!scl.checkarg(argv[i]) && argv[i][0]=='-') switch(argv[i][1])
            {
                case '-': break;
                case 'S': service = a; break;
                default: break; /*printf("WARNING: unknown commandline option\n");*/ // less warnings - 2011feb05:ft: who disabled this - I think this should be on - more warnings == more clarity
            }
            else if (strncmp(argv[i], "assaultcube://", 13)) printf("WARNING: unknown commandline argument\n");
        }
    }

    if(service && !svcctrl)
    {
        #ifdef WIN32
        svcctrl = new winservice(service);
        #endif
        if(svcctrl)
        {
            svcctrl->argc = argc; svcctrl->argv = argv;
            svcctrl->start();
        }
    }

    if ( strlen(scl.servdesc_full) ) global_name = scl.servdesc_full;
    else global_name = server_name;

    smapname[0] = '\0';

    string identity;
    if(scl.logident[0]) filtertext(identity, scl.logident, FTXT__LOGIDENT);
    else formatstring(identity)("%s#%d", scl.ip[0] ? scl.ip : "local", scl.serverport);
    int conthres = scl.verbose > 1 ? ACLOG_DEBUG : (scl.verbose ? ACLOG_VERBOSE : ACLOG_INFO);
    if(dedicated && !initlogging(identity, scl.syslogfacility, conthres, scl.filethres, scl.syslogthres, scl.logtimestamp))
        printf("WARNING: logging not started!\n");
    logline(ACLOG_INFO, "logging local AssaultCube server (version %d, protocol %d/%d) now..", AC_VERSION, SERVER_PROTOCOL_VERSION, EXT_VERSION);

    copystring(servdesc_current, scl.servdesc_full);
    servermsinit(scl.master ? scl.master : AC_MASTER_URI, scl.ip, CUBE_SERVINFO_PORT(scl.serverport), dedicated);

    if((isdedicated = dedicated))
    {
        ENetAddress address = { ENET_HOST_ANY, (enet_uint16)scl.serverport };
        if(scl.ip[0] && enet_address_set_host(&address, scl.ip)<0) logline(ACLOG_WARNING, "server ip not resolved!");
        serverhost = enet_host_create(&address, scl.maxclients+1, 3, 0, scl.uprate);
        if(!serverhost) fatal("could not create server host");
        loopi(scl.maxclients) serverhost->peers[i].data = (void *)-1;

        maprot.init(scl.maprot);
        maprot.next(false, true); // ensure minimum maprot length of '1'
        passwords.init(scl.pwdfile, scl.adminpasswd);
        ipblacklist.init(scl.blfile);
        nickblacklist.init(scl.nbfile);
        forbiddenlist.init(scl.forbidden);
        killmsgs.init(scl.killmessages);
        infofiles.init(scl.infopath, scl.motdpath);
        infofiles.getinfo("en"); // cache 'en' serverinfo
        logline(ACLOG_VERBOSE, "holding up to %d recorded demos in memory", scl.maxdemos);
        if(scl.demopath[0]) logline(ACLOG_VERBOSE,"all recorded demos will be written to: \"%s\"", scl.demopath);
        if(scl.voteperm[0]) logline(ACLOG_VERBOSE,"vote permission string: \"%s\"", scl.voteperm);
        if(scl.mapperm[0]) logline(ACLOG_VERBOSE,"map permission string: \"%s\"", scl.mapperm);
        logline(ACLOG_VERBOSE,"server description: \"%s\"", scl.servdesc_full);
        if(scl.servdesc_pre[0] || scl.servdesc_suf[0]) logline(ACLOG_VERBOSE,"custom server description: \"%sCUSTOMPART%s\"", scl.servdesc_pre, scl.servdesc_suf);
        logline(ACLOG_VERBOSE,"maxclients: %d, kick threshold: %d, ban threshold: %d", scl.maxclients, scl.kickthreshold, scl.banthreshold);
        if(scl.master) logline(ACLOG_VERBOSE,"master server URL: \"%s\"", scl.master);
        if(scl.serverpassword[0]) logline(ACLOG_VERBOSE,"server password: \"%s\"", hiddenpwd(scl.serverpassword));
#ifdef ACAC
        logline(ACLOG_INFO, "anticheat: enabled");
#else
        logline(ACLOG_INFO, "anticheat: disabled");
#endif
    }

    resetserverifempty();

    if(isdedicated)       // do not return, this becomes main loop
    {
        #ifdef WIN32
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif
        // kill -2 / Ctrl-C - see http://msdn.microsoft.com/en-us/library/xdkz3x12%28v=VS.100%29.aspx (or VS-2008?) for caveat (seems not to pertain to AC - 2011feb05:ft)
        if (signal(SIGINT, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGINT!");
        // kill -15 / probably process-manager on Win32 *shrug*
        if (signal(SIGTERM, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGTERM!");
        #ifndef WIN32
        // kill -1
        if (signal(SIGHUP, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGHUP!");
        // kill -9 is uncatchable - http://en.wikipedia.org/wiki/SIGKILL
        //if (signal(SIGKILL, quitproc) == SIG_ERR) logline(ACLOG_INFO, "Cannot handle SIGKILL!");
        #endif
        logline(ACLOG_INFO, "dedicated server started, waiting for clients...");
        logline(ACLOG_INFO, "Ctrl-C to exit"); // this will now actually call the atexit-hooks below - thanks to SIGINT hooked above - noticed and signal-code-docs found by SKB:2011feb05:ft:
        atexit(enet_deinitialize);
        atexit(cleanupserver);
        enet_time_set(0);
        for(;;) serverslice(5);
    }
}

#ifdef STANDALONE

void localservertoclient(int chan, uchar *buf, int len, bool demo) {}
void fatal(const char *s, ...)
{
    defvformatstring(msg,s,s);
    defformatstring(out)("AssaultCube fatal error: %s", msg);
    if (logline(ACLOG_ERROR, "%s", out));
    else puts(out);
    cleanupserver();
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    __try {
    #endif
    #endif
    #endif

    for(int i = 1; i<argc; i++)
    {
        if (!strncmp(argv[i],"--wizard",8)) return wizardmain(argc, argv);
    }

    if(enet_initialize()<0) fatal("Unable to initialise network module");
    initserver(true, argc, argv);
    return EXIT_SUCCESS;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}
#endif

