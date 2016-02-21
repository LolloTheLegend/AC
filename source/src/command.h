#ifndef COMMAND_H
#define COMMAND_H

#include "tools.h"

enum
{
	ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_ALIAS
};

struct identstack
{
	char *action;
	int context;
	identstack *next;
};

struct ident
{
	int type;           // one of ID_* above
	const char *name;
	bool isconst;
	union
	{
		int minval;    // ID_VAR
		float minvalf;   // ID_FVAR
	};
	union
	{
		int maxval;    // ID_VAR
		float maxvalf;   // ID_FVAR
	};
	union
	{
		int *i;         // ID_VAR
		float *f;       // ID_FVAR
		char **s;       // ID_SVAR;
	} storage;
	union
	{
		void (*fun) ();      // ID_VAR, ID_COMMAND
		identstack *stack;   // ID_ALIAS
	};
	const char *sig;        // command signature
	char *action, *executing;   // ID_ALIAS
	bool persist;

	int context;

	ident ()
	{
	}

	// ID_VAR
	ident ( int type,
	        const char *name,
	        int minval,
	        int maxval,
	        int *i,
	        void (*fun) (),
	        bool persist,
	        int context ) :
			type(type), name(name), isconst(false), minval(minval),
			        maxval(maxval), fun(fun), sig(NULL), action(NULL),
			        executing(NULL), persist(persist), context(context)
	{
		storage.i = i;
	}

	// ID_FVAR
	ident ( int type,
	        const char *name,
	        float minval,
	        float maxval,
	        float *f,
	        void (*fun) (),
	        bool persist,
	        int context ) :
			type(type), name(name), isconst(false), minvalf(minval),
			        maxvalf(maxval), fun(fun), sig(NULL), action(NULL),
			        executing(NULL), persist(persist), context(context)
	{
		storage.f = f;
	}

	// ID_SVAR
	ident ( int type,
	        const char *name,
	        char **s,
	        void (*fun) (),
	        bool persist,
	        int context ) :
			type(type), name(name), isconst(false), minval(0), maxval(0),
			        fun(fun), sig(NULL), action(NULL), executing(NULL),
			        persist(persist), context(context)
	{
		storage.s = s;
	}

	// ID_ALIAS
	ident ( int type,
	        const char *name,
	        char *action,
	        bool persist,
	        int context ) :
			type(type), name(name), isconst(false), minval(0), maxval(0),
			        stack(0), sig(NULL), action(action), executing(NULL),
			        persist(persist), context(context)
	{
		storage.i = NULL;
	}

	// ID_COMMAND
	ident ( int type,
	        const char *name,
	        void (*fun) (),
	        const char *sig,
	        int context ) :
			type(type), name(name), isconst(false), minval(0), maxval(0),
			        fun(fun), sig(sig), action(NULL), executing(NULL),
			        persist(false), context(context)
	{
		storage.i = NULL;
	}
};

enum
{
	IEXC_CORE = 0, IEXC_CFG, IEXC_PROMPT, IEXC_MAPCFG, IEXC_MDLCFG, IEXC_NUM
};
// script execution context

// nasty macros for registering script functions, abuses globals to avoid excessive infrastructure
#define COMMANDN(name, fun, sig) static bool __dummy_##fun = addcommand(#name, (void (*)())fun, sig)
#define COMMAND(name, sig) COMMANDN(name, name, sig)
#define COMMANDF(name, sig, inlinefunc) static void __dummy_##name inlinefunc ; COMMANDN(name, __dummy_##name, sig)

#define VARP(name, min, cur, max) int name = variable(#name, min, cur, max, &name, NULL, true)
#define VAR(name, min, cur, max)  int name = variable(#name, min, cur, max, &name, NULL, false)
#define VARN(name, global, min, cur, max) int global = variable(#name, min, cur, max, &global, NULL, false)
#define VARNP(name, global, min, cur, max) int global = variable(#name, min, cur, max, &global, NULL, true)
#define VARF(name, min, cur, max, body)  extern int name; void var_##name() { body; } int name = variable(#name, min, cur, max, &name, var_##name, false)
#define VARFP(name, min, cur, max, body) extern int name; void var_##name() { body; } int name = variable(#name, min, cur, max, &name, var_##name, true)

#define FVARP(name, min, cur, max) float name = fvariable(#name, min, cur, max, &name, NULL, true)
#define FVAR(name, min, cur, max)  float name = fvariable(#name, min, cur, max, &name, NULL, false)
#define FVARF(name, min, cur, max, body)  extern float name; void var_##name() { body; } float name = fvariable(#name, min, cur, max, &name, var_##name, false)
#define FVARFP(name, min, cur, max, body) extern float name; void var_##name() { body; } float name = fvariable(#name, min, cur, max, &name, var_##name, true)

#define SVARP(name, cur) char *name = svariable(#name, cur, &name, NULL, true)
#define SVAR(name, cur)  char *name = svariable(#name, cur, &name, NULL, false)
#define SVARF(name, cur, body)  extern char *name; void var_##name() { body; } char *name = svariable(#name, cur, &name, var_##name, false)
#define SVARFP(name, cur, body) extern char *name; void var_##name() { body; } char *name = svariable(#name, cur, &name, var_##name, true)

#define ATOI(s) strtol(s, NULL, 0)      // supports hexadecimal numbers

extern int execcontext;
extern bool per_idents, neverpersist;

void push ( const char *name, const char *action );
void pop ( const char *name );
void alias ( const char *name, const char *action, bool constant = false );
int variable ( const char *name,
               int min,
               int cur,
               int max,
               int *storage,
               void (*fun) (),
               bool persist );
float fvariable ( const char *name,
                  float min,
                  float cur,
                  float max,
                  float *storage,
                  void (*fun) (),
                  bool persist );
char *svariable ( const char *name,
                  const char *cur,
                  char **storage,
                  void (*fun) (),
                  bool persist );
void setvar ( const char *name, int i, bool dofunc = false );
void setfvar ( const char *name, float f, bool dofunc = false );
void setsvar ( const char *name, const char *str, bool dofunc = false );
int getvar ( const char *name );
bool identexists ( const char *name );
const char *getalias ( const char *name );
bool addcommand ( const char *name, void (*fun) (), const char *sig );
char *parseword ( const char *&p );
char *conc ( char **w, int n, bool space );
void intret ( int v );
const char *floatstr ( float v );
void floatret ( float v );
void result ( const char *s );
char *executeret ( const char *p );
int execute ( const char *p );
void resetcomplete ();
void complete ( char *s );
void setcontext ( const char *context, const char *info );
void resetcontext ();
bool execfile ( const char *cfgfile );
void exec ( const char *cfgfile );
void explodelist ( const char *s, vector<char *> &elems );
char *indexlist ( const char *s, int pos );
char *strreplace ( char *dest,
                   const char *source,
                   const char *search,
                   const char *replace );
int stringsort ( const char **a, const char **b );
void writecfg ();
void deletecfg ();
void identnames ( vector<const char *> &names, bool builtinonly );
void pushscontext ( int newcontext );
int popscontext ();
int millis_ ();
const char *currentserver ( int i );

#endif	// COMMAND_H

