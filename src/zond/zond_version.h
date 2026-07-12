#ifndef ZOND_VERSION_H_INCLUDED
#define ZOND_VERSION_H_INCLUDED

#include <glib.h>

#define ZOND_VERSION_MAJOR 1 //wenn sich Struktur der .znd-Datei aendert
#define ZOND_VERSION_MINOR 0 //neues Feature
#define ZOND_VERSION_PATCH 8 //irjendwatt

#define ZOND_VERSION_STR G_STRINGIFY(ZOND_VERSION_MAJOR) "." \
                          G_STRINGIFY(ZOND_VERSION_MINOR) "." \
                          G_STRINGIFY(ZOND_VERSION_PATCH)

#endif // ZOND_VERSION_H_INCLUDED
