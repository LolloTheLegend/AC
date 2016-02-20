/*
 * texture.h
 *
 *  Created on: Feb 20, 2016
 *      Author: edav
 */

#ifndef SOURCE_SRC_TEXTURE_H_
#define SOURCE_SRC_TEXTURE_H_

#include <GL/gl.h>
#include <SDL/SDL.h>

#include "tools.h"

struct Texture
{
    char *name;
    int xs, ys, bpp, clamp;
    float scale;
    bool mipmap, canreduce;
    GLuint id;
};

extern float skyfloor;
extern Texture *notexture, *noworldtexture;
extern int hwtexsize, hwmaxaniso;
extern bool silent_texture_load;
extern bool uniformtexres;
extern int maxtmus;

void scaletexture(uchar *src, uint sw, uint sh, uint bpp, uchar *dst, uint dw, uint dh);
void createtexture(int tnum, int w, int h, void *pixels, int clamp, bool mipmap, bool canreduce, GLenum format);
SDL_Surface *wrapsurface(void *data, int width, int height, int bpp);
SDL_Surface *creatergbsurface(int width, int height);
SDL_Surface *creatergbasurface(int width, int height);
SDL_Surface *forcergbsurface(SDL_Surface *os);
SDL_Surface *forcergbasurface(SDL_Surface *os);
GLenum texformat(int bpp);
Texture *textureload(const char *name, int clamp = 0, bool mipmap = true, bool canreduce = false, float scale = 1.0f, bool trydl = false);
Texture *createtexturefromsurface(const char *name, SDL_Surface *s);
Texture *lookuptexture(int tex, Texture *failtex = notexture, bool trydl = false);
void cleanuptextures();
bool reloadtexture(Texture &t);
bool reloadtexture(const char *name);
void reloadtextures();
void loadsky(char *basename, bool reload);
void draw_envbox(int fogdist);
void resettmu(int n);
void scaletmu(int n, int rgbscale, int alphascale = 0);
void colortmu(int n, float r = 0, float g = 0, float b = 0, float a = 0);
void setuptmu(int n, const char *rgbfunc = NULL, const char *alphafunc = NULL);
void inittmus();
void cleanuptmus();
void blitsurface(SDL_Surface *dst, SDL_Surface *src, int x, int y);


#endif /* SOURCE_SRC_TEXTURE_H_ */
