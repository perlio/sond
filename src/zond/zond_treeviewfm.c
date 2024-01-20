#include "zond_treeviewfm.h"

#include "../misc.h"
#include "zond_dbase.h"
#include "../sond_treeviewfm.h"

#include "10init/app_window.h"

#include "global_types.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32


typedef enum
{
    PROP_PROJEKT = 1,
    N_PROPERTIES
} ZondTreeviewFMProperty;

typedef struct
{
    Projekt* zond;
} ZondTreeviewFMPrivate;


G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeviewFM, zond_treeviewfm, SOND_TYPE_TREEVIEWFM)

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void
zond_treeviewfm_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    ZondTreeviewFM* self = ZOND_TREEVIEWFM(object);
    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( self );

    switch ((ZondTreeviewFMProperty) property_id)
    {
    case PROP_PROJEKT:
      priv->zond = g_value_get_pointer(value);
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
zond_treeviewfm_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    ZondTreeviewFM* self = ZOND_TREEVIEWFM(object);
    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( self );

    switch ((ZondTreeviewFMProperty) property_id)
    {
        case PROP_PROJEKT:
                g_value_set_pointer( value, priv->zond );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
}


static gint
zond_treeviewfm_dbase_begin( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_begin( SOND_TREEVIEWFM(stvfm), errmsg );
    if ( rc ) ERROR_S

    rc = zond_dbase_begin( dbase_store, errmsg );
    if ( rc ) ERROR_ROLLBACK ( dbase_work ) //dbase_store

    return 0;
}


static gint
zond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* rel_path_source,
        gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    ZondDBase* dbase_store = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_work = priv->zond->dbase_zond->zond_dbase_work;

    rc = zond_dbase_test_path( dbase_store, rel_path_source, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 1;

    rc = zond_dbase_test_path( dbase_work, rel_path_source, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 1;

    return 0;
}


static gint
zond_treeviewfm_dbase_update_path( SondTreeviewFM* stvfm,
        const gchar* rel_path_source, const gchar* rel_path_dest, gchar** errmsg )
{
    gint rc1 = 0;
    gint rc2 = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc1 = zond_dbase_update_path( dbase_store, rel_path_source, rel_path_dest, errmsg );
    rc2 = zond_dbase_update_path( dbase_work, rel_path_source, rel_path_dest, errmsg );

    if ( rc1 || rc2 ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )

    return 0;
}


static gint
zond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_end( stvfm, suc, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )

    if ( suc )
    {
        gint rc = 0;

        rc = zond_dbase_commit( priv->zond->dbase_zond->zond_dbase_store, errmsg );
        if ( rc ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )
    }
    else
    {
        gint rc = 0;

        rc = zond_dbase_rollback( dbase_store, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


static void
zond_treeviewfm_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    gboolean changed = FALSE;

    ZondTreeviewFM* ztvfm = (ZondTreeviewFM*) data;
    ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ztvfm );

    ztvfm_priv->zond->key_press_signal = g_signal_connect( ztvfm_priv->zond->app_window, "key-press-event",
            G_CALLBACK(cb_key_press), ztvfm_priv->zond );

    if ( ztvfm_priv->zond->dbase_zond->changed ) changed = TRUE;

    //chain-up
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited( cell, path_string, new_text, data );

    if ( !changed ) project_reset_changed( ztvfm_priv->zond );

    return;
}


static void
zond_treeviewfm_results_row_activated( GtkWidget* listbox, GtkWidget* row, gpointer data )
{
    ZondTreeviewFM* ztvfm = (ZondTreeviewFM*) data;
    ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ztvfm );

    //wenn FS nicht angezeigt: erst einschalten, damit man was sieht
    if ( !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button) ) )
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ztvfm_priv->zond->fs_button), TRUE );

    //chain-up
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->results_row_activated( listbox, row, data );

    return;
}


static gint
zond_treeviewfm_expand_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, GError** error )
{
    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {

    }
    else if ( G_IS_FILE_TYPE(object) )
    {

    }
    //chain-up, nur wenn nicht behandelt
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->results_row_activated( listbox, row, data );

    return;
}


static gint
zond_treeviewfm_insert_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, const gchar* content_type, GError** error )
{
    if ( g_content_type_is_mime_type( content_type, "application/pdf" ) )
    {
        gchar* rel_path = NULL;
        gint rc = 0;
        GtkTreeiter iter_newest = { 0 };

        ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );

        rc = zond_dbase_get_first_pdf_abschnitt( priv->zond->dbase_zond->zond_dbase_work,
                rel_path, NULL, &error );
        g_free( rel_path );

        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                    error->message, NULL );
            g_error_free( error );

            return -1;
        }
        else if ( rc == 1 ) gtk_tree_store_insert(
            GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
            &iter_newest, &iter_new, -1 );
    }

    //chain-up
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->insert_dummy( stvfm, iter, content_type, error );

    return 0;
}


static void
zond_treeviewfm_class_init( ZondTreeviewFMClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->set_property = zond_treeviewfm_set_property;
    object_class->get_property = zond_treeviewfm_get_property;

    obj_properties[PROP_PROJEKT] =
            g_param_spec_pointer ("Projekt",
                                  "Projekt",
                                  "Kontext-Struktur.",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

   g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    SOND_TREEVIEWFM_CLASS(klass)->dbase_begin = zond_treeviewfm_dbase_begin;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_test = zond_treeviewfm_dbase_test;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_update_path = zond_treeviewfm_dbase_update_path;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_end = zond_treeviewfm_dbase_end;
    SOND_TREEVIEWFM_CLASS(klass)->text_edited = zond_treeviewfm_text_edited;
    SOND_TREEVIEWFM_CLASS(klass)->results_row_activated = zond_treeviewfm_results_row_activated;
    SOND_TREEVIEWDM_CLASS(klass)->insert_dummy = zond_treeviewfm_insert_dummy;
    SOND_TREEVIEWDM_CLASS(klass)->expand_dummy = zond_treeviewfm_expand_dummy;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* ztvfm )
{
    return;
}






