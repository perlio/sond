#ifndef EXPORT_ZIP_H_INCLUDED
#define EXPORT_ZIP_H_INCLUDED

#include <glib.h>

typedef struct _ExportZipDatei {
	const gchar *name;  //Pfad im Archiv
	const void *data;   //muss bis zum Ende des Aufrufs gültig bleiben
	gsize len;
	gboolean store;     //unkomprimiert (z.B. mimetype, png, jpeg)
} ExportZipDatei;

/* Schreibt die Dateien in dieser Reihenfolge als ZIP nach filename. Das
 * Archiv entsteht im Speicher und wird mit g_file_set_contents()
 * geschrieben (UTF-8-Dateinamen auch unter Windows). Rückgabe 0 oder -1. */
gint export_zip_schreiben(const gchar *filename,
		const ExportZipDatei *dateien, guint anzahl, GError **error);

#endif // EXPORT_ZIP_H_INCLUDED
