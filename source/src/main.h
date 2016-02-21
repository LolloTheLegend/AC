#ifndef MAIN_H_
#define MAIN_H_

extern SDL_Surface *screen;
extern int colorbits, depthbits, stencilbits;
extern bool firstrun, inmainloop;
extern int gamespeed;
extern int screenshottype;

void keyrepeat(bool on);
bool interceptkey(int sym);


#endif /* MAIN_H_ */
