#ifndef SOURCE_SRC_CLIENT_H_
#define SOURCE_SRC_CLIENT_H_

#include <enet/enet.h>
#include <stddef.h>
#include "tools.h"

class playerent;
struct authkey;

extern int connected;
extern char* curdemofile;
extern ENetHost *clienthost;
extern ENetPeer *curpeer;
extern string demosubpath;
extern int autodownload;
extern bool sendmapidenttoserver;
extern bool canceldownloads;

int getclientnum ();
bool multiplayer ( bool msg = true );
bool allowedittoggle ();
void abortconnect ();
void connectserv ( char* servername, int* serverport, char* password );
void disconnect ( int onlyclean = 0, int async = 0 );
void trydisconnect ();
void toserver ( char *text );
void cleanupclient ();
void addmsg ( int type, const char *fmt = NULL, ... );
void sendpackettoserv ( int chan, ENetPacket *packet );
void c2skeepalive ();
void c2sinfo ( playerent *d );
int getbuildtype ();
void sendintro ();
void gets2c ();
authkey *findauthkey ( const char *desc );
bool tryauth ( const char *desc );
bool securemapcheck ( const char *map, bool msg = true );
void getmap ( char *name = NULL, char *callback = NULL );
void getdemo ( int *idx, char *dsp );
void listdemos ();
void setupcurl ();
size_t write_callback ( void *ptr, size_t size, size_t nmemb, FILE *stream );
int downloadpackages ();
bool requirepackage ( int type, const char *path );
void writepcksourcecfg ();
void sortpckservers ();

#endif /* SOURCE_SRC_CLIENT_H_ */
