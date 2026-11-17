#ifndef ZOND_UPDATE_H_INCLUDED
#define ZOND_UPDATE_H_INCLUDED

typedef int gint;
typedef struct _Projekt Projekt;
typedef struct _GError GError;
typedef struct _Info_Window InfoWindow;

gint zond_update(Projekt*, InfoWindow*, GError**);

//Prueft im Hintergrund auf neuere Version; fragt nur, wenn es eine gibt
void zond_update_check_on_start(Projekt*);

#endif // ZOND_UPDATE_H_INCLUDED
