#include "zond_treeviewfm.h"

#include "../misc.h"
#include "zond_dbase.h"
#include "../sond_treeviewfm.h"

#include "10init/app_window.h"
#include "20allgemein/project.h"

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
    gint seite_von;
    gint index_von;
    gint seite_bis;
    gint index_bis;
    gchar* icon_name;
    gchar* node_text;
} ZondPdfAbschnittPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfAbschnitt, zond_pdf_abschnitt, G_TYPE_OBJECT)


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


void
zond_pdf_abschnitt_set( ZondPdfAbschnitt* zpa, gint ID, const gchar* rel_path,
        Anbindung anbindung, const gchar* icon_name, const gchar* node_text )
{
    ZondPdfAbschnittPrivate* zpa_priv = zond_pdf_abschnitt_get_instance_private( zpa );

    zpa_priv->ID = ID;
    zpa_priv->rel_path = g_strdup( rel_path );
    zpa_priv->seite_von = anbindung.von.seite;
    zpa_priv->index_von = anbindung.von.index;
    zpa_priv->seite_bis = anbindung.bis.seite;
    zpa_priv->index_bis = anbindung.bis.index;
    zpa_priv->icon_name = g_strdup( icon_name );
    zpa_priv->node_text = g_strdup( node_text );

    return;
}


void
zond_pdf_abschnitt_set_node_text( ZondPdfAbschnitt* zpa, gchar const* node_text )
{
    ZondPdfAbschnittPrivate* zpa_priv = zond_pdf_abschnitt_get_instance_private( zpa );

    zpa_priv->node_text = g_strdup( node_text );

    return;
}


gint
zond_pdf_abschnitt_get_ID( ZondPdfAbschnitt* zpa )
{
    ZondPdfAbschnittPrivate* zpa_priv = zond_pdf_abschnitt_get_instance_private( zpa );

    return zpa_priv->ID;
}


void
zond_pdf_abschnitt_get( ZondPdfAbschnitt* zpa, gint* ID, gchar const ** rel_path,
        Anbindung* anbindung, gchar const ** icon_name, gchar const ** node_text )
{
    ZondPdfAbschnittPrivate* zpa_priv = zond_pdf_abschnitt_get_instance_private( zpa );

    if ( ID ) *ID = zpa_priv->ID;
    if ( rel_path ) *rel_path = zpa_priv->rel_path;
    if ( anbindung )
    {
        (*anbindung).von.seite = zpa_priv->seite_von;
        (*anbindung).von.index = zpa_priv->index_von;
        (*anbindung).bis.seite = zpa_priv->seite_bis;
        (*anbindung).bis.index = zpa_priv->index_bis;
    }
    if ( icon_name ) *icon_name = zpa_priv->icon_name;
    if ( node_text ) *node_text = zpa_priv->node_text;

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
zond_treeviewfm_dbase_begin( SondTreeviewFM* stvfm, GError** error )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_begin( SOND_TREEVIEWFM(stvfm), error );
    if ( rc ) ERROR_Z

    rc = zond_dbase_begin( dbase_store, error );
    if ( rc ) ERROR_Z

    return 0;
}


static gint
zond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* rel_path_source,
        GError** error )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    ZondDBase* dbase_store = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_work = priv->zond->dbase_zond->zond_dbase_work;

    rc = zond_dbase_test_path( dbase_store, rel_path_source, error);
    if ( rc == -1 ) ERROR_Z
    else if ( rc == 1 ) return 1;

    rc = zond_dbase_test_path( dbase_work, rel_path_source, error );
    if ( rc == -1 ) ERROR_Z
    else if ( rc == 1 ) return 1;

    return 0;
}


static gint
zond_treeviewfm_dbase_update_path( SondTreeviewFM* stvfm,
        const gchar* rel_path_source, const gchar* rel_path_dest, GError** error )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = zond_dbase_update_path( dbase_store, rel_path_source, rel_path_dest, error );
    if ( rc ) ERROR_ROLLBACK_Z( dbase_store )

    rc = zond_dbase_update_path( dbase_work, rel_path_source, rel_path_dest, error );
    if ( rc ) ERROR_ROLLBACK_BOTH( dbase_work, priv->zond->dbase_zond->zond_dbase_store )

    return 0;
}


static gint
zond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, GError** error )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    ZondDBase* dbase_store = priv->zond->dbase_zond->zond_dbase_store;

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_end( stvfm, suc, error );
    if ( rc ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )

    if ( suc )
    {
        gint rc = 0;

        rc = zond_dbase_commit( priv->zond->dbase_zond->zond_dbase_store, error );
        if ( rc ) ERROR_ROLLBACK_BOTH( dbase_work, dbase_store )
    }
    else
    {
        gint rc = 0;

        rc = zond_dbase_rollback( dbase_store, error );
        if ( rc ) ERROR_Z
    }

    return 0;
}


static gint
zond_treeviewfm_text_edited( SondTreeviewFM* stvfm, GtkTreeIter* iter, GObject* object, const gchar* new_text,
        GError** error )
{
    gboolean changed = FALSE;

    ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

//überflüssig?
//    ztvfm_priv->zond->key_press_signal = g_signal_connect( ztvfm_priv->zond->app_window, "key-press-event",
//            G_CALLBACK(cb_key_press), ztvfm_priv->zond );

    if ( ztvfm_priv->zond->dbase_zond->changed ) changed = TRUE;

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gint rc = 0;
        gint ID_pdf_abschnitt = 0;

        ID_pdf_abschnitt = zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) );
        rc = zond_dbase_update_node_text( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                ID_pdf_abschnitt, new_text, error );
        if ( rc ) ERROR_Z

        //ToDo
        //zond_treeview ändern

        zond_pdf_abschnitt_set_node_text( ZOND_PDF_ABSCHNITT(object), new_text );

        if ( !changed ) project_reset_changed( ztvfm_priv->zond );

        return 0;
    }

    //chain-up, wenn nicht erledigt
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited( stvfm, iter, object, new_text, error );

    //unschön
    if ( !changed ) project_reset_changed( ztvfm_priv->zond );

    return 0;
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
zond_treeviewfm_render_text( SondTreeviewFM* stvfm, GtkTreeIter* iter, GObject* object, GError** error )
{
    gint rc = 0;

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gchar const* rel_path = NULL;
        Anbindung anbindung = { 0 };
        gchar* label = NULL;

        zond_pdf_abschnitt_get( ZOND_PDF_ABSCHNITT(object), NULL, &rel_path, &anbindung, NULL, NULL );

        label = g_strdup_printf( "%s, S. %i (Index %i)", rel_path, anbindung.von.seite, anbindung.von.index );
        if ( anbindung.bis.seite || anbindung.bis.index ) label = add_string( label,
                g_strdup_printf( "- S. %i (Index %i)", anbindung.bis.seite, anbindung.bis.index ) );
        g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(stvfm) )), "text",
                label, NULL );
        g_free( label );

        return 0;
    }

    //nur, wenn nicht erledigt
    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->render_text( stvfm, iter, object, error );
    if ( rc ) ERROR_Z

    return 0;
}


static void
zond_treeviewfm_render_icon( SondTreeviewFM* stvfm, GtkCellRenderer* renderer,
        GtkTreeIter* iter, GObject* object )
{
    if ( ZOND_IS_PDF_ABSCHNITT(object) )//PDF-Abschnitt
    {
        ZondPdfAbschnittPrivate* zond_pdf_abschnitt_priv =
                zond_pdf_abschnitt_get_instance_private( ZOND_PDF_ABSCHNITT(object) );

        g_object_set( G_OBJECT(renderer), "icon-name", zond_pdf_abschnitt_priv->icon_name, NULL );

        return;
    }

    //nur, wenn nicht erledigt
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->render_icon( stvfm, renderer, iter, object );

    return;
}



static gint
zond_treeviewfm_expand_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, GError** error )
{
    gint rc = 0;
    ZondPdfAbschnitt* zpa = NULL;
    gint parent = 0;

    ZondTreeviewFMPrivate* ztvfm_priv =
            zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
            parent = zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) );
    else if ( G_IS_FILE_INFO(object) )
    {
        const gchar* content_type = NULL;

        content_type = g_file_info_get_content_type( G_FILE_INFO(object) );
        if ( g_content_type_is_mime_type( content_type, "application/pdf" ) )
        {
            gint rc = 0;
            gchar* rel_path = NULL;

            rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
            rc = zond_dbase_get_pdf_root( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    rel_path, &parent, error );
            g_free( rel_path );
            if ( rc ) ERROR_Z
        }
    }

    if ( parent )
    {
        gint rc = 0;
        gint first_child = 0;
        gint child = 0;

        rc = zond_dbase_get_first_child( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                parent, &first_child, error );
        if ( rc ) ERROR_Z

        child = first_child;
        while ( child )
        {
            gint rc = 0;
            gchar* rel_path = NULL;
            Anbindung anbindung = { 0 };
            gchar* icon_name = NULL;
            gchar* node_text = NULL;
            GtkTreeIter iter_new = { 0 };
            gint younger_sibling = 0;

            rc = zond_dbase_get_node( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    child, NULL, NULL, &rel_path, &anbindung.von.seite,
                    &anbindung.von.index, &anbindung.bis.seite, &anbindung.bis.index,
                    &icon_name, &node_text, NULL, error );
            if ( rc ) ERROR_Z

            zpa = g_object_new( ZOND_TYPE_PDF_ABSCHNITT, NULL );

            zond_pdf_abschnitt_set( zpa, child, rel_path, anbindung, icon_name, node_text );
            g_free( rel_path );

            //child in tree einfügen
            gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
                    &iter_new, iter, -1 );
            gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
                    &iter_new, 0, zpa, -1 );

            g_object_unref( zpa );

            //nächstes Geschwister
            rc = zond_dbase_get_younger_sibling( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    child, &younger_sibling, error );
            if ( rc ) ERROR_Z

            child = younger_sibling;
        }
    }
    else
    {
        //chain-up, nur wenn nicht behandelt...
        rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->expand_dummy( stvfm, iter, object, error );
        if ( rc ) ERROR_Z
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
        gint pdf_root = 0;

        ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );

        rc = zond_dbase_get_pdf_root( priv->zond->dbase_zond->zond_dbase_work,
                rel_path, &pdf_root, error );
        if ( rc ) ERROR_Z

        if ( pdf_root )
        {
            gint rc = 0;
            gint pdf_abschnitt = 0;

            rc = zond_dbase_get_first_child( priv->zond->dbase_zond->zond_dbase_work,
                    pdf_root, &pdf_abschnitt, error );
            if ( rc ) ERROR_Z

            if ( pdf_abschnitt ) gtk_tree_store_insert(
                GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
                &iter_newest, iter, -1 );
        }

        return 0; //wenn behandelt, kein chain-up mehr!
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
    SOND_TREEVIEWFM_CLASS(klass)->render_text = zond_treeviewfm_render_text;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* ztvfm )
{
    return;
}






