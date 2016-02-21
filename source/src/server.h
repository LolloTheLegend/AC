// server.h

#ifndef SERVER_H
#define SERVER_H

#include "tools.h"
#include "common.h"
#include "serverms.h"
#include "entities.h"

#include <enet/enet.h>

extern servercommandline scl;
extern const char *entnames[MAXENTTYPES];
extern itemstat ammostats[NUMGUNS];
extern itemstat powerupstats[I_ARMOUR-I_HEALTH+1];
extern guninfo guns[NUMGUNS];
extern const char *teamnames[TEAM_NUM+1];
extern const char *teamnames_s[TEAM_NUM+1];
extern char killmessages[2][NUMGUNS][MAXKILLMSGLEN];
extern int Mvolume, Marea, SHhits, Mopen;
extern float Mheight;
extern bool isdedicated;
extern int laststatus, servmillis, lastfillup;
extern string servdesc_current;
extern string smapname;
extern mapstats smapstats;
extern char *maplayout, *testlayout;
extern int smode, servmillis;
extern int interm;
extern int maplayout_factor, testlayout_factor, maplayoutssize;
extern char *global_name;
extern int totalclients;
extern string demofilenameformat;
extern string demotimestampformat;
extern int demotimelocal;
extern int demoprotocol;
extern bool watchingdemo;

bool valid_client(int cn);
void restoreserverstate(vector<entity> &ents);
void enddemoplayback();
int checkarea(int maplayout_factor, char *maplayout);
bool serverpickup(int i, int sender);
const char *disc_reason(int reason);
void localclienttoserver(int chan, ENetPacket *);
void serverslice(uint timeout);
void cleanupserver();
int getpongflags(enet_uint32 ip);
void localconnect();
void localdisconnect();
void processmasterinput(const char *cmd, int cmdlen, const char *args);
void extping_uplinkstats(ucharbuf &po);
void extping_namelist(ucharbuf &p);
void extping_serverinfo(ucharbuf &pi, ucharbuf &po);
void extping_maprot(ucharbuf &po);
void extinfo_cnbuf(ucharbuf &p, int cn);
void extinfo_statsbuf(ucharbuf &p, int pid, int bpos, ENetSocket &pongsock, ENetAddress &addr, ENetBuffer &buf, int len, int *csend);
void extinfo_teamscorebuf(ucharbuf &p);
void initserver(bool dedicated, int argc = 0, char **argv = NULL);
void fatal(const char *s, ...);

#endif	// SERVER_H

