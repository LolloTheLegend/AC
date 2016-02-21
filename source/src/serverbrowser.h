#ifndef SERVERBROWSER_H_
#define SERVERBROWSER_H_

#include "tools.h"
#include "protocol.h"
#include "common.h"
#include <enet/enet.h>


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

extern int searchlan;
extern void *servmenu, *searchmenu, *serverinfomenu;
extern bool cllock, clfail;

bool resolverwait(const char *name, ENetAddress *address);
int connectwithtimeout(ENetSocket sock, const char *hostname, ENetAddress &remoteaddress);
char *getservername(int n);
serverinfo *getconnectedserverinfo();
void addserver(const char *servername, int serverport, int weight);
void pingservers();
void refreshservers(void *menu, bool init);
bool serverskey(void *menu, int code, bool isdown, int unicode);
bool serverinfokey(void *menu, int code, bool isdown, int unicode);
void updatefrommaster(int force);
void writeservercfg();

#endif /* SERVERBROWSER_H_ */
