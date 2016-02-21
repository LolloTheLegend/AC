#ifndef SCOREBOARD_H_
#define SCOREBOARD_H_

#include "tools.h"
#include "entities.h"

struct discscore { int team, flags, frags, deaths, points; char name[MAXNAMELEN + 1]; };

extern bool needscoresreorder;
extern vector<discscore> discscores;
extern void* scoremenu;

void showscores(bool on);
void teamflagscores(int &team1, int &team2);
void renderscores(void *menu, bool init);
const char *asciiscores(bool destjpg = false);
void consolescores();

#endif /* SCOREBOARD_H_ */
