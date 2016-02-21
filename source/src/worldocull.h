#ifndef WORLDOCULL_H_
#define WORLDOCULL_H_


void disableraytable();
void computeraytable(float vx, float vy, float fov);
int isoccluded(float vx, float vy, float cx, float cy, float csize);

#endif /* WORLDOCULL_H_ */
