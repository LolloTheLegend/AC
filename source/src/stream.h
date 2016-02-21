
#ifndef STREAM_H_
#define STREAM_H_

#include "tools.h"
#include <stddef.h>

struct stream
{
    virtual ~stream() {}
    virtual void close() = 0;
    virtual bool end() = 0;
    virtual long tell() { return -1; }
    virtual bool seek(long offset, int whence = SEEK_SET) { return false; }
    virtual long size();
    virtual int read(void *buf, int len) { return 0; }
    virtual int write(const void *buf, int len) { return 0; }
    virtual int getchar() { uchar c; return read(&c, 1) == 1 ? c : -1; }
    virtual bool putchar(int n) { uchar c = n; return write(&c, 1) == 1; }
    virtual bool getline(char *str, int len);
    virtual bool putstring(const char *str) { int len = (int)strlen(str); return write(str, len) == len; }
    virtual bool putline(const char *str) { return putstring(str) && putchar('\n'); }
    virtual int printf(const char *fmt, ...) { return -1; }
    virtual uint getcrc() { return 0; }

    template<class T> bool put(T n) { return write(&n, sizeof(n)) == sizeof(n); }
    template<class T> bool putlil(T n) { return put<T>(lilswap(n)); }
    template<class T> bool putbig(T n) { return put<T>(bigswap(n)); }

    template<class T> T get() { T n; return read(&n, sizeof(n)) == sizeof(n) ? n : 0; }
    template<class T> T getlil() { return lilswap(get<T>()); }
    template<class T> T getbig() { return bigswap(get<T>()); }

#ifndef STANDALONE
    SDL_RWops *rwops();
#endif
};

char *path(char *s);
char *unixpath(char *s);
char *path(const char *s, bool copy);
const char *parentdir(const char *directory);
const char *behindpath(const char *s);
bool fileexists(const char *path, const char *mode);
bool createdir(const char *path);
size_t fixpackagedir(char *dir);
void sethomedir(const char *dir);
void addpackagedir(const char *dir);
const char *findfile(const char *filename, const char *mode);
bool listdir(const char *dir, const char *ext, vector<char *> &files);
int listfiles(const char *dir, const char *ext, vector<char *> &files);
bool delfile(const char *path);
stream *openrawfile(const char *filename, const char *mode);
stream *openfile(const char *filename, const char *mode);
int getfilesize(const char *filename);
stream *opentempfile(const char *filename, const char *mode);
stream *opengzfile(const char *filename, const char *mode, stream *file = NULL, int level = Z_BEST_COMPRESSION);
char *loadfile(const char *fn, int *size, const char *mode = NULL);
void filerotate(const char *basename, const char *ext, int keepold, const char *oldformat = NULL);

#endif /* STREAM_H_ */
