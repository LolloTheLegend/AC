#ifndef WORLDRENDER_H_
#define WORLDRENDER_H_


int lod_factor();
void render_world(float vx, float vy, float vh, float changelod, int yaw,
		int pitch, float fov, float fovy, int w, int h);


#endif /* WORLDRENDER_H_ */
