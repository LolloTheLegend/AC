#ifndef CRYPTO_TOOLS_H
#define CRYPTO_TOOLS_H
#ifdef NULL
#undef NULL
#endif
#define NULL 0

typedef unsigned int uint;

#define rndmt(x) ((int)(randomMT()&0xFFFFFF)%(x))
#define rndscale(x) (float((randomMT()&0xFFFFFF)*double(x)/double(0xFFFFFF)))

extern void seedMT(uint seed);
extern uint randomMT(void);
#endif
