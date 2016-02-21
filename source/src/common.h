#ifndef COMMON_H_
#define COMMON_H_

#define AC_VERSION 1202
#define AC_MASTER_URI "ms.cubers.net"
#define AC_MASTER_PORT 28760
#define AC_MASTER_HTTP 1 // default
#define AC_MASTER_RAW 0
#define MAXCL 16
#define CONFIGROTATEMAX 5               // keep 5 old versions of saved.cfg and init.cfg around

struct color
{
    float r, g, b, alpha;
    color(){}
    color(float r, float g, float b) : r(r), g(g), b(b), alpha(1.0f) {}
    color(float r, float g, float b, float a) : r(r), g(g), b(b), alpha(a) {}
};

#endif /* COMMON_H_ */
