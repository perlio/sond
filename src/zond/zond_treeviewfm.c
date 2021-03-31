#include "zond_treeviewfm.h"

#include "../misc.h"
#include "../dbase.h"
#include "../eingang.h"
#include "../sond_treeviewfm.h"

#include "global_types.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32



typedef struct
{
    Projekt* zond;
} ZondTreeviewFMPrivate;


G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeviewFM, zond_treeviewfm, SOND_TYPE_TREEVIEWFM)


static gint
zond_treeviewfm_dbase_begin( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_begin( SOND_TREEVIEWFM(stvfm), errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin (store)" )

    rc = dbase_begin( (DBase*) priv->zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK ( (DBase*) priv->zond->dbase_zond->dbase_work, "dbase_begin (work)" ) //dbase_store

    return 0;
}


static gint
zond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, GFile* file, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    DBase* dbase = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase_work = (DBase*) priv->zond->dbase_zond->dbase_work;

    gchar* rel_path = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), file );

    rc = dbase_test_path( dbase, rel_path, errmsg );
    if ( rc )
    {
        g_free( rel_path );
        if ( rc == -1 ) ERROR_ROLLBACK_BOTH( "dbase_test_path" )
        else if ( rc == 1 ) ROLLBACK_BOTH(dbase, (DBase*) priv->zond->dbase_zond->dbase_work)
    }

    rc = dbase_test_path( (DBase*) priv->zond->dbase_zond->dbase_work, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR_ROLLBACK_BOTH( "dbase_test (work)" )
    else if ( rc == 1 ) ROLLBACK_BOTH(dbase,(DBase*) priv->zond->dbase_zond->dbase_work)

    return 0;
}


static gint
zond_treeviewfm_dbase_update_path( SondTreeviewFM* stvfm, GFile* source, GFile* dest,
        gchar** errmsg )
{
    gint rc1 = 0;
    gint rc2 = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    DBase* dbase = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase_work = (DBase*)  priv->zond->dbase_zond->dbase_work;

    gchar* rel_path_source = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), source );
    gchar* rel_path_dest = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), dest );

    rc1 = dbase_update_path( dbase, rel_path_source, rel_path_dest, errmsg );
    rc2 = dbase_update_path( dbase_work, rel_path_source, rel_path_dest, errmsg );

    g_free( rel_path_source );
    g_free( rel_path_dest );

    if ( rc1 || rc2 ) ERROR_ROLLBACK_BOTH( "dbase_update_path" )

    return 0;
}


static gint
zond_treeviewfm_dbase_update_eingang( SondTreeviewFM* stvfm, GFile* source, GFile* dest,
        gchar** errmsg )
{

    return 0;
}


static gint
zond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_end( stvfm, suc, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_work,"dbase_end (store)" )

    if ( suc )
    {
        gint rc = 0;

        rc = dbase_commit( (DBase*) priv->zond->dbase_zond->dbase_work, errmsg );
        if ( rc ) ERROR_ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_work, "dbase_commit (work)" )
    }
    else ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_work )

    return 0;
}


static void
zond_treeviewfm_row_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    gboolean changed = FALSE;

    ZondTreeviewFM* ztvfm = (ZondTreeviewFM*) data;
    ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ztvfm );

    if ( ztvfm_priv->zond->dbase_zond->changed ) changed = TRUE;

    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->row_text_edited( cell, path_string, new_text, data );

    if ( !changed ) project_reset_changed( ztvfm_priv->zond );

    return;
}


static void
zond_treeviewfm_class_init( ZondTreeviewFMClass* klass )
{
    SOND_TREEVIEWFM_CLASS(klass)->dbase_begin = zond_treeviewfm_dbase_begin;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_test = zond_treeviewfm_dbase_test;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_update_path = zond_treeviewfm_dbase_update_path;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_update_eingang = zond_treeviewfm_dbase_update_eingang;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_end = zond_treeviewfm_dbase_end;
    SOND_TREEVIEWFM_CLASS(klass)->row_text_edited = zond_treeviewfm_row_text_edited;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* stvfm )
{

    return;
}


ZondTreeviewFM*
zond_treeviewfm_new( Clipboard* clipboard )
{
    ZondTreeviewFM* ztvfm = g_object_new( ZOND_TYPE_TREEVIEWFM, NULL );
    sond_treeview_set_clipboard( SOND_TREEVIEW(ztvfm), clipboard );

    return ztvfm;
}


void
zond_treeviewfm_set_zond( ZondTreeviewFM* ztvfm, Projekt* zond )
{
    ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ztvfm );

    ztvfm_priv->zond = zond;

    return;
}





