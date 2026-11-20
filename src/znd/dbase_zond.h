#ifndef DBASE_ZOND_H_INCLUDED
#define DBASE_ZOND_H_INCLUDED

#include <glib.h>

typedef struct _ZondDBase ZondDBase;
typedef struct _SondIndexCtx SondIndexCtx;

typedef struct _DBase_Zond {
	ZondDBase *zond_dbase_store;
	ZondDBase *zond_dbase_work;
	gboolean changed;
	gboolean index_attached; //Index-DB als Schema idx an store gehängt
	gboolean sicherung_gemacht; //Kopie der Projektdatei vor Reparatur angelegt
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
gint dbase_zond_update_path(DBaseZond*, gchar const*, gchar const*, GError**);
gint dbase_zond_remove_filepart(DBaseZond*, gchar const*, GError**);
gint dbase_zond_update_gmessage_index(DBaseZond*, gchar const*, gint, gboolean, GError**);

#endif // DBASE_ZOND_H_INCLUDED
