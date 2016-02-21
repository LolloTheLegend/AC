// all server side masterserver and pinging functionality

#include "common.h"
#include "serverms.h"
#include "server.h"
#include "cube.h"

#ifdef STANDALONE
bool resolverwait(const char *name, ENetAddress *address)
{
    return enet_address_set_host(address, name) >= 0;
}

int connectwithtimeout(ENetSocket sock, const char *hostname, ENetAddress &remoteaddress)
{
    int result = enet_socket_connect(sock, &remoteaddress);
    if(result<0) enet_socket_destroy(sock);
    return result;
}
#endif

static ENetSocket mastersock = ENET_SOCKET_NULL;
static ENetAddress masteraddress = { ENET_HOST_ANY, ENET_PORT_ANY }, serveraddress = { ENET_HOST_ANY, ENET_PORT_ANY };
string mastername = AC_MASTER_URI;
int masterport = AC_MASTER_PORT, mastertype = AC_MASTER_HTTP;
static int lastupdatemaster = 0;
static vector<char> masterout, masterin;
static int masteroutpos = 0, masterinpos = 0;

static void disconnectmaster()
{
    if(mastersock == ENET_SOCKET_NULL) return;

    enet_socket_destroy(mastersock);
    mastersock = ENET_SOCKET_NULL;

    masterout.setsize(0);
    masterin.setsize(0);
    masteroutpos = masterinpos = 0;

    masteraddress.host = ENET_HOST_ANY;
    masteraddress.port = ENET_PORT_ANY;

    //lastupdatemaster = 0;
}

ENetSocket connectmaster()
{
    if(!mastername[0]) return ENET_SOCKET_NULL;
    if(scl.maxclients>MAXCL) { logline(ACLOG_WARNING, "maxclient exceeded: cannot register"); return ENET_SOCKET_NULL; }

    if(masteraddress.host == ENET_HOST_ANY)
    {
        logline(ACLOG_INFO, "looking up %s:%d...", mastername, masterport);
        masteraddress.port = masterport;
        if(!resolverwait(mastername, &masteraddress)) return ENET_SOCKET_NULL;
    }
    ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_STREAM);
    if(sock != ENET_SOCKET_NULL && serveraddress.host != ENET_HOST_ANY && enet_socket_bind(sock, &serveraddress) < 0)
    {
        enet_socket_destroy(sock);
        sock = ENET_SOCKET_NULL;
    }
    if(sock == ENET_SOCKET_NULL || connectwithtimeout(sock, mastername, masteraddress) < 0)
    {
        logline(ACLOG_WARNING, sock==ENET_SOCKET_NULL ? "could not open socket" : "could not connect");
        return ENET_SOCKET_NULL;
    }

    enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);
    return sock;
}

static bool requestmaster(const char *req)
{
    if(mastersock == ENET_SOCKET_NULL)
    {
        mastersock = connectmaster();
        if(mastersock == ENET_SOCKET_NULL) return false;
    }

    masterout.put(req, strlen(req));
    return true;
}

bool requestmasterf(const char *fmt, ...)
{
    defvformatstring(req, fmt, fmt);
    return requestmaster(req);
}

static void processmasterinput()
{
    if(masterinpos >= masterin.length()) return;

    char *input = &masterin[masterinpos], *end = (char *)memchr(input, '\n', masterin.length() - masterinpos);
    while(end)
    {
        *end++ = '\0';

        const char *args = input;
        while(args < end && !isspace(*args)) args++;
        int cmdlen = args - input;
        while(args < end && isspace(*args)) args++;

        if(!strncmp(input, "failreg", cmdlen))
            logline(ACLOG_WARNING, "master server registration failed: %s", args);
        else if(!strncmp(input, "succreg", cmdlen))
        {
            logline(ACLOG_INFO, "master server registration succeeded");
        }
        else processmasterinput(input, cmdlen, args);

        masterinpos = end - masterin.getbuf();
        input = end;
        end = (char *)memchr(input, '\n', masterin.length() - masterinpos);
    }

    if(masterinpos >= masterin.length())
    {
        masterin.setsize(0);
        masterinpos = 0;
    }
}

static void flushmasteroutput()
{
    if(masterout.empty()) return;

    ENetBuffer buf;
    buf.data = &masterout[masteroutpos];
    buf.dataLength = masterout.length() - masteroutpos;
    int sent = enet_socket_send(mastersock, NULL, &buf, 1);
    if(sent >= 0)
    {
        masteroutpos += sent;
        if(masteroutpos >= masterout.length())
        {
            masterout.setsize(0);
            masteroutpos = 0;
        }
    }
    else disconnectmaster();
}

static void flushmasterinput()
{
    if(masterin.length() >= masterin.capacity())
        masterin.reserve(4096);

    ENetBuffer buf;
    buf.data = &masterin[masterin.length()];
    buf.dataLength = masterin.capacity() - masterin.length();
    int recv = enet_socket_receive(mastersock, NULL, &buf, 1);
    if(recv > 0)
    {
        masterin.advance(recv);
        processmasterinput();
    }
    else disconnectmaster();
}


// send alive signal to masterserver after 40 minutes of uptime and if currently in intermission (so theoretically <= 1 hour)
// TODO?: implement a thread to drop this "only in intermission" business, we'll need it once AUTH gets active!
static inline void updatemasterserver(int millis, int port)
{
    if(!lastupdatemaster || ((millis-lastupdatemaster)>40*60*1000 && (interm || !totalclients)))
    {
        char servername[30]; memset(servername,'\0',30); filtertext(servername, global_name, FTXT__GLOBALNAME, 20);
        if(mastername[0]) requestmasterf("regserv %d %s %d\n", port, servername[0] ? servername : "noname", AC_VERSION);
        lastupdatemaster = millis + 1;
    }
}

static ENetSocket pongsock = ENET_SOCKET_NULL, lansock = ENET_SOCKET_NULL;

void serverms(int mode, int numplayers, int minremain, char *smapname, int millis, const ENetAddress &localaddr, int *mnum, int *msend, int *mrec, int *cnum, int *csend, int *crec, int protocol_version)
{
    flushmasteroutput();
    updatemasterserver(millis, localaddr.port);

    static ENetSocketSet sockset;
    ENET_SOCKETSET_EMPTY(sockset);
    ENetSocket maxsock = pongsock;
    ENET_SOCKETSET_ADD(sockset, pongsock);
    if(mastersock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, mastersock);
        ENET_SOCKETSET_ADD(sockset, mastersock);
    }
    if(lansock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, lansock);
        ENET_SOCKETSET_ADD(sockset, lansock);
    }
    if(enet_socketset_select(maxsock, &sockset, NULL, 0) <= 0) return;

    // reply all server info requests
    static uchar data[MAXTRANS];
    ENetBuffer buf;
    ENetAddress addr;
    buf.data = data;
    int len;

    loopi(2)
    {
        ENetSocket sock = i ? lansock : pongsock;
        if(sock == ENET_SOCKET_NULL || !ENET_SOCKETSET_CHECK(sockset, sock)) continue;

        buf.dataLength = sizeof(data);
        len = enet_socket_receive(sock, &addr, &buf, 1);
        if(len < 0) continue;

        // ping & pong buf
        ucharbuf pi(data, len), po(&data[len], sizeof(data)-len);
        bool std = false;
        if(getint(pi) != 0) // std pong
        {
            extern struct servercommandline scl;
            (*mnum)++; *mrec += len; std = true;
            putint(po, protocol_version);
            putint(po, mode);
            putint(po, numplayers);
            putint(po, minremain);
            sendstring(smapname, po);
            sendstring(servdesc_current, po);
            putint(po, scl.maxclients);
            putint(po, getpongflags(addr.host));
            if(pi.remaining())
            {
                int query = getint(pi);
                switch(query)
                {
                    case EXTPING_NAMELIST:
                    {
                        putint(po, query);
                        extping_namelist(po);
                        break;
                    }
                    case EXTPING_SERVERINFO:
                    {
                        putint(po, query);
                        extping_serverinfo(pi, po);
                        break;
                    }
                    case EXTPING_MAPROT:
                    {
                        putint(po, query);
                        extping_maprot(po);
                        break;
                    }
                    case EXTPING_UPLINKSTATS:
                    {
                        putint(po, query);
                        extping_uplinkstats(po);
                        break;
                    }
                    case EXTPING_NOP:
                    default:
                        putint(po, EXTPING_NOP);
                        break;
                }
            }
        }
        else // ext pong - additional server infos
        {
            (*cnum)++; *crec += len;
            int extcmd = getint(pi);
            putint(po, EXT_ACK);
            putint(po, EXT_VERSION);

            switch(extcmd)
            {
                case EXT_UPTIME:        // uptime in seconds
                {
                    putint(po, uint(millis)/1000);
                    break;
                }

                case EXT_PLAYERSTATS:   // playerstats
                {
                    int cn = getint(pi);     // get requested player, -1 for all
                    if(!valid_client(cn) && cn != -1)
                    {
                        putint(po, EXT_ERROR);
                        break;
                    }
                    putint(po, EXT_ERROR_NONE);              // add no error flag

                    int bpos = po.length();                  // remember buffer position
                    putint(po, EXT_PLAYERSTATS_RESP_IDS);    // send player ids following
                    extinfo_cnbuf(po, cn);
                    *csend += int(buf.dataLength = len + po.length());
                    enet_socket_send(pongsock, &addr, &buf, 1); // send all available player ids
                    po.len = bpos;

                    extinfo_statsbuf(po, cn, bpos, pongsock, addr, buf, len, csend);
                    return;
                }

                case EXT_TEAMSCORE:
                    extinfo_teamscorebuf(po);
                    break;

                default:
                    putint(po,EXT_ERROR);
                    break;
            }
        }

        buf.dataLength = len + po.length();
        enet_socket_send(pongsock, &addr, &buf, 1);
        if(std) *msend += (int)buf.dataLength;
        else *csend += (int)buf.dataLength;
    }

    if(mastersock != ENET_SOCKET_NULL && ENET_SOCKETSET_CHECK(sockset, mastersock)) flushmasterinput();
}

// this function should be made better, because it is used just ONCE (no need of so much parameters)
void servermsinit(const char *master, const char *ip, int infoport, bool listen)
{
    copystring(mastername, master);
    disconnectmaster();

    if(listen)
    {
        ENetAddress address = { ENET_HOST_ANY, (enet_uint16)infoport };
        if(*ip)
        {
            if(enet_address_set_host(&address, ip)<0) logline(ACLOG_WARNING, "server ip not resolved");
            else serveraddress.host = address.host;
        }
        pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0)
        {
            enet_socket_destroy(pongsock);
            pongsock = ENET_SOCKET_NULL;
        }
        if(pongsock == ENET_SOCKET_NULL) fatal("could not create server info socket");
        else enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
        address.port = CUBE_SERVINFO_PORT_LAN;
        lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if(lansock != ENET_SOCKET_NULL && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0))
        {
            enet_socket_destroy(lansock);
            lansock = ENET_SOCKET_NULL;
        }
        if(lansock == ENET_SOCKET_NULL) logline(ACLOG_WARNING, "could not create LAN server info socket");
        else enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);
    }
}







bool servercommandline::checkarg(const char *arg)
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

void serverconfigfile::init(const char *name)
{
    copystring(filename, name);
    path(filename);
    read();
}

bool serverconfigfile::load()
{
    DELETEA(buf);
    buf = loadfile(filename, &filelen);
    if(!buf)
    {
        logline(ACLOG_INFO,"could not read config file '%s'", filename);
        return false;
    }
    char *p;
    if('\r' != '\n') // this is not a joke!
    {
        char c = strchr(buf, '\n') ? ' ' : '\n'; // in files without /n substitute /r with /n, otherwise remove /r
        for(p = buf; (p = strchr(p, '\r')); p++) *p = c;
    }
    for(p = buf; (p = strstr(p, "//")); ) // remove comments
    {
        while(*p != '\n' && *p != '\0') p++[0] = ' ';
    }
    for(p = buf; (p = strchr(p, '\t')); p++) *p = ' ';
    for(p = buf; (p = strchr(p, '\n')); p++) *p = '\0'; // one string per line
    return true;
}

