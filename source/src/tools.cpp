// implementation of generic tools

#include "cube.h"

const char *timestring(bool local, const char *fmt)
{
    static string asciitime;
    time_t t = time(NULL);
    struct tm * timeinfo;
    timeinfo = local ? localtime(&t) : gmtime (&t);
    strftime(asciitime, sizeof(string) - 1, fmt ? fmt : "%Y%m%d_%H.%M.%S", timeinfo); // sortable time for filenames
    return asciitime;
}

const char *asctime()
{
    return timestring(true, "%c");
}

const char *numtime()
{
    static string numt;
    formatstring(numt)("%ld", (long long) time(NULL));
    return numt;
}

static const uchar transformenttab[] = {
            /* mapformat     1..5                               6..7     */
            /*  0 */   /* NOTUSED        */ NOTUSED,      /* NOTUSED     */ NOTUSED,
            /*  1 */   /* LIGHT          */ LIGHT,        /* LIGHT       */ LIGHT,
            /*  2 */   /* PLAYERSTART    */ PLAYERSTART,  /* PLAYERSTART */ PLAYERSTART,
            /*  3 */   /* I_SHELLS       */ I_AMMO,       /* I_CLIPS     */ I_CLIPS,
            /*  4 */   /* I_BULLETS      */ I_AMMO,       /* I_AMMO      */ I_AMMO,
            /*  5 */   /* I_ROCKETS      */ I_AMMO,       /* I_GRENADE   */ I_GRENADE,
            /*  6 */   /* I_ROUNDS       */ I_AMMO,       /* I_HEALTH    */ I_HEALTH,
            /*  7 */   /* I_HEALTH       */ I_HEALTH,     /* I_ARMOUR    */ I_ARMOUR,
            /*  8 */   /* I_BOOST        */ I_HEALTH,     /* I_AKIMBO    */ I_AKIMBO,
            /*  9 */   /* I_GREENARMOUR  */ I_HELMET,     /* MAPMODEL    */ MAPMODEL,
            /* 10 */   /* I_YELLOWARMOUR */ I_ARMOUR,     /* CARROT      */ CARROT,
            /* 11 */   /* I_QUAD         */ I_AKIMBO,     /* LADDER      */ LADDER,
            /* 12 */   /* TELEPORT       */ NOTUSED,      /* CTF_FLAG    */ CTF_FLAG,
            /* 13 */   /* TELEDEST       */ NOTUSED,      /* SOUND       */ SOUND,
            /* 14 */   /* MAPMODEL       */ MAPMODEL,     /* CLIP        */ CLIP,
            /* 15 */   /* MONSTER        */ NOTUSED,      /* PLCLIP      */ PLCLIP,
            /* 16 */   /* CARROT         */ NOTUSED,                        16,
            /* 17 */   /* JUMPPAD        */ NOTUSED,                        17      };

void transformoldentities(int mapversion, uchar &enttype)
{
    const uchar *usetab = transformenttab + (mapversion > 5 ? 1 : 0);
    if(mapversion < 8 && enttype < 18) enttype = usetab[enttype * 2];
}

int fixmapheadersize(int version, int headersize)   // we can't trust hdr.headersize for file versions < 10 (thx flow)
{
    if(version < 4) return sizeof(header) - sizeof(int) * 16;
    else if(version == 7 || version == 8) return sizeof(header) + sizeof(char) * 128;  // mediareq
    else if(version < 10 || headersize < int(sizeof(header))) return sizeof(header);
    return headersize;
}

mapdim mapdims;     // min/max X/Y and delta X/Y and min/max Z

// TODO: Lollo fix this utter crap
extern char *maplayout, *testlayout;
extern int maplayout_factor, testlayout_factor, Mvolume, Marea, Mopen, SHhits;
extern float Mheight;
extern int checkarea(int, char *);

mapstats *loadmapstats(const char *filename, bool getlayout)
{
    const int sizeof_header = sizeof(header), sizeof_baseheader = sizeof_header - sizeof(int) * 16;
    static mapstats s;
    static uchar *enttypes = NULL;
    static short *entposs = NULL;

    DELETEA(enttypes);
    loopi(MAXENTTYPES) s.entcnt[i] = 0;
    loopi(3) s.spawns[i] = 0;
    loopi(2) s.flags[i] = 0;

    stream *f = opengzfile(filename, "rb");
    if(!f) return NULL;
    memset(&s.hdr, 0, sizeof_header);
    if(f->read(&s.hdr, sizeof_baseheader) != sizeof_baseheader || (strncmp(s.hdr.head, "CUBE", 4) && strncmp(s.hdr.head, "ACMP",4))) { delete f; return NULL; }
    lilswap(&s.hdr.version, 4);
    s.hdr.headersize = fixmapheadersize(s.hdr.version, s.hdr.headersize);
    int restofhead = min(s.hdr.headersize, sizeof_header) - sizeof_baseheader;
    if(s.hdr.version > MAXMAPVERSION || s.hdr.numents > MAXENTITIES ||
       f->read(&s.hdr.waterlevel, restofhead) != restofhead ||
       !f->seek(clamp(s.hdr.headersize - sizeof_header, 0, MAXHEADEREXTRA), SEEK_CUR)) { delete f; return NULL; }
    if(s.hdr.version>=4)
    {
        lilswap(&s.hdr.waterlevel, 1);
        lilswap(&s.hdr.maprevision, 2);
    }
    else s.hdr.waterlevel = -100000;
    entity e;
    enttypes = new uchar[s.hdr.numents];
    entposs = new short[s.hdr.numents * 3];
    loopi(s.hdr.numents)
    {
        f->read(&e, sizeof(persistent_entity));
        lilswap((short *)&e, 4);
        transformoldentities(s.hdr.version, e.type);
        if(e.type == PLAYERSTART && (e.attr2 == 0 || e.attr2 == 1 || e.attr2 == 100)) s.spawns[e.attr2 == 100 ? 2 : e.attr2]++;
        if(e.type == CTF_FLAG && (e.attr2 == 0 || e.attr2 == 1)) { s.flags[e.attr2]++; s.flagents[e.attr2] = i; }
        s.entcnt[e.type]++;
        enttypes[i] = e.type;
        entposs[i * 3] = e.x; entposs[i * 3 + 1] = e.y; entposs[i * 3 + 2] = e.z + e.attr1;
    }
    DELETEA(testlayout);
    int minfloor = 0;
    int maxceil = 0;
    if(s.hdr.sfactor <= LARGEST_FACTOR && s.hdr.sfactor >= SMALLEST_FACTOR)
    {
        testlayout_factor = s.hdr.sfactor;
        int layoutsize = 1 << (testlayout_factor * 2);
        bool fail = false;
        testlayout = new char[layoutsize + 256];
        memset(testlayout, 0, layoutsize * sizeof(char));
        char *t = NULL;
        char floor = 0, ceil;
        int diff = 0;
        Mvolume = Marea = SHhits = 0;
        loopk(layoutsize)
        {
            char *c = testlayout + k;
            int type = f->getchar();
            int n = 1;
            switch(type)
            {
                case 255:
                {
                    if(!t || (n = f->getchar()) < 0) { fail = true; break; }
                    memset(c, *t, n);
                    k += n - 1;
                    break;
                }
                case 254: // only in MAPVERSION<=2
                    if(!t) { fail = true; break; }
                    *c = *t;
                    f->getchar(); f->getchar();
                    break;
                default:
                    if(type<0 || type>=MAXTYPE)  { fail = true; break; }
                    floor = f->getchar();
                    ceil = f->getchar();
                    if(floor >= ceil && ceil > -128) floor = ceil - 1;  // for pre 12_13
                    diff = ceil - floor;
                    if(type == FHF) floor = -128;
                    if(floor!=-128 && floor<minfloor) minfloor = floor;
                    if(ceil>maxceil) maxceil = ceil;
                    f->getchar(); f->getchar();
                    if(s.hdr.version>=2) f->getchar();
                    if(s.hdr.version>=5) f->getchar();

                case SOLID:
                    *c = type == SOLID ? 127 : floor;
                    f->getchar(); f->getchar();
                    if(s.hdr.version<=2) { f->getchar(); f->getchar(); }
                    break;
            }
            if ( type != SOLID && diff > 6 )
            {
                // Lucas (10mar2013): Removed "pow2" because it was too strict
                if (diff > MAXMHEIGHT) SHhits += /*pow2*/(diff-MAXMHEIGHT)*n;
                Marea += n;
                Mvolume += diff * n;
            }
            if(fail) break;
            t = c;
        }
        if(fail) { DELETEA(testlayout); }
        else
        {
            Mheight = Marea ? (float)Mvolume/Marea : 0;
            Mopen = checkarea(testlayout_factor, testlayout);
        }
    }
    if(getlayout)
    {
        DELETEA(maplayout);
        if (testlayout)
        {
            maplayout_factor = testlayout_factor;
            extern int maplayoutssize;
            maplayoutssize = 1 << testlayout_factor;
            int layoutsize = 1 << (testlayout_factor * 2);
            maplayout = new char[layoutsize + 256];
            memcpy(maplayout, testlayout, layoutsize * sizeof(char));

            memset(&mapdims, 0, sizeof(struct mapdim));
            mapdims.x1 = mapdims.y1 = maplayoutssize;
            loopk(layoutsize) if (testlayout[k] != 127)
            {
                int cwx = k%maplayoutssize,
                cwy = k/maplayoutssize;
                if(cwx < mapdims.x1) mapdims.x1 = cwx;
                if(cwy < mapdims.y1) mapdims.y1 = cwy;
                if(cwx > mapdims.x2) mapdims.x2 = cwx;
                if(cwy > mapdims.y2) mapdims.y2 = cwy;
            }
            mapdims.xspan = mapdims.x2 - mapdims.x1;
            mapdims.yspan = mapdims.y2 - mapdims.y1;
            mapdims.minfloor = minfloor;
            mapdims.maxceil = maxceil;
        }
    }
    delete f;
    s.hasffaspawns = s.spawns[2] > 0;
    s.hasteamspawns = s.spawns[0] > 0 && s.spawns[1] > 0;
    s.hasflags = s.flags[0] > 0 && s.flags[1] > 0;
    s.enttypes = enttypes;
    s.entposs = entposs;
    s.cgzsize = getfilesize(filename);
    return &s;
}

///////////////////////// debugging ///////////////////////

#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out)("Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    SymInitialize(GetCurrentProcess(), NULL, TRUE);

    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
    {
        struct { IMAGEHLP_SYMBOL sym; string n; } si = { { sizeof( IMAGEHLP_SYMBOL ), 0, 0, 0, sizeof(string) } };
        IMAGEHLP_LINE li = { sizeof( IMAGEHLP_LINE ) };
        DWORD off;
        if(SymGetSymFromAddr(GetCurrentProcess(), (DWORD)sf.AddrPC.Offset, &off, &si.sym) && SymGetLineFromAddr(GetCurrentProcess(), (DWORD)sf.AddrPC.Offset, &off, &li))
        {
            char *del = strrchr(li.FileName, '\\');
            formatstring(t)("%s - %s [%d]\n", si.sym.Name, del ? del + 1 : li.FileName, li.LineNumber);
            concatstring(out, t);
        }
    }
    fatal("%s", out);
}
#elif defined(linux) || defined(__linux) || defined(__linux__)

#include <execinfo.h>

// stack dumping on linux, inspired by Sachin Agrawal's sample code

struct signalbinder
{
    static void stackdumper(int sig)
    {
        printf("stacktrace:\n");

        const int BTSIZE = 25;
        void *array[BTSIZE];
        int n = backtrace(array, BTSIZE);
        char **symbols = backtrace_symbols(array, n);
        for(int i = 0; i < n; i++)
        {
            printf("%s\n", symbols[i]);
        }
        free(symbols);

        fatal("AssaultCube error (%d)", sig);

    }

    signalbinder()
    {
        // register signals to dump the stack if they are raised,
        // use constructor for early registering
        signal(SIGSEGV, stackdumper);
        signal(SIGFPE, stackdumper);
        signal(SIGILL, stackdumper);
        signal(SIGBUS, stackdumper);
        signal(SIGSYS, stackdumper);
        signal(SIGABRT, stackdumper);
    }
};

signalbinder sigbinder;

#endif


///////////////////////// misc tools ///////////////////////

bool cmpb(void *b, int n, enet_uint32 c)
{
    ENetBuffer buf;
    buf.data = b;
    buf.dataLength = n;
    return enet_crc32(&buf, 1)==c;
}

bool cmpf(char *fn, enet_uint32 c)
{
    int n = 0;
    char *b = loadfile(fn, &n);
    bool r = cmpb(b, n, c);
    delete[] b;
    return r;
}

enet_uint32 adler(unsigned char *data, size_t len)
{
    enet_uint32 a = 1, b = 0;
    while (len--)
    {
        a += *data++;
        b += a;
    }
    return b;
}

bool isbigendian()
{
    return !*(const uchar *)&islittleendian;
}

void strtoupper(char *t, const char *s)
{
    if(!s) s = t;
    while(*s)
    {
        *t = toupper(*s);
        t++; s++;
    }
    *t = '\0';
}

const char *atoip(const char *s, enet_uint32 *ip)
{
    unsigned int d[4];
    int n;
    if(!s || sscanf(s, "%u.%u.%u.%u%n", d, d + 1, d + 2, d + 3, &n) != 4) return NULL;
    *ip = 0;
    loopi(4)
    {
        if(d[i] > 255) return NULL;
        *ip = (*ip << 8) + d[i];
    }
    return s + n;
}

const char *atoipr(const char *s, iprange *ir)
{
    if((s = atoip(s, &ir->lr)) == NULL) return NULL;
    ir->ur = ir->lr;
    s += strspn(s, " \t");
    if(*s == '-')
    {
        if(!(s = atoip(s + 1, &ir->ur)) || ir->lr > ir->ur) return NULL;
    }
    else if(*s == '/')
    {
        int m, n;
        if(sscanf(s + 1, "%d%n", &m, &n) != 1 || m < 0 || m > 32) return NULL;
        unsigned long bm = (1 << (32 - m)) - 1;
        ir->lr &= ~bm;
        ir->ur |= bm;
        s += 1 + n;
    }
    return s;
}

const char *iptoa(const enet_uint32 ip)
{
    static string s[2];
    static int buf = 0;
    buf = (buf + 1) % 2;
    formatstring(s[buf])("%d.%d.%d.%d", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return s[buf];
}

const char *iprtoa(const struct iprange &ipr)
{
    static string s[2];
    static int buf = 0;
    buf = (buf + 1) % 2;
    if(ipr.lr == ipr.ur) copystring(s[buf], iptoa(ipr.lr));
    else formatstring(s[buf])("%s-%s", iptoa(ipr.lr), iptoa(ipr.ur));
    return s[buf];
}

int cmpiprange(const struct iprange *a, const struct iprange *b)
{
    if(a->lr < b->lr) return -1;
    if(a->lr > b->lr) return 1;
    return 0;
}

int cmpipmatch(const struct iprange *a, const struct iprange *b)
{
    return - (a->lr < b->lr) + (a->lr > b->ur);
}

char *concatformatstring(char *d, const char *s, ...)
{
    static defvformatstring(temp, s, s);
    return concatstring(d, temp);
}

int cvecprintf(vector<char> &v, const char *s, ...)
{
    defvformatstring(temp, s, s);
    int len = strlen(temp);
    if(len) v.put(temp, len);
    return len;
}

const char *hiddenpwd(const char *pwd, int showchars)
{
    static int sc = 3;
    static string text;
    copystring(text, pwd);
    if(showchars > 0) sc = showchars;
    for(int i = (int)strlen(text) - 1; i >= sc; i--) text[i] = '*';
    return text;
}

