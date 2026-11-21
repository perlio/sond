#ifndef DBASE_ZOND_H_INCLUDED
#define DBASE_ZOND_H_INCLUDED

#include <glib.h>

typedef struct _ZondDBase ZondDBase;
typedef struct _SondIndexCtx SondIndexCtx;

/* Die ZND-Datenbank eines Projekts: store (die Projektdatei) und, nur in zond, work (die lokale
 * Arbeitskopie, per ATTACH als Schema "work" an die store-Connection gehängt). Operationen über
 * beide laufen in einer Transaktion auf der store-Connection (atomar). Ohne work (zond_dbase_work
 * ist NULL, z.B. im Client) wirkt alles nur auf store. */
typedef struct _DBase_Zond {
	ZondDBase *zond_dbase_store;
	ZondDBase *zond_dbase_work; //NULL ohne Arbeitskopie
	gboolean changed;
	gboolean index_attached; //Index-DB als Schema idx an store gehängt
	gboolean sicherung_gemacht; //Kopie der Projektdatei vor Reparatur angelegt
} DBaseZond;

/* Nur store, ohne work (Client): öffnet die Projektdatei. Die Verbindung wird so geöffnet wie
 * in zond (s. zond_dbase_new()). NULL bei Fehler. */
DBaseZond* dbase_zond_new_store(gchar const *path, GError **error);

/* Wie dbase_zond_new_store(), aber nur lesend (s. zond_dbase_new_readonly()). Schreibende
 * Funktionen (Transaktionen, Index) dürfen auf so geöffnetem Objekt nicht aufgerufen werden. */
DBaseZond* dbase_zond_new_store_readonly(gchar const *path, GError **error);

//Gibt die Datenbanken (auch work, falls vorhanden) und die Struktur frei
void dbase_zond_free(DBaseZond*);

/* Die Datenbank, auf der gelesen und der Anbindungsbaum geändert wird: work, sonst store */
ZondDBase* dbase_zond_primary(DBaseZond*);

/* Ergebnis der Prüfung, ob etwas angebunden ist: REF_PRIMARY: in der primären DB (work, sonst
 * store), REF_ONLY_STORE: nur im store (in work schon entfernt, aber noch nicht gespeichert) */
typedef enum {
	DBASE_ZOND_REF_NONE = 0,
	DBASE_ZOND_REF_PRIMARY,
	DBASE_ZOND_REF_ONLY_STORE
} DBaseZondRef;

/* Ist ein Pfad (Präfix) bzw. ein Abschnitt einer Datei angebunden? 0 mit *ref, -1 bei Fehler.
 * Wie zond_dbase_test_path() und zond_dbase_test_path_section(), aber über alle vorhandenen DBs. */
gint dbase_zond_test_path(DBaseZond*, gchar const *path_prefix, DBaseZondRef *ref, GError**);
gint dbase_zond_test_path_section(DBaseZond*, gchar const *filepart, gchar const *section,
		gboolean smaller, DBaseZondRef *ref, GError**);

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
gint dbase_zond_update_path(DBaseZond*, gchar const*, gchar const*, GError**);
gint dbase_zond_remove_filepart(DBaseZond*, gchar const*, GError**);
gint dbase_zond_update_gmessage_index(DBaseZond*, gchar const*, gint, gboolean, GError**);

#endif // DBASE_ZOND_H_INCLUDED
