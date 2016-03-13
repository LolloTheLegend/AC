/*
 * clients2c.h
 *
 *  Created on: Feb 20, 2016
 *      Author: edav
 */

#ifndef CLIENTS2C_H_
#define CLIENTS2C_H_

#include "entities.h"
#include "tools.h"

struct packetqueue
{
	ringbuf<ENetPacket *, 8> packets;

	packetqueue ();
	~packetqueue ();
	void queue ( ENetPacket *p );
	bool flushtolog ( const char *logfile );
	void clear ();
};

extern void* downloaddemomenu;
extern packetqueue pktlogger;
extern int MA, Mhits;
extern int maxrollremote;
extern char *mlayout;
extern int Mv, Ma, F2F;
extern float Mh;
extern bool item_fail;
extern int lastspawn;
extern int voicecomsounds;
extern bool medals_arrived;
extern medalsst a_medals[END_MDS];

bool good_map ();
void servertoclient ( int chan, uchar *buf, int len, bool demo = false );
void localservertoclient ( int chan, uchar *buf, int len, bool demo = false );
void neterr ( const char *s );

#endif /* CLIENTS2C_H_ */
