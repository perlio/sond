 #include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../misc.h"

#include "../pdf_ocr.h"
#include "../zond_pdf_document.h"
#include "../zond_tree_store.h"

#include "../20allgemein/pdf_text.h"

#include "general.h"
#include "pdf.h"

/** rc == -1: Fähler
 rc == 0: alles ausgeführt, sämtliche Callbacks haben 0 zurückgegeben
 rc == 1: alles ausgeführt, mindestens ein Callback hat 1 zurückgegeben
 rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> sofortiger Abbruch
 **/
/*
 static gint
 dir_foreach( GFile* file, gboolean rec, gint (*foreach) ( GFile*, GFileInfo*, gpointer, gchar** ),
 gpointer data, gchar** errmsg )
 {
 GError* error = NULL;
 gboolean flag = FALSE;
 GFileEnumerator* enumer = NULL;

 enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
 if ( !enumer )
 {
 if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
 error->message, NULL );
 g_error_free( error );

 return -1;
 }

 while ( 1 )
 {
 GFile* file_child = NULL;
 GFileInfo* info_child = NULL;

 if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
 {
 if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
 error->message, NULL );
 g_error_free( error );
 g_object_unref( enumer );

 return -1;
 }

 if ( file_child ) //es gibt noch Datei in Verzeichnis
 {
 gint rc = 0;

 rc = foreach( file_child, info_child, data, errmsg );
 if ( rc == -1 )
 {
 g_object_unref( enumer );
 if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
 *errmsg );

 return -1;
 }
 else if ( rc == 1 ) flag = TRUE;
 else if ( rc == 2 ) //Abbruch gewählt
 {
 g_object_unref( enumer );
 return 2;
 }

 if ( rec && g_file_info_get_file_type( info_child ) == G_FILE_TYPE_DIRECTORY )
 {
 gint rc = 0;

 rc = dir_foreach( file_child, TRUE, foreach, data, errmsg );
 if ( rc == -1 )
 {
 g_object_unref( enumer );
 if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
 *errmsg );

 return -1;
 }
 else if ( rc == 1 ) flag = TRUE;//Abbruch gewählt
 else if ( rc == 2 )
 {
 g_object_unref( enumer );
 return 2;
 }
 }
 } //ende if ( file_child )
 else break;
 }

 g_object_unref( enumer );

 return (flag) ? 1 : 0;
 }


 gint
 test_II( Projekt* zond, gchar** errmsg )
 {
 const gchar* root = "C:/Users/nc-kr/laufende Akten";
 InfoWindow* info_window = NULL;
 gint rc = 0;

 file_root = g_file_new_for_path( root );
 info_window = info_window_open( zond->app_window, "Untersuchung auf InlineImages" );
 rc = dir_foreach( file_root, TRUE, test_pdf, info_window, errmsg );
 info_window_close( info_window );
 if ( rc == -1 ) ERROR_S

 return 0;
 }
 */
#include <gio/gio.h>
#include <stdio.h>

int test(Projekt* zond, gchar **errmsg) {
    const char *path = "Adressliste.xlsx";
    GError* error = NULL;

    /* GFile anlegen */
    GFile *file = g_file_new_for_path(path);

    /* FileInfo mit content-type anfragen */
    GFileInfo *info = g_file_query_info(file,
                                        "standard::content-type",
                                        G_FILE_QUERY_INFO_NONE,
                                        NULL, /* GCancellable */
                                        &error);
    if (!info) {
        g_printerr("g_file_query_info failed: %s\n", error->message);
        g_clear_error(&error);
        g_object_unref(file);
        return 2;
    }

    const char *content_type = g_file_info_get_content_type(info);
    if (!content_type) {
        g_printerr("Keine content-type info gefunden für %s\n", path);
        g_object_unref(info);
        g_object_unref(file);
        return 3;
    }

    g_print("Content type: %s\n", content_type);

    /* Default AppInfo ermitteln */
    GAppInfo *app = g_app_info_get_default_for_type(content_type, /*must_support_uris=*/TRUE);
    if (!app) {
    	printf("Fähler\n");
        g_printerr("Keine Default-Anwendung für %s gefunden\n", content_type);
        g_object_unref(info);
        g_object_unref(file);
        return 4;
    }

    const char *cmd = g_app_info_get_commandline(app);
    const char *exe = g_app_info_get_executable(app);
    g_print("App commandline: %s\n", cmd ? cmd : "(none)");
    g_print("App executable: %s\n", exe ? exe : "(none)");

    /* Optional: App wirklich mit der Datei starten */
    if (!g_app_info_launch(app, (GList*)g_list_prepend(NULL, g_file_dup(file)), NULL, &error)) {
        g_printerr("Starten der Anwendung fehlgeschlagen: %s\n", error->message);
        g_clear_error(&error);
    } else {
        g_print("Anwendung gestartet (oder Start angefragt)\n");
    }

    /* aufräumen */
    g_object_unref(app);
    g_object_unref(info);
    g_object_unref(file);
    return 0;
}

void pdf_print_buffer(fz_context *ctx, fz_buffer *buf) {
	gchar *data = NULL;
	gchar *pos = NULL;
	size_t size = 0;

	size = fz_buffer_storage(ctx, buf, (guchar**) &data);
	pos = data;
	for (gint i = 0; i < size; i++) {
		if (!(*pos == 0 || *pos == 9 || *pos == 10 || *pos == 12 || *pos == 13
				|| *pos == 31))
			printf("%c", *pos);
		else if (*pos == 10 || *pos == 13)
			printf("\n");
		else if (*pos == 0)
			printf("(null)\n");
		else
			printf(" ");
		pos++;
	}

	return;
}

fz_buffer*
pdf_ocr_get_content_stream_as_buffer(fz_context *ctx, pdf_obj *page_ref,
		gchar **errmsg);

gint pdf_print_content_stream(fz_context *ctx, pdf_obj *page_ref,
		GError **error) {
	gchar* errmsg = NULL;
	fz_buffer *buf = NULL;

	buf = pdf_ocr_get_content_stream_as_buffer(ctx, page_ref, &errmsg);
	if (!buf) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__, errmsg ? errmsg : "");
		g_free(errmsg);
		return -1;
	}

	pdf_print_buffer(ctx, buf);

	fz_drop_buffer(ctx, buf);

	return 0;
}

