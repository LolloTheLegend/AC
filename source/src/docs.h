#ifndef SOURCE_DOCS_H_
#define SOURCE_DOCS_H_

extern void* docmenu;

void toggledoc();
void scrolldoc(int i);
void renderdoc(int x, int y, int doch);
void renderdocmenu(void *menu, bool init);


#endif /* SOURCE_DOCS_H_ */
