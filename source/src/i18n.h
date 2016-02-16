// internationalization and localization

// localization manager
#ifndef I18N_H
#define I18N_H

struct i18nmanager
{
    const char *domain;
    const char *basepath;
    char *locale;

    i18nmanager(const char *domain, const char *basepath); // initialize locale system

};

extern char *lang;

enum { CF_NONE = 0, CF_OK, CF_FAIL, CF_SIZE };

#define FONTSTART 33
#define FONTCHARS 94

#endif	// I18N_H

