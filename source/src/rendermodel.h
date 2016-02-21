#ifndef SOURCE_SRC_RENDERMODEL_H_
#define SOURCE_SRC_RENDERMODEL_H_

#include "model.h"
#include "texture.h"

class playerent;

mapmodelinfo &getmminfo ( int i );
model *loadmodel ( const char *name, int i = -1, bool trydl = false );
void cleanupmodels ();
void startmodelbatches ();
void clearmodelbatches ();
void endmodelbatches ( bool flush = true );
void rendermodel ( const char *mdl,
                   int anim,
                   int tex,
                   float rad,
                   const vec &o,
                   float yaw,
                   float pitch,
                   float speed = 0,
                   int basetime = 0,
                   playerent *d = NULL,
                   modelattach *a = NULL,
                   float scale = 1.0f );
int findanim ( const char *name );
void loadskin ( const char *dir, const char *altdir, Texture *&skin );
void preload_playermodels ();
void preload_entmodels ();
void preload_mapmodels ( bool trydl = false );
void renderclient ( playerent *d,
                    const char *mdlname,
                    const char *vwepname,
                    int tex = 0 );
void updateclientname ( playerent *d );
void renderclient ( playerent *d );
void renderclients ();

#endif /* SOURCE_SRC_RENDERMODEL_H_ */
