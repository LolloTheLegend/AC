#ifndef SOURCE_SRC_PHYSICS_H_
#define SOURCE_SRC_PHYSICS_H_

#include "geom.h"

class playerent;
class physent;
class bounceent;

// TODO: Lollo fix these when rendergl.cpp and rendergl.h is refactored
extern physent* camera1;

extern bool senst;

float raycube(const vec &o, const vec &ray, vec &surface);
bool raycubelos(const vec &from, const vec &to, float margin = 0);
bool objcollide(physent *d, const vec &objpos, float objrad, float objheight);
bool collide(physent *d, bool spawn = false, float drop = 0, float rise = 0, int level = 7);
void clamproll(physent *pl);
void moveplayer(physent *pl, int moveres, bool local, int curtime);
void physicsframe();
void moveplayer(physent *pl, int moveres, bool local);
void movebounceent(bounceent *p, int moveres, bool local);
void attack(bool on);
void updatecrouch(playerent *p, bool on);
void fixcamerarange(physent *cam = camera1);
int tsens(int x);
void mousemove(int dx, int dy);
void entinmap(physent *d);
void vecfromyawpitch(float yaw, float pitch, int move, int strafe, vec &m);
void vectoyawpitch(const vec &v, float &yaw, float &pitch);

#endif /* SOURCE_SRC_PHYSICS_H_ */
