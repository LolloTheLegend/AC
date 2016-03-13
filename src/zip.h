#ifndef ZIP_H_
#define ZIP_H_

#include "stream.h"
#include "tools.h"

bool addzip ( const char *name, const char *mount = NULL, const char *strip =
        NULL,
              bool extract = false, int type = -1 );
bool removezip ( const char *name );
stream *openzipfile ( const char *name, const char *mode );
int listzipfiles ( const char *dir, const char *ext, vector<char *> &files );

#endif /* ZIP_H_ */
