#ifndef RENDERGL_H
#define RENDERGL_H

#include <GL/gl.h>
#include "entities.h"
#include "world.h"

// TODO: Lollo fix this
struct color;

extern int ati_mda_bug;
extern float scopesensfunc;
extern float fovy, aspect;
extern int farplane;
extern physent *camera1;
extern bool minimap, reflecting, refracting;
extern int waterreflect, waterrefract;
extern int minimaplastsize;
extern GLuint minimaptex;
extern int xtraverts;
extern glmatrixf mvmatrix, projmatrix, clipmatrix, mvpmatrix, invmvmatrix, invmvpmatrix;
extern vec worldpos, camdir, camup, camright;

// GL_ARB_multitexture
extern PFNGLACTIVETEXTUREARBPROC       glActiveTexture_;
extern PFNGLCLIENTACTIVETEXTUREARBPROC glClientActiveTexture_;
extern PFNGLMULTITEXCOORD2FARBPROC     glMultiTexCoord2f_;
extern PFNGLMULTITEXCOORD3FARBPROC     glMultiTexCoord3f_;

// GL_EXT_multi_draw_arrays
extern PFNGLMULTIDRAWARRAYSEXTPROC   glMultiDrawArrays_;
extern PFNGLMULTIDRAWELEMENTSEXTPROC glMultiDrawElements_;

// GL_EXT_draw_range_elements
extern PFNGLDRAWRANGEELEMENTSEXTPROC glDrawRangeElements_;

// GL_EXT_stencil_two_side
extern PFNGLACTIVESTENCILFACEEXTPROC glActiveStencilFace_;

// GL_ATI_separate_stencil
extern PFNGLSTENCILOPSEPARATEATIPROC   glStencilOpSeparate_;
extern PFNGLSTENCILFUNCSEPARATEATIPROC glStencilFuncSeparate_;





void gl_checkextensions();
void gl_init(int w, int h, int bpp, int depth, int fsaa);
void enablepolygonoffset(GLenum type);
void disablepolygonoffset(GLenum type, bool restore = true);
void line(int x1, int y1, float z1, int x2, int y2, float z2);
void line(int x1, int y1, int x2, int y2, color *c = NULL);
void linestyle(float width, int r, int g, int b);
void box(block &b, float z1, float z2, float z3, float z4);
void quad(GLuint tex, float x, float y, float s, float tx, float ty, float tsx, float tsy = 0);
void quad(GLuint tex, vec &c1, vec &c2, float tx, float ty, float tsx, float tsy);
void circle(GLuint tex, float x, float y, float r, float tx, float ty, float tr, int subdiv = 32);
void dot(int x, int y, float z);
void blendbox(int x1, int y1, int x2, int y2, bool border, int tex = -1, color *c = NULL);
void renderaboveheadicon(playerent *p);
void rendercursor(int x, int y, int w);
float dynfov();
void resetcamera();
void clearminimap();
void cleanupgl();
void resetzones();
void setperspective(float fovy, float aspect, float nearplane, float farplane);
void sethudgunperspective(bool on);
void gl_drawframe(int w, int h, float changelod, float curfps);


#endif /* RENDERGL_H */
