#ifndef SERVERMS_H_
#define SERVERMS_H_

#include <enet/enet.h>

#include "common.h"
#include "entities.h"
#include "tools.h"

struct serverconfigfile
{
	string filename;
	int filelen;
	char *buf;
	serverconfigfile () :
			filelen(0), buf(NULL)
	{
		filename[0] = '\0';
	}
	virtual ~serverconfigfile ()
	{
		DELETEA(buf);
	}

	virtual void read ()
	{
	}
	void init ( const char *name );
	bool load ();
};

#define DEFDEMOFILEFMT "%w_%h_%n_%Mmin_%G"
#define DEFDEMOTIMEFMT "%Y%m%d_%H%M"

struct servercommandline
{
	int uprate, serverport, syslogfacility, filethres, syslogthres, maxdemos,
	        maxclients, kickthreshold, banthreshold, verbose, incoming_limit,
	        afk_limit, ban_time, demotimelocal;
	const char *ip, *master, *logident, *serverpassword, *adminpasswd,
	        *demopath, *maprot, *pwdfile, *blfile, *nbfile, *infopath,
	        *motdpath, *forbidden, *killmessages, *demofilenameformat,
	        *demotimestampformat;
	bool logtimestamp, demo_interm, loggamestatus;
	string motd, servdesc_full, servdesc_pre, servdesc_suf, voteperm, mapperm;
	int clfilenesting;
	vector<const char *> adminonlymaps;

	servercommandline () :
			uprate(0), serverport(CUBE_DEFAULT_SERVER_PORT), syslogfacility(6),
			        filethres(-1), syslogthres(-1), maxdemos(5),
			        maxclients(DEFAULTCLIENTS), kickthreshold(-5),
			        banthreshold(-6), verbose(0), incoming_limit(10),
			        afk_limit(45000), ban_time(20 * 60 * 1000),
			        demotimelocal(0), ip(""), master(NULL), logident(""),
			        serverpassword(""), adminpasswd(""), demopath(""),
			        maprot("config/maprot.cfg"),
			        pwdfile("config/serverpwd.cfg"),
			        blfile("config/serverblacklist.cfg"),
			        nbfile("config/nicknameblacklist.cfg"),
			        infopath("config/serverinfo"), motdpath("config/motd"),
			        forbidden("config/forbidden.cfg"),
			        killmessages("config/serverkillmessages.cfg"),
			        logtimestamp(false), demo_interm(false),
			        loggamestatus(true), clfilenesting(0)
	{
		motd[0] = servdesc_full[0] = servdesc_pre[0] = servdesc_suf[0] =
		        voteperm[0] = mapperm[0] = '\0';
		demofilenameformat = DEFDEMOFILEFMT;
		demotimestampformat = DEFDEMOTIMEFMT;
	}

	bool checkarg ( const char *arg );
};

extern int masterport, mastertype;
extern string mastername;

ENetSocket connectmaster ();
bool requestmasterf ( const char *fmt, ... );   // for AUTH et al
void serverms ( int mode,
                int numplayers,
                int minremain,
                char *smapname,
                int millis,
                const ENetAddress &localaddr,
                int *mnum,
                int *msend,
                int *mrec,
                int *cnum,
                int *csend,
                int *crec,
                int protocol_version );
void servermsinit ( const char *master,
                    const char *ip,
                    int serverport,
                    bool listen );

#endif /* SERVERMS_H_ */
