#ifndef SOND_CLIENT_FILE_MANAGER_H_INCLUDED
#define SOND_CLIENT_FILE_MANAGER_H_INCLUDED

typedef struct _FileManager FileManager;
typedef struct _GtkEntry GtkEntry;
typedef void *gpointer;

void sond_client_file_manager_entry_activate(GtkEntry*, gpointer);

#endif // SOND_CLIENT_FILE_MANAGER_H_INCLUDED
