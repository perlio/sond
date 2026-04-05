#ifndef HEADERBAR_H_INCLUDED
#define HEADERBAR_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _SondTreeviewFM SondTreeviewFM;

void init_headerbar(Projekt*);

#ifdef _WIN32
void cb_seadrive_status_app_window(SondTreeviewFM *stvfm,
        guint pending_down, guint pending_up,
        gpointer user_data);
#endif

#endif // HEADERBAR_H_INCLUDED
