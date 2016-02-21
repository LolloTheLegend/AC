#ifndef RENDERPARTICLES_H_
#define RENDERPARTICLES_H_

#include "geom.h"
#include "entities.h"

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

void particleinit();
void particlereset();
void cleanupparticles();
void render_particles(int time, int typemask = ~0);
void particle_emit(int type, int *args, int basetime, int seed, const vec &p);
void particle_flash(int type, float scale, float angle, const vec &p);
void particle_splash(int type, int num, int fade, const vec &p);
void particle_cube(int type, int num, int fade, int x, int y);
void particle_trail(int type, int fade, const vec &from, const vec &to);
void particle_fireball(int type, const vec &o);
bool addbullethole(dynent *d, const vec &from, const vec &to, float radius = 1, bool noisy = true, int type = 0); // shotty
bool addscorchmark(vec &o, float radius = 7);
void addshotline(dynent *d, const vec &from, const vec &to);

#endif /* RENDERPARTICLES_H_ */
