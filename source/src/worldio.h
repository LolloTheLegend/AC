#ifndef WORLDIO_H_
#define WORLDIO_H_

#include "tools.h"
#include "world.h"

extern int numspawn[3], maploaded, numflagspawn[2];

const char *setnames(const char *name);
void writemap(char *name, int size, uchar *data);
uchar *readmap(char *name, int *size, int *revision);
void writecfggz(char *name, int size, int sizegz, uchar *data);
uchar *readmcfggz(char *name, int *size, int *sizegz);
void rlencodecubes(vector<uchar> &f, sqr *s, int len, bool preservesolids);
void rldecodecubes(ucharbuf &f, sqr *s, int len, int version, bool silent);
void clearheaderextras();
void save_world(char *mname, bool skipoptimise = false, bool addcomfort = false);
bool load_world(char *mname);
void xmapbackup(const char *nickprefix, const char *nick);
void writeallxmaps();
int loadallxmaps();

#endif /* WORLDIO_H_ */
