#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

#include "../dbase_zond.h"

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _GtkWidget GtkWidget;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _ZondDBase ZondDBase;
typedef struct _SondFilePart SondFilePart;
typedef struct _GError GError;
typedef struct _SondIndexCtx SondIndexCtx;

typedef void *gpointer;
typedef int gint;
typedef char gchar;

/* Intervall des automatischen Speicherns (Projekt öffnen und Menü) */
#define AUTOSAVE_INTERVAL_SECONDS (5 * 60)

// Project state management
void project_reset_changed(Projekt*, gboolean);
void project_set_widgets_sensitive(Projekt*, gboolean);
gboolean project_timeout_autosave(gpointer);

// Project operations
gint project_close(Projekt*, GError**);
gint project_save(Projekt*, GError**);
gint project_load_trees(Projekt*, GError**);

/* Kopie der Projektdatei neben dem Original, einmal pro geöffnetem Projekt
 * (vor der ersten Reparatur). *path_bak (g_free) nur, wenn gerade angelegt. */
gint project_sicherungskopie(Projekt*, gchar**, GError**);
gint project_open(Projekt*, const gchar*, gboolean, GError**);
gint project_load(Projekt* zond, GError** error);
gint project_new(Projekt* zond, GError** error);

/* Pfad zu einer GGUF-Modelldatei ermitteln (GSettings-Override oder
 * <exe_dir>/../models/<default_filename>) - auch außerhalb von project.c
 * genutzt (zond_chat.c), um denselben Standardpfad für das Chat-Modell
 * zu ermitteln wie project_open() ihn fürs Embedding-Modell verwendet. */
gchar* resolve_model_path(Projekt *zond, gchar const *settings_key,
		gchar const *default_filename);

/* Lokalen, deterministischen Pfad für die work-Datenbank (SQLite-
 * "Arbeitskopie", laufend fortgeschrieben, s. project_create_dbase_zond())
 * ermitteln. Liegt bewußt AUSSERHALB des Projektordners, da dieser bei
 * Cloud-Sync-Laufwerken (SeaDrive etc.) liegen kann - eine dort laufend
 * gelesene/geschriebene SQLite-Datei kollidiert mit der Sync-Aktivität
 * des Cloud-Clients und verlangsamt jeden einzelnen Datenbankzugriff
 * erheblich). Der Pfad wird deterministisch aus dem vollen (absoluten)
 * Projektpfad abgeleitet (SHA-256-Hash) - derselbe project_path liefert
 * immer denselben lokalen Pfad, sowohl beim Anlegen
 * (project_create_dbase_zond()) als auch bei der Absturz-Wiederherstellung
 * (zond_init.c, recover()). Bewußt NICHT nur der Projektname (Basename):
 * zwei verschiedene Projekte mit gleichem Dateinamen in unterschiedlichen
 * Ordnern würden sonst auf denselben lokalen Pfad kollidieren. */
gchar* project_get_local_tmp_path(gchar const *project_path, GError **error);

/* Arbeitskopie path_tmp zur Projektdatei machen (alte Projektdatei wird
 * <project>.bak_<Zeit>). Spielt vorher liegengebliebene Journale ab. */
gboolean project_arbeitskopie_uebernehmen(gchar const *project,
		gchar const *path_tmp, GError **error);

/* Arbeitskopie path_tmp als <project>.recovery_<Zeit> aufbewahren (statt sie
 * zu überschreiben oder liegen zu lassen). *path_neu (g_free) = neuer Pfad. */
gboolean project_arbeitskopie_sichern(gchar const *project,
		gchar const *path_tmp, gchar **path_neu, GError **error);

/* Container-bewusste Alternative zu filename_oeffnen() (misc.c) - öffnet
 * einen modalen Dialog mit einem eigenen, frischen Dateiverzeichnis-Baum
 * (ZondTreeviewFM), der - anders als der normale GTK-Dateiauswahldialog -
 * auch in ZIP-Archive hinabsteigen kann. s. ausführlichen Kommentar an
 * der Definition (project.c).
 *
 * extra (optional, darf NULL sein): zusätzliches Widget (z.B. Checkboxen),
 * das unterhalb des Dateibaums im Dialog angezeigt wird. Der Dialog wird
 * vor der Rückkehr zerstört (und damit auch 'extra') - Zustände daher
 * über Signalhandler (z.B. "toggled") in Aufruferdaten festhalten.
 *
 * Rückgabe: neue Referenz auf das ausgewählte SondFilePart (Aufrufer muss
 * g_object_unref()en), oder NULL (*error == NULL: Nutzer hat
 * abgebrochen; *error gesetzt: echter Fehler). */
SondFilePart* filepart_oeffnen(Projekt *zond, GtkWidget *extra,
		GError **error);

#endif // PROJECT_H_INCLUDED
