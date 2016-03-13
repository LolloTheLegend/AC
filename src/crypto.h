#ifndef CRYPTO_H
#define CRYPTO_H

#ifdef NULL
#undef NULL
#endif
#define NULL 0

#include "../src/tools.h"

uint randomMT ( void );

#define rndmt(x) ((int)(randomMT()&0xFFFFFF)%(x))
#define rndscale(x) (float((randomMT()&0xFFFFFF)*double(x)/double(0xFFFFFF)))

void seedMT ( uint seed );
void genprivkey ( const char *seed,
                  vector<char> &privstr,
                  vector<char> &pubstr );
bool hashstring ( const char *str, char *result, int maxlen );
const char *genpwdhash ( const char *name, const char *pwd, int salt );
void answerchallenge ( const char *privstr,
                       const char *challenge,
                       vector<char> &answerstr );
void *parsepubkey ( const char *pubstr );
void freepubkey ( void *pubkey );
void *genchallenge ( void *pubkey,
                     const void *seed,
                     int seedlen,
                     vector<char> &challengestr );
void freechallenge ( void *answer );
bool checkchallenge ( const char *answerstr, void *correct );

#endif
