#ifndef ZOND_CONVERT_H_INCLUDED
#define ZOND_CONVERT_H_INCLUDED

#include <glib.h>

#include "../znd/zond_dbase.h"

gint zond_convert(ZondDBase*, gchar const*, GError**);

//Meldet Rückfrage und Konvertierung bei zond_dbase an (einmal beim Start)
void zond_convert_register_handler(void);

#endif // ZOND_CONVERT_H_INCLUDED
