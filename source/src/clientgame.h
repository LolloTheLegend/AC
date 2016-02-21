#ifndef SOURCE_SRC_CLIENTGAME_H_
#define SOURCE_SRC_CLIENTGAME_H_

#include <entities.h>
#include "tools.h"
#include "protocol.h"

class playerent;
class botent;

struct serverstate { int autoteam; int mastermode; int matchteamsize; void reset() { autoteam = mastermode = matchteamsize = 0; }};

struct votedisplayinfo
{
    playerent *owner;
    int type, stats[VOTE_NUM], result, millis;
    string desc;
    bool localplayervoted;
    votedisplayinfo() : owner(NULL), result(VOTE_NEUTRAL), millis(0), localplayervoted(false) { loopi(VOTE_NUM) stats[i] = VOTE_NEUTRAL; }
};

extern int nextmode;
extern int gamemode;
extern int modeacronyms;
extern flaginfo flaginfos[2];
extern bool intermission;
extern int arenaintermission;
extern struct serverstate servstate;
extern playerent *player1;              // special client ent that receives input and acts as camera
extern vector<playerent *> players;     // all the other clients (in multiplayer)
extern int lastmillis, totalmillis, nextmillis;
extern int lasthit;
extern int curtime;
extern string clientmap;
extern int spawnpermission;
extern int lastpm;
extern int minutesremaining, gametimecurrent, gametimemaximum, lastgametimeupdate;
extern votedisplayinfo *curvote;
extern int sessionid;
extern void *kickmenu, *banmenu, *forceteammenu, *giveadminmenu;




char* getclientmap();
int getclientmode();
void setskin(playerent *pl, int skin, int team = -1);
char *colorname(playerent *d, char *name = NULL, const char *prefix = "");
char *colorping(int ping);
char *colorpj(int pj);
const char *highlight(const char *text);
void newteam(char *name);
void deathstate(playerent *pl);
void spawnstate(playerent *d);
playerent *newplayerent();
botent *newbotent();
void freebotent(botent *d);
void zapplayer(playerent *&d);
void addsleep(int msec, const char *cmd, bool persist = false);
void resetsleep(bool force = false);
void updateworld(int curtime, int lastmillis);
void gotoplayerstart(playerent *d, entity *e);
void findplayerstart(playerent *d, bool mapcenter = false, int arenaspawn = -1);
void spawnplayer(playerent *d);
void respawnself();
bool tryrespawn();
void dodamage(int damage, playerent *pl, playerent *actor, int gun = -1, bool gib = false, bool local = true);
void dokill(playerent *pl, playerent *act, bool gib = false, int gun = 0);
void silenttimeupdate(int milliscur, int millismax);
void timeupdate(int milliscur, int millismax); // was (int timeremain);
playerent *newclient(int cn);
playerent *getclient(int cn);
void initclient();
void zapplayerflags(playerent *owner);
void preparectf(bool cleanonly = false);
void resetmap(bool mrproper = true);
void startmap(const char *name, bool reset = true, bool norespawn = false);
void flagmsg(int flag, int message, int actor, int flagtime);
char *votestring(int type, char *arg1, char *arg2, char *arg3);
votedisplayinfo *newvotedisplayinfo(playerent *owner, int type, const char *arg1, const char *arg2, const char *arg3 = "");
void callvote(int type, const char *arg1 = NULL, const char *arg2 = NULL, const char *arg3 = NULL);
int vote(int v);
void displayvote(votedisplayinfo *v);
void voteresult(int v);
void callvotesuc();
void callvoteerr(int e);
void votecount(int v);
void clearvote();
void cleanplayervotes(playerent *owner);
void refreshsopmenu(void *menu, bool init);
playerent *updatefollowplayer(int shiftdirection = 0);
void spectatemode(int mode);
void togglespect();

#endif /* SOURCE_SRC_CLIENTGAME_H_ */
