// audio interface of the engine

#include "cube.h"

#define DEBUGCOND (audiodebug==1)

VARF(audio, 0, 1, 1,
     initwarning("sound configuration", INIT_RESET, CHANGE_SOUND));
VARP(audiodebug, 0, 0, 1);
static char *musicdonecmd = NULL;

VARFP(musicvol, 0, 128, 255, audiomgr.setmusicvol(musicvol));

// audio manager

audiomanager::audiomanager ()
{
	nosound = true;
	currentpitch = 1.0f;
	device = NULL;
	context = NULL;
}

void audiomanager::initsound ()
{
	if ( !audio ) {
		conoutf("audio is disabled");
		return;
	}

	nosound = true;
	device = NULL;
	context = NULL;

	// list available devices
	if ( alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") ) {
		const ALCchar *devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
		if ( devices ) {
			string d;
			copystring(d, "Audio devices: ");

			// null separated device string
			for ( const ALchar *c = devices; *c; c += strlen(c) + 1 ) {
				if ( c != devices ) concatstring(d, ", ");
				concatstring(d, c);
			}
			conoutf("%s", d);
		}
	}

	// open device
	const char *devicename = getalias("openaldevice");
	device = alcOpenDevice(devicename && devicename[0] ? devicename : NULL);

	if ( device ) {
		context = alcCreateContext(device, NULL);
		if ( context ) {
			alcMakeContextCurrent(context);

			alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);

			// backend infos
			conoutf("Sound: %s / %s (%s)",
			        alcGetString(device, ALC_DEVICE_SPECIFIER),
			        alGetString(AL_RENDERER), alGetString(AL_VENDOR));
			conoutf("Driver: %s", alGetString(AL_VERSION));

			// allocate OpenAL resources
			sourcescheduler::instance().init(16);

			// let the stream get the first source from the scheduler
			gamemusic = new oggstream();
			if ( !gamemusic->valid ) DELETEP(gamemusic);
			setmusicvol(musicvol);

			nosound = false;
		}
	}

	if ( nosound ) {
		ALERR;
		if ( context ) alcDestroyContext(context);
		if ( device ) alcCloseDevice(device);
		conoutf("sound initialization failed!");
	}
}

void audiomanager::music ( char *name, int millis, char *cmd )
{
	if ( nosound ) return;
	stopsound();
	if ( musicvol && *name ) {
		if ( cmd[0] ) musicdonecmd = newstring(cmd);

		if ( gamemusic->open(name) ) {
			// fade
			if ( millis > 0 ) {
				const int fadetime = 1000;
				gamemusic->fadein(lastmillis, fadetime);
				gamemusic->fadeout(lastmillis + millis, fadetime);
			}

			// play
			bool loop = cmd && cmd[0];
			if ( !gamemusic->playback(loop) ) {
				conoutf("could not play music: %s", name);
				return;
			}
			setmusicvol(musicvol);
		}
		else
			conoutf("could not open music: %s", name);
	}
}

void audiomanager::musicpreload ( int id )
{
	if ( nosound ) return;
	stopsound();
	if ( musicvol && ( id >= M_FLAGGRAB && id <= M_LASTMINUTE2 ) ) {
		char *name = musics[id];
		conoutf("preloading music #%d : %s", id, name);
		if ( gamemusic->open(name) ) {
			defformatstring(whendone)("musicvol %d", musicvol);
			musicdonecmd = newstring(whendone);
			//conoutf("when done: %s", musicdonecmd);
			const int preloadfadetime = 3;
			gamemusic->fadein(lastmillis, preloadfadetime);
			gamemusic->fadeout(lastmillis + 2 * preloadfadetime,
			                   preloadfadetime);
			if ( !gamemusic->playback(false) ) {
				conoutf("could not play music: %s", name);
				return;
			}
			setmusicvol(1);   // not 0 !
		}
		else
			conoutf("could not open music: %s", name);
	}
	else
		setmusicvol(musicvol);   // call "musicpreload -1" to ensure musicdonecmd runs - but it should w/o that
}

void audiomanager::musicsuggest ( int id, int millis, bool rndofs )   // play bg music if nothing else is playing
{
	if ( nosound || !gamemusic ) return;
	if ( gamemusic->playing() ) return;
	if ( !musicvol ) return;
	if ( !musics.inrange(id) ) {
		conoutf("\f3music %d not registered", id);
		return;
	}
	char *name = musics[id];
	if ( gamemusic->open(name) ) {
		gamemusic->fadein(lastmillis, 1000);
		gamemusic->fadeout(millis ? lastmillis + millis : 0, 1000);
		if ( rndofs )
		    gamemusic->seek(
		            millis ? (double) rnd(millis) / 2.0f : (double) lastmillis);
		if ( !gamemusic->playback(rndofs) )
		    conoutf("could not play music: %s", name);
	}
	else
		conoutf("could not open music: %s", name);
}

void audiomanager::musicfadeout ( int id )
{
	if ( nosound || !gamemusic ) return;
	if ( !gamemusic->playing() || !musics.inrange(id) ) return;
	if ( !strcmp(musics[id], gamemusic->name) )
	    gamemusic->fadeout(lastmillis + 1000, 1000);
}

void audiomanager::setmusicvol ( int musicvol )
{
	if ( gamemusic ) gamemusic->setvolume(musicvol > 0 ? musicvol / 255.0f : 0);
}

void audiomanager::setlistenervol ( int vol )
{
	if ( !nosound ) alListenerf(AL_GAIN, vol / 255.0f);
}

void audiomanager::registermusic ( char *name )
{
	if ( nosound || !musicvol ) return;
	if ( !name || !name[0] ) return;
	musics.add(newstring(name));
}

int audiomanager::findsound ( char *name, int vol, vector<soundconfig> &sounds )
{
	loopv(sounds)
	{
		if ( !strcmp(sounds[i].buf->name, name) && ( !vol
		        || sounds[i].vol == vol ) ) return i;
	}
	return -1;
}

int audiomanager::addsound ( char *name,
                             int vol,
                             int maxuses,
                             bool loop,
                             vector<soundconfig> &sounds,
                             bool load,
                             int audibleradius )
{
	if ( nosound ) return -1;
	if ( !soundvol ) return -1;

	// check if the sound was already registered
	int index = findsound(name, vol, sounds);
	if ( index > -1 ) return index;

	sbuffer *b = bufferpool.find(name);
	if ( !b ) {
		conoutf("\f3failed to allocate sample %s", name);
		return -1;
	}

	if ( load && !b->load() ) conoutf("\f3failed to load sample %s", name);

	soundconfig s(b, vol, maxuses, loop, audibleradius);
	sounds.add(s);
	return sounds.length() - 1;
}

void audiomanager::preloadmapsound ( entity &e, bool trydl )
{
	if ( e.type != SOUND || !mapsounds.inrange(e.attr1) ) return;
	sbuffer *buf = mapsounds[e.attr1].buf;
	if ( !buf->load(trydl) && !trydl )
	    conoutf("\f3failed to load sample %s", buf->name);
}

void audiomanager::preloadmapsounds ( bool trydl )
{
	loopv(ents)
	{
		entity &e = ents[i];
		if ( e.type == SOUND ) preloadmapsound(e, trydl);
	}
}

void audiomanager::applymapsoundchanges ()   // during map editing, drop all mapsounds so they can be re-added
{
	loopv(locations)
	{
		location *l = locations[i];
		if ( l && l->ref && l->ref->type == worldobjreference::WR_ENTITY )
		    l->drop();
	}
}

void audiomanager::setchannels ( int num )
{
	if ( !nosound ) sourcescheduler::instance().init(num);
}
;

// called at game exit
void audiomanager::soundcleanup ()
{
	if ( nosound ) return;

	// destroy consuming code
	stopsound();
	DELETEP(gamemusic);
	mapsounds.shrink(0);
	locations.deletecontents();
	gamesounds.shrink(0);

	// kill scheduler
	sourcescheduler::instance().reset();

	bufferpool.clear();

	// shutdown openal
	alcMakeContextCurrent(NULL);
	if ( context ) alcDestroyContext(context);
	if ( device ) alcCloseDevice(device);
}

// clear world-related sounds, called on mapchange
void audiomanager::clearworldsounds ( bool fullclean )
{
	stopsound();
	if ( fullclean ) mapsounds.shrink(0);
	locations.deleteworldobjsounds();
}

void audiomanager::mapsoundreset ()
{
	mapsounds.shrink(0);
	locations.deleteworldobjsounds();
}

static VARP(footsteps, 0, 1, 1);
static VARP(localfootsteps, 0, 1, 1);

void audiomanager::updateplayerfootsteps ( playerent *p )
{
	if ( !p ) return;

	const int footstepradius = 20;

	// find existing footstep sounds
	physentreference ref(p);
	location *locs[] = { locations.find(S_FOOTSTEPS, &ref, gamesounds),
	                     locations.find(S_FOOTSTEPSCROUCH, &ref, gamesounds),
	                     locations.find(S_WATERFOOTSTEPS, &ref, gamesounds),
	                     locations.find(S_WATERFOOTSTEPSCROUCH, &ref,
	                                    gamesounds) };

	bool local = ( p == camera1 );
	bool inrange = footsteps
	        && ( local || ( camera1->o.dist(p->o) < footstepradius ) );

	if ( !footsteps || ( local && !localfootsteps ) || !inrange
	     || p->state != CS_ALIVE || lastmillis - p->lastpain < 300
	     || ( !p->onfloor && p->timeinair > 50 ) || ( !p->move && !p->strafe )
	     || p->inwater ) {
		const int minplaytime = 200;
		loopi(sizeof(locs)/sizeof(locs[0]))
		{
			location *l = locs[i];
			if ( !l ) continue;
			if ( l->playmillis + minplaytime > totalmillis ) continue;   // tolerate short interruptions by enforcing a minimal playtime
			l->drop();
		}
	}
	else {
		// play footsteps

		int grounddist;
		if ( ( (int) p->o.x ) >= 0 && ( (int) p->o.y ) >= 0
		     && ( (int) p->o.x ) < ssize && ( (int) p->o.y ) < ssize ) {   // sam's fix to the sound crash
			grounddist = hdr.waterlevel - S((int)p->o.x, (int)p->o.y)->floor;
		}
		else {
			grounddist = 0;
		}
//        int grounddist = hdr.waterlevel-S((int)p->o.x, (int)p->o.y)->floor;
		bool water = p->o.z - p->eyeheight + 0.25f < hdr.waterlevel;
		if ( water && grounddist > p->eyeheight ) return;   // don't play step sound when jumping into water

		int stepsound;
		if ( p->crouching )
			stepsound = water ? S_WATERFOOTSTEPSCROUCH : S_FOOTSTEPSCROUCH;   // crouch
		else
			stepsound = water ? S_WATERFOOTSTEPS : S_FOOTSTEPS;   // normal

		// proc existing sounds
		bool isplaying = false;
		loopi(sizeof(locs)/sizeof(locs[0]))
		{
			location *l = locs[i];
			if ( !l ) continue;
			if ( i + S_FOOTSTEPS == stepsound )
				isplaying = true;   // already playing
			else
				l->drop();   // different footstep sound, drop it
		}

		if ( !isplaying ) {
			// play
			float rndoffset = float(rnd(500)) / 500.0f;
			audiomgr._playsound(stepsound, ref, local ? SP_HIGH : SP_LOW,
			                    rndoffset);
		}
	}
}

// manage looping sounds
location *audiomanager::updateloopsound ( int sound, bool active, float vol )
{
	location *l = locations.find(sound, NULL, gamesounds);
	if ( !l && active )
		l = _playsound(sound, camerareference(), SP_HIGH, 0.0f, true);
	else if ( l && !active ) l->drop();
	if ( l && vol != 1.0f ) l->src->gain(vol);
	return l;
}

static VARP(mapsoundrefresh, 0, 10, 1000);

void audiomanager::mutesound ( int n, int off )
{
	bool mute = ( off == 0 );
	if ( !gamesounds.inrange(n) ) {
		conoutf("\f3could not %s sound #%d", mute ? "silence" : "unmute", n);
		return;
	}
	gamesounds[n].muted = mute;
}

void audiomanager::unmuteallsounds ()
{
	loopv(gamesounds)
		gamesounds[i].muted = false;
}

int audiomanager::soundmuted ( int n )
{
	return gamesounds.inrange(n) && !gamesounds[n].muted ? 0 : 1;
}

void audiomanager::writesoundconfig ( stream *f )
{
	bool wrotesound = false;
	loopv(gamesounds)
	{
		if ( gamesounds[i].muted ) {
			if ( !wrotesound ) {
				f->printf("// sound settings\n\n");
				wrotesound = true;
			}
			f->printf("mutesound %d\n", i);
		}
	}
}

static void voicecom ( char *sound, char *text )
{
	if ( !sound || !sound[0] ) return;
	if ( !text || !text[0] ) return;
	static int last = 0;
	if ( !last || lastmillis - last > 2000 ) {
		extern int voicecomsounds;
		defformatstring(soundpath)("voicecom/%s", sound);
		int s = audiomgr.findsound(soundpath, 0, gamesounds);
		if ( s < 0 || s < S_AFFIRMATIVE || s >= S_NULL ) return;
		if ( voicecomsounds > 0 ) audiomgr.playsound(s, SP_HIGH);
		if ( s >= S_NICESHOT )   // public
		{
			addmsg(SV_VOICECOM, "ri", s);
			toserver(text);
		}
		else   // team
		{
			addmsg(SV_VOICECOMTEAM, "ri", s);
			defformatstring(teamtext)("%c%s", '%', text);
			toserver(teamtext);
		}
		last = lastmillis;
	}
}

COMMAND(voicecom, "ss");

void soundtest ()
{
	loopi(S_NULL)
		audiomgr.playsound(i, rnd(SP_HIGH + 1));
}

COMMAND(soundtest, "");

// sound configuration

soundconfig::soundconfig ( sbuffer *b,
                           int vol,
                           int maxuses,
                           bool loop,
                           int audibleradius )
{
	buf = b;
	this->vol = vol > 0 ? vol : 100;
	this->maxuses = maxuses;
	this->loop = loop;
	this->audibleradius = audibleradius;
	this->model = audibleradius > 0 ? DM_LINEAR : DM_DEFAULT;   // use linear model when radius is configured
	uses = 0;
	muted = false;
}

void soundconfig::onattach ()
{
	uses++;
}

void soundconfig::ondetach ()
{
	uses--;
}

vector<soundconfig> gamesounds, mapsounds;

void audiomanager::detachsounds ( playerent *owner )
{
	if ( nosound ) return;
	// make all dependent locations static
	locations.replaceworldobjreference(physentreference(owner),
	                                   staticreference(owner->o));
}

VARP(maxsoundsatonce, 0, 32, 100);

location *audiomanager::_playsound ( int n,
                                     const worldobjreference &r,
                                     int priority,
                                     float offset,
                                     bool loop )
{
	if ( nosound || !soundvol ) return NULL;
	if ( soundmuted(n) ) return NULL;
	DEBUGVAR(n);
	DEBUGVAR(priority);

	if ( r.type != worldobjreference::WR_ENTITY ) {
		// avoid bursts of sounds with heavy packetloss and in sp
		static int soundsatonce = 0, lastsoundmillis = 0;
		if ( totalmillis == lastsoundmillis )
			soundsatonce++;
		else
			soundsatonce = 1;
		lastsoundmillis = totalmillis;
		if ( maxsoundsatonce && soundsatonce > maxsoundsatonce ) {
			DEBUGVAR(soundsatonce);
			return NULL;
		}
	}

	location *loc = new location(n, r, priority);
	locations.add(loc);
	if ( !loc->stale ) {
		if ( offset > 0 ) loc->offset(offset);
		if ( currentpitch != 1.0f ) loc->pitch(currentpitch);
		loc->play(loop);
	}

	return loc;
}

void audiomanager::playsound ( int n, int priority )
{
	_playsound(n, camerareference(), priority);
}
void audiomanager::playsound ( int n, physent *p, int priority )
{
	if ( p ) _playsound(n, physentreference(p), priority);
}
void audiomanager::playsound ( int n, entity *e, int priority )
{
	if ( e ) _playsound(n, entityreference(e), priority);
}
void audiomanager::playsound ( int n, const vec *v, int priority )
{
	if ( v ) _playsound(n, staticreference(*v), priority);
}

void audiomanager::playsoundname ( char *s, const vec *loc, int vol )
{
	if ( !nosound ) return;

	if ( vol <= 0 ) vol = 100;
	int id = findsound(s, vol, gamesounds);
	if ( id < 0 ) id = addsound(s, vol, 0, false, gamesounds, true, 0);
	playsound(id, loc, SP_NORMAL);
}

void audiomanager::playsoundc ( int n, physent *p, int priority )
{
	if ( p && p != player1 )
		playsound(n, p, priority);
	else {
		addmsg(SV_SOUND, "i", n);
		playsound(n, priority);
	}
}

void audiomanager::stopsound ()
{
	if ( nosound ) return;
	DELETEA(musicdonecmd);
	if ( gamemusic ) gamemusic->reset();
}

static VARP(heartbeat, 0, 0, 99);

// main audio update routine

void audiomanager::updateaudio ()
{
	if ( nosound ) return;

	alcSuspendContext(context);   // don't process sounds while we mess around

	bool alive = player1->state == CS_ALIVE;
	bool firstperson =
	        camera1 == player1 || ( player1->isspectating()
	                && player1->spectatemode == SM_DEATHCAM );

	// footsteps
	updateplayerfootsteps(player1);
	loopv(players)
	{
		playerent *p = players[i];
		if ( !p ) continue;
		updateplayerfootsteps(p);
	}

	// water
	bool underwater = /*alive &&*/firstperson
	        && hdr.waterlevel > player1->o.z + player1->aboveeye;
	updateloopsound(S_UNDERWATER, underwater);

	// tinnitus
	bool tinnitus = alive && firstperson && player1->eardamagemillis > 0
	                && lastmillis <= player1->eardamagemillis;
	location *tinnitusloc = updateloopsound(S_TINNITUS, tinnitus);

	// heartbeat
	bool heartbeatsound = heartbeat && alive && firstperson && !m_osok
	                      && player1->health <= heartbeat;
	updateloopsound(S_HEARTBEAT, heartbeatsound);

	// pitch fx
	const float lowpitch = 0.65f;
	bool pitchfx = underwater || tinnitus;
	if ( pitchfx && currentpitch != lowpitch ) {
		currentpitch = lowpitch;
		locations.forcepitch(currentpitch);
		if ( tinnitusloc ) tinnitusloc->pitch(1.9f);   // super high pitched tinnitus
	}
	else if ( !pitchfx && currentpitch == lowpitch ) {
		currentpitch = 1.0f;
		locations.forcepitch(currentpitch);
	}

	// update map sounds
	static int lastmapsound = 0;
	if ( !lastmapsound || totalmillis - lastmapsound > mapsoundrefresh
	     || !mapsoundrefresh ) {
		loopv(ents)
		{
			entity &e = ents[i];
			vec o(e.x, e.y, e.z);
			if ( e.type != SOUND ) continue;

			int sound = e.attr1;
			int radius = e.attr2;
			bool hearable = ( radius == 0 || camera1->o.dist(o) < radius );
			entityreference entref(&e);

			// search existing sound loc
			location *loc = locations.find(sound, &entref, mapsounds);

			if ( hearable && !loc )   // play
			{
				_playsound(sound, entref, SP_HIGH, 0.0f, true);
			}
			else if ( !hearable && loc )   // stop
			        {
				loc->drop();
			}
		}
		lastmapsound = totalmillis;
	}

	// update all sound locations
	locations.updatelocations();

	// update background music
	if ( gamemusic ) {
		if ( !gamemusic->update() ) {
			// music ended, exec command
			if ( musicdonecmd ) {
				char *cmd = musicdonecmd;
				musicdonecmd = NULL;
				execute(cmd);
				delete[] cmd;
			}
		}
	}

	// listener
	vec o[2];
	o[0].x = (float) ( cos(RAD * ( camera1->yaw - 90 )) );
	o[0].y = (float) ( sin(RAD * ( camera1->yaw - 90 )) );
	o[0].z = 0.0f;
	o[1].x = o[1].y = 0.0f;
	o[1].z = -1.0f;
	alListenerfv(AL_ORIENTATION, (ALfloat *) &o);
	alListenerfv(AL_POSITION, (ALfloat *) &camera1->o);

	alcProcessContext(context);
}

// binding of sounds to the 3D world

// camera

camerareference::camerareference () :
		worldobjreference(WR_CAMERA)
{
}

worldobjreference *camerareference::clone () const
{
	return new camerareference(*this);
}

const vec &camerareference::currentposition () const
{
	return camera1->o;
}

bool camerareference::nodistance ()
{
	return true;
}

bool camerareference::operator== ( const worldobjreference &other )
{
	return type == other.type;
}

// physent

physentreference::physentreference ( physent *ref ) :
		worldobjreference(WR_PHYSENT)
{
	ASSERT(ref);
	phys = ref;
}

worldobjreference *physentreference::clone () const
{
	return new physentreference(*this);
}

const vec &physentreference::currentposition () const
{
	return phys->o;
}

bool physentreference::nodistance ()
{
	return phys == camera1;
}

bool physentreference::operator== ( const worldobjreference &other )
{
	return type == other.type && phys == ( (physentreference &) other ).phys;
}

// entity

entityreference::entityreference ( entity *ref ) :
		worldobjreference(WR_ENTITY)
{
	ASSERT(ref);
	ent = ref;
}

worldobjreference *entityreference::clone () const
{
	return new entityreference(*this);
}
const vec &entityreference::currentposition () const
{
	static vec tmp;
	tmp = vec(ent->x, ent->y, ent->z);
	return tmp;
}
bool entityreference::nodistance ()
{
	return ent->attr3 > 0;
}
bool entityreference::operator== ( const worldobjreference &other )
{
	return type == other.type && ent == ( (entityreference &) other ).ent;
}

// static

staticreference::staticreference ( const vec &ref ) :
		worldobjreference(WR_STATICPOS)
{
	pos = ref;
}

worldobjreference *staticreference::clone () const
{
	return new staticreference(*this);
}

const vec &staticreference::currentposition () const
{
	return pos;
}

bool staticreference::nodistance ()
{
	return false;
}

bool staticreference::operator== ( const worldobjreference &other )
{
	return type == other.type && pos == ( (staticreference &) other ).pos;
}

// instance

audiomanager audiomgr;

COMMANDF(sound, "i", (int *n) { audiomgr.playsound(*n); });

COMMANDF(applymapsoundchanges, "", (){ audiomgr.applymapsoundchanges(); });

COMMANDF(unmuteallsounds, "", () { audiomgr.unmuteallsounds(); });

COMMANDF(mutesound, "ii", (int *n, int *off) { audiomgr.mutesound(*n, *off); });

COMMANDF(soundmuted, "i", (int *n) { intret(audiomgr.soundmuted(*n)); });

COMMANDF(mapsoundreset, "", () { audiomgr.mapsoundreset(); });

VARF(soundchannels, 4, 32, 1024, audiomgr.setchannels(soundchannels)
; );

VARFP(soundvol, 0, 128, 255, audiomgr.setlistenervol(soundvol)
; );

COMMANDF(
        registersound,
        "siii",
        (char *name, int *vol, int *loop, int *audibleradius) { intret(audiomgr.addsound(name, *vol, -1, *loop != 0, gamesounds, true, *audibleradius)); });

COMMANDF(
        mapsound,
        "si",
        (char *name, int *maxuses) { audiomgr.addsound(name, 255, *maxuses, true, mapsounds, false, 0); });

COMMANDF(registermusic, "s", (char *name) { audiomgr.registermusic(name); });

COMMANDF(musicpreload, "i", (int *id) { audiomgr.musicpreload(*id); });

COMMANDF(
        music,
        "sis",
        (char *name, int *millis, char *cmd) { audiomgr.music(name, *millis, cmd); });

// ogg compat

static size_t oggcallbackread ( void *ptr,
                                size_t size,
                                size_t nmemb,
                                void *datasource )
{
	stream *s = (stream *) datasource;
	return s ? s->read(ptr, int(size * nmemb)) / size : 0;
}

static int oggcallbackseek ( void *datasource, ogg_int64_t offset, int whence )
{
	stream *s = (stream *) datasource;
	return s && s->seek(long(offset), whence) ? 0 : -1;
}

static int oggcallbackclose ( void *datasource )
{
	stream *s = (stream *) datasource;
	if ( !s ) return -1;
	delete s;
	return 0;
}

static long oggcallbacktell ( void *datasource )
{
	stream *s = (stream *) datasource;
	return s ? s->tell() : -1;
}

ov_callbacks oggcallbacks = { oggcallbackread, oggcallbackseek,
                              oggcallbackclose, oggcallbacktell };

// ogg audio streaming

oggstream::oggstream () :
		valid(false), isopen(false), src(NULL)
{
	reset();

	// grab a source and keep it during the whole lifetime
	src = sourcescheduler::instance().newsource(SP_HIGHEST, camera1->o);
	if ( src ) {
		if ( src->valid ) {
			src->init(this);
			src->sourcerelative(true);
		}
		else {
			sourcescheduler::instance().releasesource(src);
			src = NULL;
		}
	}

	if ( !src ) return;

	alclearerr();
	alGenBuffers(2, bufferids);
	valid = !ALERR;
}

oggstream::~oggstream ()
{
	reset();

	if ( src ) sourcescheduler::instance().releasesource(src);

	if ( alIsBuffer(bufferids[0]) || alIsBuffer(bufferids[1]) ) {
		alclearerr();
		alDeleteBuffers(2, bufferids);
		ALERR;
	}
}

void oggstream::reset ()
{
	name[0] = '\0';

	// stop playing
	if ( src ) {
		src->stop();
		src->unqueueallbuffers();
		src->buffer(0);
	}
	format = AL_NONE;

	// reset file handler
	if ( isopen ) {
		isopen = !ov_clear(&oggfile);
	}
	info = NULL;
	totalseconds = 0.0f;

	// default settings
	startmillis = endmillis = startfademillis = endfademillis = 0;
	gain = 1.0f;   // reset gain but not volume setting
	looping = false;
}

bool oggstream::open ( const char *f )
{
	ASSERT(valid);
	if ( !f ) return false;
	if ( playing() || isopen ) reset();

	const char *exts[] = { "", ".wav", ".ogg" };
	string filepath;

	loopi(sizeof(exts)/sizeof(exts[0]))
	{
		formatstring(filepath)("packages/audio/soundtracks/%s%s", f, exts[i]);
		::stream *file = openfile(path(filepath), "rb");
		if(!file) continue;

		isopen = !ov_open_callbacks(file, &oggfile, NULL, 0, oggcallbacks);
		if(!isopen)
		{
			delete file;
			continue;
		}

		info = ov_info(&oggfile, -1);
		format = info->channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
		totalseconds = ov_time_total(&oggfile, -1);
		copystring(name, f);

		return true;
	}
	return false;
}

void oggstream::onsourcereassign ( source *s )
{
	// should NEVER happen because streams do have the highest priority, see constructor
	ASSERT(0);
	if ( src && src == s ) {
		reset();
		src = NULL;
	}
}

bool oggstream::stream ( ALuint bufid )
{
	ASSERT(valid);

	loopi(2)
	{
		char pcm[BUFSIZE];
		ALsizei size = 0;
		int bitstream;
		while ( size < BUFSIZE ) {
			long bytes = ov_read(&oggfile, pcm + size, BUFSIZE - size,
			                     isbigendian(), 2, 1, &bitstream);
			if ( bytes > 0 )
				size += bytes;
			else if ( bytes < 0 )
				return false;
			else
				break;   // done
		}

		if ( size == 0 ) {
			if ( looping && !ov_pcm_seek(&oggfile, 0) )
				continue;   // try again to replay
			else
				return false;
		}

		alclearerr();
		alBufferData(bufid, format, pcm, size, info->rate);
		return !ALERR;
	}

	return false;
}

bool oggstream::update ()
{
	ASSERT(valid);
	if ( !isopen || !playing() ) return false;

	// update buffer queue
	ALint processed;
	bool active = true;
	alGetSourcei(src->id, AL_BUFFERS_PROCESSED, &processed);
	loopi(processed)
	{
		ALuint buffer;
		alSourceUnqueueBuffers(src->id, 1, &buffer);
		active = stream(buffer);
		if ( active ) alSourceQueueBuffers(src->id, 1, &buffer);
	}

	if ( active ) {
		// fade in
		if ( startmillis > 0 ) {
			const float start = ( lastmillis - startmillis )
			        / (float) startfademillis;
			if ( start >= 0.00f && start <= 1.00001f ) {
				setgain(start);
				return true;
			}
		}

		// fade out
		if ( endmillis > 0 ) {
			if ( lastmillis <= endmillis )   // set gain
			{
				const float end = ( endmillis - lastmillis )
				        / (float) endfademillis;
				if ( end >= -0.00001f && end <= 1.00f ) {
					setgain(end);
					return true;
				}
			}
			else   // stop
			{
				active = false;
			}
		}
	}

	if ( !active ) reset();   // reset stream if the end is reached
	return active;
}

bool oggstream::playing ()
{
	ASSERT(valid);
	return src->playing();
}

void oggstream::updategain ()
{
	ASSERT(valid);
	src->gain(gain * volume);
}

void oggstream::setgain ( float g )
{
	ASSERT(valid);
	gain = g;
	updategain();
}

void oggstream::setvolume ( float v )
{
	ASSERT(valid);
	volume = v;
	updategain();
}

void oggstream::fadein ( int startmillis, int fademillis )
{
	ASSERT(valid);
	setgain(0.01f);
	this->startmillis = startmillis;
	this->startfademillis = fademillis;
}

void oggstream::fadeout ( int endmillis, int fademillis )
{
	ASSERT(valid);
	this->endmillis =
	        ( endmillis || totalseconds > 0.0f ) ?
	                endmillis : lastmillis + (int) totalseconds;
	this->endfademillis = fademillis;
}

bool oggstream::playback ( bool looping )
{
	ASSERT(valid);
	if ( playing() ) return true;
	this->looping = looping;
	if ( !stream(bufferids[0]) || !stream(bufferids[1]) ) return false;
	if ( !startmillis && !endmillis && !startfademillis && !endfademillis )
	    setgain(1.0f);

	updategain();
	src->queuebuffers(2, bufferids);
	src->play();

	return true;
}

void oggstream::seek ( double offset )
{
	ASSERT(valid);
	if ( !totalseconds ) return;
	ov_time_seek_page(&oggfile, fmod(totalseconds - 5.0f, totalseconds));
}

// simple OpenAL call wrappers

static VAR(al_referencedistance, 0, 400, 1000000);
static VAR(al_rollofffactor, 0, 100, 1000000);

// represents an OpenAL source, an audio emitter in the 3D world

source::source () :
		id(0), owner(NULL), locked(false), valid(false), priority(SP_NORMAL)
{
	valid = generate();
	ASSERT(!valid || alIsSource(id));
}

source::~source ()
{
	if ( valid ) delete_();
}

void source::lock ()
{
	locked = true;
	DEBUG("source locked, " << lastmillis);
}

void source::unlock ()
{
	locked = false;
	owner = NULL;
	stop();
	buffer(0);
	DEBUG("source unlocked, " << lastmillis);
}

void source::reset ()
{
	ASSERT(alIsSource(id));
	owner = NULL;
	locked = false;
	priority = SP_NORMAL;

	// restore default settings

	stop();
	buffer(0);

	pitch(1.0f);
	gain(1.0f);
	position(0.0f, 0.0f, 0.0f);
	velocity(0.0f, 0.0f, 0.0f);

	looping(false);
	sourcerelative(false);

	// fit into distance model
	alSourcef(id, AL_REFERENCE_DISTANCE, al_referencedistance / 100.0f);
	alSourcef(id, AL_ROLLOFF_FACTOR, al_rollofffactor / 100.0f);
}

void source::init ( sourceowner *o )
{
	ASSERT(o);
	owner = o;
}

void source::onreassign ()
{
	if ( owner ) {
		owner->onsourcereassign(this);
		owner = NULL;
	}
}

bool source::generate ()
{
	alclearerr();
	alGenSources(1, &id);

	return !alerr(false);
}

bool source::delete_ ()
{
	alclearerr();
	alDeleteSources(1, &id);
	return !ALERR;
}

bool source::buffer ( ALuint buf_id )
{
	alclearerr();
#ifdef __APPLE__    // weird bug
	if (buf_id)
#endif
	alSourcei(id, AL_BUFFER, buf_id);

	return !ALERR;
}

bool source::looping ( bool enable )
{
	alclearerr();
	alSourcei(id, AL_LOOPING, enable ? 1 : 0);
	return !ALERR;
}

bool source::queuebuffers ( ALsizei n, const ALuint *buffer_ids )
{
	alclearerr();
	alSourceQueueBuffers(id, n, buffer_ids);
	return !ALERR;
}

bool source::unqueueallbuffers ()
{
	alclearerr();
	ALint queued;
	alGetSourcei(id, AL_BUFFERS_QUEUED, &queued);
	ALERR;
	loopi(queued)
	{
		ALuint buffer;
		alSourceUnqueueBuffers(id, 1, &buffer);
	}
	return !ALERR;
}

bool source::gain ( float g )
{
	alclearerr();
	alSourcef(id, AL_GAIN, g);
	return !ALERRF("gain: %f", g);
}

bool source::pitch ( float p )
{
	alclearerr();
	alSourcef(id, AL_PITCH, p);
	return !ALERRF("pitch: %f", p);
}

bool source::position ( const vec &pos )
{
	return position(pos.x, pos.y, pos.z);
}

bool source::position ( float x, float y, float z )
{
	alclearerr();
	alSource3f(id, AL_POSITION, x, y, z);
	if ( x < 0 || y < 0 || z < -128 || x > ssize || y > ssize || z > 128 )
	    clientlogf("warning: sound position out of range (%f,%f,%f)", x, y, z);
	return !ALERRF("id %u, %d, x: %f, y: %f, z: %f", id,
	               alIsSource(id) == AL_TRUE ? 1 : 0, x, y, z);
//    return !ALERRF("x: %f, y: %f, z: %f", x, y, z);   // when sound bug has been absent for a while, switch to this version
}

bool source::velocity ( float x, float y, float z )
{
	alclearerr();
	alSource3f(id, AL_VELOCITY, x, y, z);
	return !ALERRF("dx: %f, dy: %f, dz: %f", x, y, z);
}

vec source::position ()
{
	alclearerr();
	ALfloat v[3];
	alGetSourcefv(id, AL_POSITION, v);
	if ( ALERR )
		return vec(0, 0, 0);
	else
		return vec(v[0], v[1], v[2]);
}

bool source::sourcerelative ( bool enable )
{
	alclearerr();
	alSourcei(id, AL_SOURCE_RELATIVE, enable ? AL_TRUE : AL_FALSE);
	return !ALERR;
}

int source::state ()
{
	ALint s;
	alGetSourcei(id, AL_SOURCE_STATE, &s);
	return s;
}

bool source::secoffset ( float secs )
{
	alclearerr();
	alSourcef(id, AL_SEC_OFFSET, secs);
	// some openal implementations seem to spam invalid enum on this
	// return !ALERR;
	return !alerr(false);
}

float source::secoffset ()
{
	alclearerr();
	ALfloat s;
	alGetSourcef(id, AL_SEC_OFFSET, &s);
	// some openal implementations seem to spam invalid enum on this
	// ALERR;
	alerr(false);
	return s;
}

bool source::playing ()
{
	return ( state() == AL_PLAYING );
}

bool source::play ()
{
	alclearerr();
	alSourcePlay(id);
	return !ALERR;
}

bool source::stop ()
{
	alclearerr();
	alSourceStop(id);
	return !ALERR;
}

bool source::rewind ()
{
	alclearerr();
	alSourceRewind(id);
	return !ALERR;
}

void source::printposition ()
{
	alclearerr();
	vec v = position();
	ALint s;
	alGetSourcei(id, AL_SOURCE_TYPE, &s);
	conoutf("sound %d: pos(%f,%f,%f) t(%d) ", id, v.x, v.y, v.z, s);
	ALERR;
}

// represents an OpenAL sound buffer

sbuffer::sbuffer () :
		id(0), name(NULL)
{
}

sbuffer::~sbuffer ()
{
	unload();
}

bool sbuffer::load ( bool trydl )
{
	if ( !name ) return false;
	if ( id ) return true;
	alclearerr();
	alGenBuffers(1, &id);
	if ( !ALERR ) {
		const char *exts[] = { "", ".wav", ".ogg" };
		string filepath;
		loopk(2)
		{
			loopi(sizeof(exts)/sizeof(exts[0]))
			{
				formatstring(filepath)("packages/audio/%s%s", name, exts[i]);
				stream *f = openfile(path(filepath), "rb");
				if(!f && k>0 && trydl)   // only try donwloading after trying all extensions
				{
					requirepackage(PCK_AUDIO, filepath);
					bool skip = false;
					loopj(sizeof(exts)/sizeof(exts[0])) if(strstr(name, exts[j])) skip = true;   // don't try extensions if name already has a known extension
					if(skip) break;
					continue;
				}
				if(!f) continue;

				size_t len = strlen(filepath);
				if(len >= 4 && !strcasecmp(filepath + len - 4, ".ogg"))
				{
					OggVorbis_File oggfile;
					if(!ov_open_callbacks(f, &oggfile, NULL, 0, oggcallbacks))
					{
						vorbis_info *info = ov_info(&oggfile, -1);

						const size_t BUFSIZE = 32*1024;
						vector<char> buf;
						int bitstream;
						long bytes;

						do
						{
							char buffer[BUFSIZE];
							bytes = ov_read(&oggfile, buffer, BUFSIZE, isbigendian(), 2, 1, &bitstream);
							loopi(bytes) buf.add(buffer[i]);
						}while(bytes > 0);

						alBufferData(id, info->channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, buf.getbuf(), buf.length(), info->rate);
						ov_clear(&oggfile);
					}
					else
					{
						delete f;
						continue;
					}
				}
				else
				{
					SDL_AudioSpec wavspec;
					uint32_t wavlen;
					uint8_t *wavbuf;

					if(!SDL_LoadWAV_RW(f->rwops(), 1, &wavspec, &wavbuf, &wavlen))
					{
						SDL_ClearError();
						continue;
					}

					ALenum format;
					switch(wavspec.format)   // map wav header to openal format
					{
						case AUDIO_U8:
						case AUDIO_S8:
						format = wavspec.channels==2 ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8;
						break;
						case AUDIO_U16:
						case AUDIO_S16:
						format = wavspec.channels==2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
						break;
						default:
						SDL_FreeWAV(wavbuf);
						delete f;
						unload();
						return false;
					}

					alBufferData(id, format, wavbuf, wavlen, wavspec.freq);
					SDL_FreeWAV(wavbuf);
					delete f;

					if(ALERR)
					{
						unload();
						return false;
					};
				}

				return true;
			}
		}
	}
	unload();   // loading failed
	return false;
}

void sbuffer::unload ()
{
	if ( !id ) return;
	alclearerr();
	if ( alIsBuffer(id) ) alDeleteBuffers(1, &id);
	id = 0;
	ALERR;
}

// buffer collection, find or load data

bufferhashtable::~bufferhashtable ()
{
}

sbuffer *bufferhashtable::find ( char *name )
{
	sbuffer *b = access(name);
	if ( !b ) {
		name = newstring(name);
		b = & ( *this )[name];
		b->name = name;
	}
	return b;
}

// OpenAL error handling

void alclearerr ()
{
	alGetError();
}

bool alerr ( bool msg, int line, const char *s, ... )
{
	ALenum er = alGetError();
	if ( er && msg ) {
		const char *desc = "unknown";
		switch ( er )
		{
			case AL_INVALID_NAME:
				desc = "invalid name";
				break;
			case AL_INVALID_ENUM:
				desc = "invalid enum";
				break;
			case AL_INVALID_VALUE:
				desc = "invalid value";
				break;
			case AL_INVALID_OPERATION:
				desc = "invalid operation";
				break;
			case AL_OUT_OF_MEMORY:
				desc = "out of memory";
				break;
		}
		if ( s ) {
			defvformatstring(p, s, s);
			conoutf("\f3OpenAL Error (%X): %s, line %d, (%s)", er, desc, line,
			        p);
		}
		else if ( line )
			conoutf("\f3OpenAL Error (%X): %s, line %d", er, desc, line);
		else
			conoutf("\f3OpenAL Error (%X): %s", er, desc);
	}
	return er > 0;
}

VARP(gainscale, 1, 90, 100);
int warn_about_unregistered_sound = 0;
location::location ( int sound, const worldobjreference &r, int priority ) :
		cfg(NULL), src(NULL), ref(NULL), stale(false), playmillis(0)
{
	vector<soundconfig> &sounds = (
	        r.type == worldobjreference::WR_ENTITY ? mapsounds : gamesounds );
	if ( !sounds.inrange(sound) ) {
		if ( lastmillis - warn_about_unregistered_sound > 30 * 1000 )   // delay message to every 30 secs so console is not spammed.
		        {
			// occurs when a map contains an ambient sound entity, but sound entity is not found in map cfg file.
			conoutf("\f3ERROR: this map contains at least one unregistered ambient sound (sound entity# %d)",
			        sound);
			warn_about_unregistered_sound = lastmillis;
		}
		stale = true;
		return;
	}

	// get sound config
	cfg = &sounds[sound];
	cfg->onattach();
	const float dist = camera1->o.dist(r.currentposition());
	if ( ( r.type == worldobjreference::WR_ENTITY && cfg->maxuses >= 0
	       && cfg->uses >= cfg->maxuses )
	     || cfg->muted || ( cfg->audibleradius && dist > cfg->audibleradius ) )   // check max-use limits and audible radius
	     {
		stale = true;
		return;
	}

	// assign buffer
	sbuffer *buf = cfg->buf;
	if ( !buf || !buf->id ) {
		stale = true;
		return;
	}

	// obtain source
	src = sourcescheduler::instance().newsource(priority, r.currentposition());
	// apply configuration
	if ( !src || !src->valid || !src->buffer(cfg->buf->id)
	     || !src->looping(cfg->loop) || !setvolume(1.0f) ) {
		stale = true;
		return;
	}
	src->init(this);

	// set position
	attachworldobjreference(r);
}

location::~location ()
{
	if ( src ) sourcescheduler::instance().releasesource(src);
	if ( cfg ) cfg->ondetach();
	if ( ref ) {
		ref->detach();
		DELETEP(ref);
	}
}

// attach a reference to a world object to get the 3D position from

void location::attachworldobjreference ( const worldobjreference &r )
{
	ASSERT(!stale && src && src->valid);
	if ( stale ) return;
	if ( ref ) {
		ref->detach();
		DELETEP(ref);
	}
	ref = r.clone();
	evaluateworldobjref();
	ref->attach();
}

// enable/disable distance calculations
void location::evaluateworldobjref ()
{
	src->sourcerelative(ref->nodistance());
}

// marks itself for deletion if source got lost
void location::onsourcereassign ( source *s )
{
	if ( s == src ) {
		stale = true;
		src = NULL;
	}
}

void location::updatepos ()
{
	ASSERT(!stale && ref);
	if ( stale ) return;

	const vec &pos = ref->currentposition();

	// forced fadeout radius
	bool volumeadjust = ( cfg->model == soundconfig::DM_LINEAR );
	float forcedvol = 1.0f;
	if ( volumeadjust ) {
		float dist = camera1->o.dist(pos);
		if ( dist > cfg->audibleradius )
			forcedvol = 0.0f;
		else if ( dist < 0 )
			forcedvol = 1.0f;
		else
			forcedvol = 1.0f - ( dist / cfg->audibleradius );
	}

	// reference determines the used model
	switch ( ref->type )
	{
		case worldobjreference::WR_CAMERA:
			break;
		case worldobjreference::WR_PHYSENT: {
			if ( !ref->nodistance() ) src->position(pos);
			if ( volumeadjust ) setvolume(forcedvol);
			break;
		}
		case worldobjreference::WR_ENTITY: {
			entityreference &eref = *(entityreference *) ref;
			const float vol =
			        eref.ent->attr4 <= 0.0f ? 1.0f : eref.ent->attr4 / 255.0f;
			float dist = camera1->o.dist(pos);

			if ( ref->nodistance() ) {
				// own distance model for entities/mapsounds: linear & clamping

				const float innerradius = float(eref.ent->attr3);   // full gain area / size property
				const float outerradius = float(eref.ent->attr2);   // fading gain area / radius property

				if ( dist <= innerradius )
					src->gain(1.0f * vol);   // inside full gain area
				else if ( dist <= outerradius )   // inside fading gain area
				        {
					const float fadeoutdistance = outerradius - innerradius;
					const float fadeout = dist - innerradius;
					src->gain( ( 1.0f - fadeout / fadeoutdistance ) * vol);
				}
				else
					src->gain(0.0f);   // outside entity
			}
			else {
				// use openal distance model to make the sound appear from a certain direction (non-ambient)
				src->position(pos);
				src->gain(vol);
			}
			break;
		}
		case worldobjreference::WR_STATICPOS: {
			if ( !ref->nodistance() ) src->position(pos);
			if ( volumeadjust ) setvolume(forcedvol);
			break;
		}
	}
}

void location::update ()
{
	if ( stale ) return;

	switch ( src->state() )
	{
		case AL_PLAYING:
			updatepos();
			break;
		case AL_STOPPED:
		case AL_PAUSED:
		case AL_INITIAL:
			stale = true;
			DEBUG("location is stale")
			;
			break;
	}
}

void location::play ( bool loop )
{
	if ( stale ) return;

	updatepos();
	if ( loop ) src->looping(loop);
	if ( src->play() ) playmillis = totalmillis;
}

void location::pitch ( float p )
{
	if ( stale ) return;
	src->pitch(p);
}

bool location::setvolume ( float v )
{
	if ( stale ) return false;
	return src->gain(cfg->vol / 100.0f * ( (float) gainscale ) / 100.0f * v);
}

void location::offset ( float secs )
{
	ASSERT(!stale);
	if ( stale ) return;
	src->secoffset(secs);
}

float location::offset ()
{
	ASSERT(!stale);
	if ( stale ) return 0.0f;
	return src->secoffset();
}

void location::drop ()
{
	src->stop();
	stale = true;   // drop from collection on next update cycle
}

// location collection

location *locvector::find ( int sound,
                            worldobjreference *ref,
                            const vector<soundconfig> &soundcollection /* = gamesounds*/)
{
	if ( sound < 0 || sound >= soundcollection.length() ) return NULL;
	loopi(ulen)
		if ( buf[i] && !buf[i]->stale ) {
			if ( buf[i]->cfg != &soundcollection[sound] ) continue;   // check if its the same sound
			if ( ref && *buf[i]->ref != *ref ) continue;   // optionally check if its the same reference
			return buf[i];   // found
		}
	return NULL;
}

void locvector::delete_ ( int i )
{
	delete remove(i);
}

void locvector::replaceworldobjreference ( const worldobjreference &oldr,
                                           const worldobjreference &newr )
{
	loopv(*this)
	{
		location *l = buf[i];
		if ( !l || !l->ref ) continue;
		if ( *l->ref == oldr ) l->attachworldobjreference(newr);
	}
}

// update stuff, remove stale data
void locvector::updatelocations ()
{
	// check if camera carrier changed
	bool camchanged = false;
	static physent *lastcamera = NULL;
	if ( lastcamera != camera1 ) {
		if ( lastcamera != NULL ) camchanged = true;
		lastcamera = camera1;
	}

	// update all locations
	loopv(*this)
	{
		location *l = buf[i];
		if ( !l ) continue;

		l->update();
		if ( l->stale )
			delete_(i--);
		else if ( camchanged ) l->evaluateworldobjref();   // cam changed, evaluate world reference again
	}
}

// force pitch across all locations
void locvector::forcepitch ( float pitch )
{
	loopv(*this)
	{
		location *l = buf[i];
		if ( !l ) continue;
		if ( l->src && l->src->locked ) l->src->pitch(pitch);
	}
}

// delete all sounds except world-neutral sounds like GUI/notification
void locvector::deleteworldobjsounds ()
{
	loopv(*this)
	{
		location *l = buf[i];
		if ( !l ) continue;
		// world-neutral sounds
		if ( l->cfg == &gamesounds[S_MENUENTER] || l->cfg
		        == &gamesounds[S_MENUSELECT]
		     || l->cfg == &gamesounds[S_CALLVOTE]
		     || l->cfg == &gamesounds[S_VOTEPASS]
		     || l->cfg == &gamesounds[S_VOTEFAIL] ) continue;

		delete_(i--);
	}
}

#define DEBUGCOND (audiodebug==1)

VARP(soundschedpriorityscore, 0, 100, 1000);
VARP(soundscheddistancescore, 0, 5, 1000);
VARP(soundschedoldbonus, 0, 100, 1000);
VARP(soundschedreserve, 0, 2, 100);

sourcescheduler *sourcescheduler::inst;

sourcescheduler::sourcescheduler ()
{
}

sourcescheduler &sourcescheduler::instance ()
{
	if ( inst == NULL ) inst = new sourcescheduler();
	return *inst;
}

void sourcescheduler::init ( int numsoundchannels )
{
	this->numsoundchannels = numsoundchannels;
	int newchannels = numsoundchannels - sources.length();
	if ( newchannels < 0 ) {
		loopv(sources)
		{
			source *src = sources[i];
			if ( src->locked ) continue;
			sources.remove(i--);
			delete src;
			if ( sources.length() <= numsoundchannels ) break;
		}
	}
	else
		loopi(newchannels)
		{
			source *src = new source();
			if ( src->valid )
				sources.add(src);
			else {
				DELETEP(src);
				break;
			}
		}
}

void sourcescheduler::reset ()
{
	loopv(sources)
		sources[i]->reset();
	sources.deletecontents();
}

// returns a free sound source (channel)
// consuming code must call sourcescheduler::releasesource() after use

source *sourcescheduler::newsource ( int priority, const vec &o )
{
	if ( !sources.length() ) {
		DEBUG("empty source collection");
		return NULL;
	}

	source *src = NULL;

	// reserve some sources for sounds of higher priority
	int reserved = ( SP_HIGHEST - priority ) * soundschedreserve;
	DEBUGVAR(reserved);

	// search unused source
	loopv(sources)
	{
		if ( !sources[i]->locked && reserved-- <= 0 ) {
			src = sources[i];
			DEBUGVAR(src);
			break;
		}
	}

	if ( !src ) {
		DEBUG("no empty source found");

		// low priority sounds can't replace others
		if ( SP_LOW == priority ) {
			DEBUG("low prio sound aborted");
			return NULL;
		}

		// try replacing a used source
		// score our sound
		const float dist = o.iszero() ? 0.0f : camera1->o.dist(o);
		const float score = ( priority * soundschedpriorityscore )
		        - ( dist * soundscheddistancescore );

		// score other sounds
		float worstscore = 0.0f;
		source *worstsource = NULL;

		loopv(sources)
		{
			source *s = sources[i];
			if ( s->priority == SP_HIGHEST ) continue;   // highest priority sounds can't be replaced

			const vec & otherpos = s->position();
			float otherdist =
			        otherpos.iszero() ? 0.0f : camera1->o.dist(otherpos);
			float otherscore = ( s->priority * soundschedpriorityscore )
			        - ( otherdist * soundscheddistancescore )
			                   + soundschedoldbonus;
			if ( !worstsource || otherscore < worstscore ) {
				worstsource = s;
				worstscore = otherscore;
			}
		}

		// pick worst source and replace it
		if ( worstsource && score > worstscore ) {
			src = worstsource;
			src->onreassign();   // inform previous owner about the take-over
			DEBUG("replaced sound of same prio");
		}
	}

	if ( !src ) {
		DEBUG("sound aborted, no channel takeover possible");
		return NULL;
	}

	src->reset();       // default settings
	src->lock();        // exclusive lock
	src->priority = priority;
	return src;
}

// give source back to the pool
void sourcescheduler::releasesource ( source *src )
{
	ASSERT(src);
	ASSERT(src->locked);   // detect double release

	if ( !src ) return;

	DEBUG("unlocking source");

	src->unlock();

	if ( sources.length() > numsoundchannels ) {
		sources.removeobj(src);
		delete src;
		DEBUG("source deleted");
	}
}

