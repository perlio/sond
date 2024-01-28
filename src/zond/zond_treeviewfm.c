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


//ZOND_PDF_ABSCHNITT
typedef struct
{
    gint ID;
    gchar* rel_path;
    gchar* icon_name;
    gchar* node_text;
    gint page_begin;
    gint index_beginn;
    gint page_end;
    gint index_end;
} ZondPdfAbschnittPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfAbschnitt, zond_pdf_abschnitt, ZOND_TYPE_PDF_ABSCHNITT)


static void
zond_pdf_abschnitt_finalize( GObject* self )
{
    ZondPdfAbschnittPrivate* zond_pdf_abschnitt_priv =
            zond_pdf_abschnitt_get_instance_private( ZOND_PDF_ABSCHNITT(self) );

    g_free( zond_pdf_abschnitt_priv->rel_path );
    g_free( zond_pdf_abschnitt_priv->icon_name );
    g_free( zond_pdf_abschnitt_priv->node_text );

    G_OBJECT_CLASS(zond_pdf_abschnitt_parent_class)->finalize( self );

    return;
}

static void
zond_pdf_abschnitt_class_init( ZondPdfAbschnittClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = zond_pdf_abschnitt_finalize;

    return;
}


static void
zond_pdf_abschnitt_init( ZondPdfAbschnitt* self )
{
    return;
}

//nun mit ZondTreeviewFM weiter
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
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = zond_dbase_update_path( dbase_store, rel_path_source, rel_path_dest, errmsg );
    if ( rc ) ERROR_ROLLBACK( dbase_store )

    rc = zond_dbase_update_path( dbase_work, rel_path_source, rel_path_dest, errmsg );
    if ( rc ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )

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


static void
zond_treeviewfm_render_icon( SondTreeviewFM* stvfm, GtkTreeIter* iter, GObject* object )
{
    if ( ZOND_IS_PDF_ABSCHNITT(object) )//PDF-Abschnitt
    {
        GtkCellRenderer* renderer = NULL;

        ZondPdfAbschnittPrivate* zond_pdf_abschnitt_priv =
                zond_pdf_abschnitt_get_instance_private( ZOND_PDF_ABSCHNITT(object) );

        renderer = sond_treeview_get_cell_renderer_icon( SOND_TREEVIEW(stvfm) );

        g_object_set( G_OBJECT(renderer), "icon-name", zond_pdf_abschnitt_priv->icon_name, NULL );
    }

    g_object_unref( object );

    return;
}



static gint
zond_treeviewfm_expand_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, GError** error )
{
    gint rc = 0;

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {

        return 0;
    }
    else if ( G_IS_FILE_INFO(object) )
    {
        const gchar* content_type = NULL;

        content_type = g_file_info_get_content_type( G_FILE_INFO(object) );
        if ( g_content_type_is_mime_type( content_type, "application/pdf" ) )
        {

            return 0;
        }
    }

    //chain-up, nur wenn nicht behandelt...
    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->expand_dummy( stvfm, iter, object, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    return 0;
}


static gint
zond_treeviewfm_insert_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, const gchar* content_type, GError** error )
{
    if ( g_content_type_is_mime_type( content_type, "application/pdf" ) )
    {
        gchar* rel_path = NULL;
        gint rc = 0;
        GtkTreeIter iter_newest = { 0 };

        ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );

        rc = zond_dbase_get_first_pdf_abschnitt( priv->zond->dbase_zond->zond_dbase_work,
                rel_path, NULL, &error );
        g_free( rel_path );

        if ( rc == -1 )
        {
            g_prefix_error( error, "%s\n", __func__ );

            return -1;
        }
        else if ( rc == 1 ) gtk_tree_store_insert(
            GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
            &iter_newest, iter, -1 );
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
    SOND_TREEVIEWFM_CLASS(klass)->insert_dummy = zond_treeviewfm_insert_dummy;
    SOND_TREEVIEWFM_CLASS(klass)->expand_dummy = zond_treeviewfm_expand_dummy;
    SOND_TREEVIEWFM_CLASS(klass)->render_icon = zond_treeviewfm_render_icon;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* ztvfm )
{
    return;
}






