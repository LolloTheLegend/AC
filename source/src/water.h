#ifndef WATER_H_
#define WATER_H_

#include <GL/gl.h>

extern int wx1, wy1, wx2, wy2;
extern float wsx1, wsy1, wsx2, wsy2;
extern int mtwater;

void setwatercolor(const char *r = "", const char *g = "", const char *b = "", const char *a = "");
int renderwater(float hf, GLuint reflecttex, GLuint refracttex);
void addwaterquad(int x, int y, int size);
void calcwaterscissor();
void resetwater();

#endif /* WATER_H_ */
