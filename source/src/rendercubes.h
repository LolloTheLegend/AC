/*
 * rendercubes.h
 *
 *  Created on: Feb 20, 2016
 *      Author: edav
 */

#ifndef RENDERCUBES_H
#define RENDERCUBES_H

#include "tools.h"
#include "world.h"

extern vector<vertex> verts;
extern int nquads;

extern void setupstrips ();
extern void renderstripssky ();
extern void renderstrips ();
void mipstats ( const int a[] );
bool editfocusdetails ( sqr *s );
void render_flat ( int tex,
                   int x,
                   int y,
                   int size,
                   int h,
                   sqr *l1,
                   sqr *l2,
                   sqr *l3,
                   sqr *l4,
                   bool isceil );
void render_flatdelta ( int wtex,
                        int x,
                        int y,
                        int size,
                        float h1,
                        float h2,
                        float h3,
                        float h4,
                        sqr *l1,
                        sqr *l2,
                        sqr *l3,
                        sqr *l4,
                        bool isceil );
void render_square ( int wtex,
                     float floor1,
                     float floor2,
                     float ceil1,
                     float ceil2,
                     int x1,
                     int y1,
                     int x2,
                     int y2,
                     int size,
                     sqr *l1,
                     sqr *l2,
                     bool topleft,
                     int dir );
void render_tris ( int x,
                   int y,
                   int size,
                   bool topleft,
                   sqr *h1,
                   sqr *h2,
                   sqr *s,
                   sqr *t,
                   sqr *u,
                   sqr *v );
void resetcubes ();
void rendershadow ( int x,
                    int y,
                    int xs,
                    int ys,
                    const vec &texgenS,
                    const vec &texgenT );

#endif /* SOURCE_SRC_RENDERCUBES_H_ */
