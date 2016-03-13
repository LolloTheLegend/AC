#ifndef WORLDLIGHT_H_
#define WORLDLIGHT_H_

#include "entities.h"
#include "tools.h"
#include "world.h"

extern int lastcalclight;

void fullbrightlight ( int level = -1 );
void calclight ();
void adddynlight ( physent *owner,
                   const vec &o,
                   int reach,
                   int expire,
                   int fade,
                   uchar r,
                   uchar g = 0,
                   uchar b = 0 );
void cleardynlights ();
void removedynlights ( physent *owner );
void dodynlights ();
void undodynlights ();
block *blockcopy ( const block &b );
void blockpaste ( const block &b, int bx, int by, bool light );
void blockpaste ( const block &b );
void freeblockp ( block *b );
void freeblock ( block *&b );
block *duplicateblock ( const block *s );

#endif /* WORLDLIGHT_H_ */
