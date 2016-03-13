#ifndef SHADOW_H_
#define SHADOW_H_

#include <GL/gl.h>

#include "geom.h"

extern int stenciling, stencilshadow;

bool addshadowbox ( const vec &bbmin,
                    const vec &bbmax,
                    const vec &extrude,
                    const glmatrixf &mat );
void drawstencilshadows ();

#endif /* SHADOW_H_ */
