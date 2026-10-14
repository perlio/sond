#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _ZondDBase ZondDBase;
typedef struct _SondFilePart SondFilePart;
typedef struct _GError GError;
typedef struct _SondIndexCtx SondIndexCtx;

typedef void *gpointer;
typedef int gint;
typedef char gchar;

typedef struct _DBase_Zond {
	ZondDBase *zond_dbase_store;
	ZondDBase *zond_dbase_work;
	gboolean changed;
	gboolean index_attached; //Index-DB als Schema idx an store gehängt
} DBaseZond;

// Database transaction functions
gint dbase_zond_begin(DBaseZond*, GError**);
void dbase_zond_rollback(DBaseZond*, GError**);
gint dbase_zond_commit(DBaseZond*, GError**);

/* Index-DB (.sond_index.db) als drittes Schema "idx" an die store-Connection
 * hängen bzw. wieder lösen. Beides nur außerhalb einer Transaktion: attach
 * VOR dbase_zond_begin(), detach NACH commit/rollback. Solange idx hängt,
 * gehören die Schreibzugriffe auf die Index-DB zur Dual-Write-Transaktion
 * (ein Commit für store, work und idx). */
gint dbase_zond_attach_index(DBaseZond*, gchar const*, GError**);
void dbase_zond_detach_index(DBaseZond*);

/* Index-Kontext fuer die Schreibzugriffe in der Transaktion: Kopie von src, die
 * ueber die store-Connection (Schema idx) arbeitet. Nur nach attach benutzen. */
void dbase_zond_index_ctx(DBaseZond*, SondIndexCtx*, SondIndexCtx const*);

// Database update functions
gint dbase_zond_update_sections(DBaseZond*, DisplayedDocument*, GError**);
gint dbase_zond_update_path(DBaseZond*, gchar const*, gchar const*, GError**);
gint dbase_zond_remove_filepart(DBaseZond*, gchar const*, GError**);
gint dbase_zond_update_gmessage_index(DBaseZond*, gchar const*, gint, gboolean, GError**);

// Project state management
void project_reset_changed(Projekt*, gboolean);
void project_set_widgets_sensitive(Projekt*, gboolean);
gboolean project_timeout_autosave(gpointer);

// Project operations
gint project_close(Projekt*, GError**);
gint project_save(Projekt*, GError**);
gint project_load_trees(Projekt*, GError**);
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

/* Container-bewusste Alternative zu filename_oeffnen() (misc.c) - öffnet
 * einen modalen Dialog mit einem eigenen, frischen Dateiverzeichnis-Baum
 * (ZondTreeviewFM), der - anders als der normale GTK-Dateiauswahldialog -
 * auch in ZIP-Archive hinabsteigen kann. s. ausführlichen Kommentar an
 * der Definition (project.c).
 *
 * Rückgabe: neue Referenz auf das ausgewählte SondFilePart (Aufrufer muss
 * g_object_unref()en), oder NULL (*error == NULL: Nutzer hat
 * abgebrochen; *error gesetzt: echter Fehler). */
SondFilePart* filepart_oeffnen(Projekt *zond, GError **error);

#endif // PROJECT_H_INCLUDED
