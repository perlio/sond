#include "zond_treeviewfm.h"

#include "../misc.h"
#include "../sond_treeviewfm.h"

#include "zond_dbase.h"
#include "zond_treeview.h"

#include "10init/app_window.h"
#include "20allgemein/project.h"
#include "20allgemein/oeffnen.h"
#include "99conv/general.h"

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
    if ( rc ) ERROR_ROLLBACK_Z( dbase_work );

    return 0;
}


static gint
zond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* rel_path_source,
        GError** error )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    ZondDBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );

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

    if ( ztvfm_priv->zond->dbase_zond->changed ) changed = TRUE;

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gint rc = 0;
        gint ID_pdf_abschnitt = 0;

        ID_pdf_abschnitt = zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) );
        rc = zond_dbase_update_node_text( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                ID_pdf_abschnitt, new_text, error );
        if ( rc ) ERROR_Z

        zond_pdf_abschnitt_set_node_text( ZOND_PDF_ABSCHNITT(object), new_text );

        //zond_treeview ändern
        zond_treeview_set_text_pdf_abschnitt( ZOND_TREEVIEW(ztvfm_priv->zond->treeview[BAUM_INHALT]),
                ID_pdf_abschnitt, new_text );
    }
    //chain-up, wenn nicht erledigt
    else SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->text_edited( stvfm, iter, object, new_text, error );

    if ( !changed ) project_reset_changed( ztvfm_priv->zond, FALSE );

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
zond_treeviewfm_render_text( SondTreeviewFM* stvfm, GtkTreeIter* iter, GObject* object,
        gchar const** node_text, gboolean* background, GError** error )
{
    if ( G_IS_FILE_INFO(object) )
    {
        gchar* rel_path = NULL;
        gint rc = 0;
        gchar* file_part = NULL;
        gint file_part_root = 0;

        ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        *node_text = g_file_info_get_name( G_FILE_INFO(object) );

        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
        file_part = g_strdup_printf( "/%s//", rel_path );
        g_free( rel_path );

        rc = zond_dbase_get_file_part_root( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                file_part, &file_part_root, error );
        g_free( file_part );
        if ( rc ) ERROR_Z

        if ( !file_part_root ) return 0; //file_part nicht angelegt - nicht ausgrauen
        else //prüfen, ob überhaupt angebunden
        {
            gint rc = 0;
            gint baum_inhalt_file = 0;

            rc = zond_dbase_find_baum_inhalt_file( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    file_part_root, &baum_inhalt_file, NULL, NULL, error );
            if ( rc ) ERROR_Z

            if ( baum_inhalt_file ) *background = TRUE;

            return 0;
        }
    }
    else if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gint rc = 0;
        gint ID = 0;
        gint baum_inhalt_file = 0;

        ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        zond_pdf_abschnitt_get( ZOND_PDF_ABSCHNITT(object), &ID, NULL, NULL, NULL, node_text );

        //Testen, ob grau eingefärbt werden soll, weil Anbindung angebunden
        rc = zond_dbase_find_baum_inhalt_file( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                ID, &baum_inhalt_file, NULL, NULL, error );
        if ( rc ) ERROR_Z

        if ( baum_inhalt_file ) *background = TRUE;

        return 0;
    }
    else //object ist file_part
    {

    }

    //kein chain-up!

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
    else if ( G_IS_FILE_INFO(object) )
    {
        gchar* rel_path = NULL;
        gint rc = 0;
        GError* error = NULL;
        gint file_part_root = 0;
        gchar* icon_name = NULL;
        gchar* file_part = NULL;

        ZondTreeviewFMPrivate* ztvfm_priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        //prüfen, ob in dbase gespeichert
        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
        file_part = g_strdup_printf( "/%s//", rel_path );
        g_free( rel_path );
        rc = zond_dbase_get_file_part_root( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                file_part, &file_part_root, &error );
        g_free( file_part );
        if ( rc )
        {
            display_error( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Icon konnte nicht gerendert werden",
                    error->message );
            g_error_free( error );

            return;
        }

        if ( file_part_root )
        {
            gint rc = 0;

            rc = zond_dbase_get_node( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    file_part_root, NULL, NULL, NULL, NULL, &icon_name, NULL, NULL, &error );
            if ( rc )
            {
                display_error( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Icon konnte nicht gerendert werden",
                        error->message );
                g_error_free( error );

                return;
            }

            g_object_set( G_OBJECT(renderer), "icon-name", icon_name, NULL );
            g_free( icon_name );

            return;
        }
    }

    //nur, wenn nicht erledigt
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->render_icon( stvfm, renderer, iter, object );

    return;
}


gint
zond_treeviewfm_insert_section( ZondTreeviewFM* ztvfm, gint node_id,
        GtkTreeIter* iter_anchor, gboolean child, GtkTreeIter* iter_inserted, GError** error )
{
    gint rc = 0;
    ZondPdfAbschnitt* zpa = NULL;
    gchar* file_part = NULL;
    gchar* section = NULL;
    Anbindung anbindung = { 0 };
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    GtkTreeIter iter_new = { 0 };
    gint first_grandchild = 0;

    ZondTreeviewFMPrivate* ztvfm_priv =
            zond_treeviewfm_get_instance_private( ztvfm );

    rc = zond_dbase_get_node( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
            node_id, NULL, NULL, &file_part, &section, &icon_name, &node_text, NULL, error );
    if ( rc ) ERROR_Z

    if ( !section )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKnoten enthält keine section", __func__ );
        g_free( file_part );

        return -1;
    }

    anbindung_parse_file_section( section, &anbindung );
    g_free( section );

    zpa = g_object_new( ZOND_TYPE_PDF_ABSCHNITT, NULL );

    zond_pdf_abschnitt_set( zpa, node_id, file_part, anbindung, icon_name, node_text );
    g_free( file_part );
    g_free( icon_name );
    g_free( node_text );

    //child in tree einfügen
    gtk_tree_store_insert_after( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
            &iter_new, (child) ? iter_anchor : NULL, (child) ? NULL : iter_anchor );
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )),
            &iter_new, 0, zpa, -1 );

    g_object_unref( zpa );

    //insert dummy
    zond_dbase_get_first_child( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &first_grandchild, error );
    if ( rc ) ERROR_Z

    if ( first_grandchild )
    {
        GtkTreeIter iter_tmp = { 0 };

        gtk_tree_store_insert( GTK_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztvfm) )), &iter_tmp, &iter_new, -1 );
    }

    if ( iter_inserted ) *iter_inserted = iter_new;

    return 0;
}


static gint
zond_treeviewfm_expand_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, GError** error )
{
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
            gchar* file_part = NULL;

            rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
            file_part = g_strdup_printf( "/%s//", rel_path );
            g_free( rel_path );
            rc = zond_dbase_get_file_part_root( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    file_part, &parent, error );
            g_free( file_part );
            if ( rc ) ERROR_Z
        }
    }
    //else if ( MIME_PART oder ZIP
    //dann mime prüfen und falls application/pdf: prüfen, ob in dbase - dann ist parent = ID

    //parent muß immer > 0 sein, sonst hätte gar kein dummy gesetzt werden können, der expand erlaubt
    if ( parent )
    {
        gint rc = 0;
        gint id_child = 0;
        gboolean child = TRUE;
        GtkTreeIter iter_anchor = { 0 };

        rc = zond_dbase_get_first_child( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                parent, &id_child, error );
        if ( rc ) ERROR_Z

        iter_anchor = *iter;
        while ( id_child )
        {
            gint rc = 0;
            GtkTreeIter iter_inserted = { 0 };
            gint younger_sibling = 0;

            //hier wird auch dummy eingefügt, wenn erforderlich;
            rc = zond_treeviewfm_insert_section( ZOND_TREEVIEWFM(stvfm), id_child, &iter_anchor, child, &iter_inserted, error );
            if ( rc ) ERROR_Z

            //nächstes Geschwister
            rc = zond_dbase_get_younger_sibling( ztvfm_priv->zond->dbase_zond->zond_dbase_work,
                    id_child, &younger_sibling, error );
            if ( rc ) ERROR_Z

            id_child = younger_sibling;
            iter_anchor = iter_inserted;
            child = FALSE;
        }
    }
    else
    {
        gint rc = 0;

        //chain-up, nur wenn nicht behandelt...
        rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->expand_dummy( stvfm, iter, object, error );
        if ( rc ) ERROR_Z
    }

    return 0;
}


static gint
zond_treeviewfm_insert_dummy( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, GError** error )
{
    if ( G_IS_FILE_INFO(object) )
    {
        gchar* rel_path = NULL;
        gchar* file_part = NULL;
        gint rc = 0;
        GtkTreeIter iter_newest = { 0 };
        gint pdf_root = 0;
        gchar const* content_type = NULL;

        content_type = g_file_info_get_content_type( G_FILE_INFO(object) );
        if ( g_content_type_is_mime_type( content_type, "application/pdf" ) );
        ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

        rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
        file_part = g_strdup_printf( "/%s//", rel_path );
        g_free( rel_path );

        rc = zond_dbase_get_file_part_root( priv->zond->dbase_zond->zond_dbase_work,
                file_part, &pdf_root, error );
        g_free( file_part );
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
    // else if ( mime_part oder zip-file, das pdf ist)

    //chain-up
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->insert_dummy( stvfm, iter, object, error );

    return 0;
}


static gint
zond_treeviewfm_open_row( SondTreeviewFM* stvfm,
        GtkTreeIter* iter, GObject* object, gboolean open_with, GError** error )
{
    ZondTreeviewFMPrivate* ztvfm_priv =
            zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    if ( G_IS_FILE_INFO(object) && !open_with )
    {
        const gchar* content_type = NULL;

        content_type = g_file_info_get_content_type( G_FILE_INFO(object) );
        if ( g_content_type_is_mime_type( content_type, "application/pdf" ) )
        {
            gint rc = 0;
            gchar* rel_path = NULL;
            gchar* errmsg = NULL;

            rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );

            rc = oeffnen_internal_viewer( ztvfm_priv->zond, rel_path, NULL, NULL, &errmsg );
            g_free( rel_path );
            if ( rc )
            {
                g_prefix_error( error, "%s\n%s", __func__, errmsg );
                g_free( errmsg );

                return -1;
            }

            return 0;
        }
    }
    else if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gchar* errmsg = NULL;
        gint rc = 0;
        PdfPos pdf_pos = { 0 };

        ZondPdfAbschnitt* zpa = ZOND_PDF_ABSCHNITT(object);
        ZondPdfAbschnittPrivate* zpa_priv = zond_pdf_abschnitt_get_instance_private( zpa );

        pdf_pos.seite = zpa_priv->seite_von;
        pdf_pos.index = zpa_priv->index_von;

        rc = oeffnen_internal_viewer( ztvfm_priv->zond, zpa_priv->rel_path, NULL, &pdf_pos, &errmsg );
        if ( rc )
        {
            g_prefix_error( error, "%s\n%s", __func__, errmsg );
            g_free( errmsg );

            return -1;
        }

        return 0;
    }

    //chain-up, falls nicht bearbeitet
    SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->open_row( stvfm,
            iter, object, open_with, error );

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
    SOND_TREEVIEWFM_CLASS(klass)->open_row = zond_treeviewfm_open_row;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* ztvfm )
{
    return;
}


typedef struct
{
    gint ID;
    gchar const* text_new;
    ZondTreeviewFM* ztvfm;
} FMForeach;

static gboolean
zond_treeviewfm_foreach_pdf_abschnitt( GtkTreeModel* model, GtkTreePath* path,
        GtkTreeIter* iter, gpointer data )
{
    gboolean ret = FALSE;
    GObject* object = NULL;

    FMForeach* fm_foreach = (FMForeach*) data;

    gtk_tree_model_get( model, iter, 0, &object, -1 );

    if ( !object ) return FALSE;

    if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        if ( fm_foreach->ID == zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) ) )
        {
            zond_pdf_abschnitt_set_node_text( ZOND_PDF_ABSCHNITT(object), fm_foreach->text_new );
            gtk_tree_view_columns_autosize( GTK_TREE_VIEW(fm_foreach->ztvfm) );

            ret = TRUE;
        }
    }

    g_object_unref( object );

    return ret;
}


void
zond_treeviewfm_set_pdf_abschnitt( ZondTreeviewFM* ztvfm, gint ID, gchar const* text_new )
{
    FMForeach fm_foreach = { ID, text_new, ztvfm };

    gtk_tree_model_foreach( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ),
            zond_treeviewfm_foreach_pdf_abschnitt, &fm_foreach );

    return;
}


static gint
zond_treeviewfm_file_part_visible( ZondTreeviewFM* ztvfm, gchar const* file_part,
        gboolean open, gboolean* visible, GtkTreeIter* iter, gboolean* children, gboolean* opened, GError** error )
{
    gchar* rel_path = NULL;
    gint rc = 0;

    rel_path = get_rel_path_from_file_part( file_part );
    if ( !rel_path )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s: file_part malformed", __func__ );

        return -1;
    }

    rc = sond_treeviewfm_rel_path_visible( SOND_TREEVIEWFM(ztvfm), rel_path, open, visible,
            iter, error );
    g_free( rel_path );
    if ( rc ) ERROR_Z

    if ( !(*visible) ) return 0;

    //ToDo: file_parts durchnudeln

    return 0;
}


gint
zond_treeviewfm_section_visible( ZondTreeviewFM* ztvfm, gchar const* file_part, gchar const* section,
        gboolean open, gboolean* visible, GtkTreeIter* iter, gboolean* children, gboolean* opened, GError** error )
{
    gint rc = 0;
    gboolean visible_intern = FALSE;
    GtkTreeIter iter_intern = { 0 };

    if ( !open && !visible ) return 0; //ergibt einfach keinen Sinn

    rc = zond_treeviewfm_file_part_visible( ztvfm, file_part, open, &visible_intern,
            &iter_intern, NULL, NULL, error );
    if ( rc ) ERROR_Z

    if ( !visible_intern )
    {
        *visible = FALSE; //

        return 0;
    }

    if ( section )
    {
        Anbindung anbindung_prev = { { 0, 0 }, { 999999, 999999 } };
        Anbindung anbindung = { 0 };

        anbindung_parse_file_section( section, &anbindung );

        do
        {
            if ( anbindung_1_eltern_von_2( anbindung_prev, anbindung ) )
            {
                GObject* object = NULL;
                GtkTreeIter iter_child = { 0 };

                if ( !gtk_tree_model_iter_children( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ),
                        &iter_child, &iter_intern ) )
                {
                    if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKnoten hat keinen Abkömmling", __func__ );

                    return -1;
                }

                gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_child, 0, &object, -1 );

                if ( !object ) //dummy
                {
                    if ( !open ) //wenn's nicht geöffnet werden soll, sind wir hier fertig
                    {
                        *visible = FALSE; //visible muß != NULL sein, sonst schon oben Ende

                        return 0;
                    }
                    else  sond_treeview_expand_row( SOND_TREEVIEW(ztvfm), &iter_intern );
                }
                else //ist object
                {
                    if ( !ZOND_IS_PDF_ABSCHNITT(object) )
                    {
                        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nobject ist kein PDF-Abschnitt", __func__ );
                        g_object_unref( object );

                        return -1;
                    }

                    zond_pdf_abschnitt_get( ZOND_PDF_ABSCHNITT(object), NULL, NULL, &anbindung_prev, NULL, NULL );
                    g_object_unref( object );

                    iter_intern = iter_child;
                }
            }
            else if ( anbindung_1_vor_2( anbindung_prev, anbindung ) )
            {
                GtkTreeIter iter_sibling = { 0 };
                GObject* object = NULL;

                iter_sibling = iter_intern;

                if ( !gtk_tree_model_iter_next( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_sibling ) )
                {
                    if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKein jüngeres Geschwister", __func__ );

                    return -1;
                }

                gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_sibling, 0, &object, -1 );

                if ( !object )
                {
                    if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                            "%sd\nZu jüngerem Geschwister kein object gespeichert", __func__ );

                    return -1;
                }
                else if ( !ZOND_IS_PDF_ABSCHNITT(object) )
                {
                    if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                            "%sd\nobject ist kein ZPA", __func__ );
                    g_object_unref( object );

                    return -1;
                }

                zond_pdf_abschnitt_get( ZOND_PDF_ABSCHNITT(object), NULL, NULL, &anbindung_prev, NULL, NULL );
                g_object_unref( object );

                iter_intern = iter_sibling;
            }
            else if ( anbindung_1_gleich_2( anbindung_prev, anbindung ) ) break;
            else
            {
                if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nsection-tree ist korrupt", __func__ );

                return -1;
            }
        } while ( 1 );
    }

    if ( visible ) *visible = TRUE;

    //children und opened abfragen
    //prüfen, ob Kinder, und wenn nur dummy
    if ( children )
    {
        if ( gtk_tree_model_iter_has_child( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_intern ) )
        {
            GtkTreePath* path = NULL;
            gboolean expanded = FALSE;

            *children = TRUE;

            if ( opened )
            {
                path = gtk_tree_model_get_path( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_intern );
                expanded = gtk_tree_view_row_expanded( GTK_TREE_VIEW(ztvfm), path );
                gtk_tree_path_free( path );

                if ( expanded ) *opened = TRUE;
                else *opened = FALSE;
            }
        }
        else *children = FALSE;
    }

    if ( iter ) *iter = iter_intern;

    return 0;
}


gint
zond_treeviewfm_set_cursor_on_section( ZondTreeviewFM* ztvfm, gchar const* file_part,
        gchar const* section, GError** error )
{
    gint rc = 0;
    GtkTreeIter iter = { 0 };

    rc = zond_treeviewfm_section_visible( ztvfm, file_part, section,
            TRUE, NULL, &iter, NULL, NULL, error );
    if ( rc ) ERROR_Z

    sond_treeview_set_cursor( SOND_TREEVIEW(ztvfm), &iter );

    return 0;
}


#define G_NODE(node) ((GNode *)node)
static void
zond_treeviewfm_walk_tree( GtkTreeModel* model, gint stamp, GNode* node, gint pos )
{
    GNode* child = NULL;
    GtkTreeIter iter = { 0 };
    GtkTreePath* path = NULL;
    gint pos_child = 0;

    iter.stamp = stamp;
    iter.user_data = node;
    path = gtk_tree_model_get_path( model, &iter );

    gtk_tree_model_row_inserted( model, path, &iter );

    if ( node->parent->parent != NULL )
    {
        /* child_toggled */
        if ( node->prev == NULL && node->next == NULL ) //keineGeschwister
        {
            GtkTreeIter new_iter = { 0 };
            gtk_tree_path_up (path);
            new_iter.stamp = stamp;
            new_iter.user_data = node->parent;
            gtk_tree_model_row_has_child_toggled( model, path, &new_iter );
        }
    }
    gtk_tree_path_free (path);

    //Kinder durchgehen
    child = node->children;
    while ( child )
    {
        zond_treeviewfm_walk_tree( model, stamp, child, pos_child );

        child = child->next;
        pos_child++;
    }

    return;
}


static void
zond_treeviewfm_move_node( GtkTreeModel* model, GtkTreeIter* iter_src, GtkTreeIter* anchor,
        gboolean child, GtkTreeIter* iter_new )
{
    GNode* node_src = NULL;
    GNode* node_src_parent = NULL;
    GtkTreePath* path = NULL;
    gint pos = 0;

    node_src = iter_src->user_data;
    node_src_parent = node_src->parent;

    path = gtk_tree_model_get_path( model, iter_src );

    //node ausklinken
    g_node_unlink( node_src );

    //und im treeview bekanntgeben
    gtk_tree_model_row_deleted( model, path );

    if ( node_src_parent->parent != NULL ) //nicht root?
    {
        /* child_toggled */
        if (node_src_parent->children == NULL ) //keineGeschwister
        {
            GtkTreeIter new_iter = {0,};
            gtk_tree_path_up (path);
            new_iter.stamp = iter_src->stamp;
            new_iter.user_data = node_src_parent;
            gtk_tree_model_row_has_child_toggled( model, path, &new_iter );
        }
    }
    gtk_tree_path_free (path);

    //jetzt Knoten wieder einfügen
    if ( child )
    {
        GNode* node_anchor = NULL;

        if ( anchor ) node_anchor = anchor->user_data;
        else //anchor ist root-node
        {
            GtkTreeIter iter_first = { 0 };

            gtk_tree_model_get_iter_first( model, &iter_first );
            node_anchor = ((GNode*) (iter_first.user_data))->parent;
        }

        g_node_insert_after( node_anchor, NULL, node_src );
    }
    else
    {
        g_node_insert_after( G_NODE(anchor->user_data)->parent, G_NODE(anchor->user_data), node_src );
        pos = g_node_child_position( G_NODE(anchor->user_data)->parent, node_src );
    }

    //im treeview bekannt geben
    zond_treeviewfm_walk_tree( model, iter_src->stamp, node_src, pos );

    if ( iter_new )
    {
        iter_new->stamp = iter_src->stamp;
        iter_new->user_data = node_src;
    }

    return;
}


void
zond_treeviewfm_kill_parent( ZondTreeviewFM* ztvfm, GtkTreeIter* iter )
{
    GtkTreeIter child = { 0 };
    GtkTreeIter anchor = { 0 };
    GtkTreeModel* model = NULL;

    if ( !iter ) return;

    model = gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) );

    anchor = *iter;

    while ( gtk_tree_model_iter_children( model, &child, iter ) )
    {
        GtkTreeIter inserted = { 0 };

        zond_treeviewfm_move_node( model, &child, &anchor, FALSE, &inserted );

        anchor = inserted;
    }

    gtk_tree_store_remove( GTK_TREE_STORE(model), iter );

    return;
}


gint
zond_treeviewfm_get_id_pda( ZondTreeviewFM* ztvfm, GtkTreeIter* iter, gint* ID, GError** error )
{
    GObject* object = NULL;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), iter,
        0, &object, -1 );
    if ( !object )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKnoten ist dummy", __func__ );

        return -1;
    }
    else if ( !ZOND_IS_PDF_ABSCHNITT(object) )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKnoten ist dummy", __func__ );
        g_object_unref( object );

        return -1;
    }

    if ( ID ) *ID = zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) );
    g_object_unref( object );

    return 0;
}
