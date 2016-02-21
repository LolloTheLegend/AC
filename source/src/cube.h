#ifndef CUBE_H
#define CUBE_H

// to "trick" i18n/gettext
#define CC '\f'

#include "platform.h"
#include "tools.h"
#include "stream.h"
#include "zip.h"
#include "geom.h"
#include "model.h"
#include "protocol.h"
#include "audiomanager.h"
#include "weapon.h"
#include <entities.h>
#include "world.h"
#include "i18n.h"
#include "command.h"

#ifndef STANDALONE
 #include "varray.h"
 #include "console.h"
 enum
 {
   SDL_AC_BUTTON_WHEELDOWN = -5,
   SDL_AC_BUTTON_WHEELUP = -4,
   SDL_AC_BUTTON_RIGHT = -3,
   SDL_AC_BUTTON_MIDDLE = -2,
   SDL_AC_BUTTON_LEFT = -1
 };
#endif

#include "console.h"
#include "docs.h"
#include "client.h"
#include "clientgame.h"
#include "clients2c.h"
#include "crypto.h"
#include "editing.h"
#include "menus.h"
#include "physics.h"

#include "texture.h"
#include "rendercubes.h"
#include "rendergl.h"
#include "renderhud.h"
#include "rendermodel.h"
#include "renderparticles.h"
#include "rendertext.h"
#include "scoreboard.h"
#include "shadow.h"
#include "water.h"
#include "wizard.h"
#include "worldio.h"
#include "worldlight.h"
 #include "protos.h"                     // external function decls

#endif

