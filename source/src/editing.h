/*
 * editing.h
 *
 *  Created on: Feb 20, 2016
 *      Author: edav
 */

#ifndef SOURCE_SRC_EDITING_H_
#define SOURCE_SRC_EDITING_H_

#include "tools.h"
#include "world.h"

extern bool editmode;
extern vector<block> sels;
extern int unsavededits;
extern vector<int> tagclipcubes;
extern bool showtagclipfocus;
extern int showtagclips;

void toggleedit(bool force = false);
bool noteditmode(const char* func = NULL);
char *editinfo();
void checkselections();
void cursorupdate();
void pruneundos(int maxremain = 0);
void storeposition(short p[]);
void makeundo(block &sel);
void restoreposition(short p[]);
void restoreeditundo(ucharbuf &q);
int backupeditundo(vector<uchar> &buf, int undolimit, int redolimit);
void editdrag(bool isdown);
void editheightxy(bool isfloor, int amount, block &sel);
void edittexxy(int type, int t, block &sel);
void edittypexy(int type, block &sel);
void editequalisexy(bool isfloor, block &sel);
void setvdeltaxy(int delta, block &sel);

#endif /* SOURCE_SRC_EDITING_H_ */
