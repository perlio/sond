#include "zond_treeviewfm.h"

#include "../misc.h"
#include "../dbase.h"
#include "../eingang.h"
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
    DBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase_store = (DBase*) priv->zond->dbase_zond->dbase_store;

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_begin( SOND_TREEVIEWFM(stvfm), errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin (work)" )

    rc = dbase_begin( (DBase*) dbase_store, errmsg );
    if ( rc ) ERROR_ROLLBACK ( dbase_work, "dbase_begin (store)" ) //dbase_store

    return 0;
}


static gint
zond_treeviewfm_dbase_test( SondTreeviewFM* stvfm, const gchar* rel_path_source,
        gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );
    DBase* dbase = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase_work = (DBase*) priv->zond->dbase_zond->dbase_work;

    rc = dbase_test_path( dbase, rel_path_source, errmsg );
    if ( rc == -1 ) ERROR_SOND( "dbase_test_path" )
    else if ( rc == 1 ) return 1;

    rc = dbase_test_path( dbase_work, rel_path_source, errmsg );
    if ( rc == -1 ) ERROR_SOND( "dbase_test (work)" )
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

    DBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase = (DBase*)  priv->zond->dbase_zond->dbase_store;

    rc1 = dbase_update_path( dbase, rel_path_source, rel_path_dest, errmsg );
    rc2 = dbase_update_path( dbase_work, rel_path_source, rel_path_dest, errmsg );

    if ( rc1 || rc2 ) ERROR_ROLLBACK_BOTH( "dbase_update_path" )

    return 0;
}


static gint
zond_treeviewfm_dbase_update_eingang( SondTreeviewFM* stvfm,
        const gchar* rel_path_source, const gchar* rel_path_dest, gboolean del,
        gchar** errmsg )
{
    gint rc1 = 0;
    gint rc2 = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    DBase* dbase_work = sond_treeviewfm_get_dbase( stvfm );
    DBase* dbase = (DBase*)  priv->zond->dbase_zond->dbase_store;

    rc1 = eingang_update_rel_path( dbase, rel_path_source, dbase, rel_path_dest, del, errmsg );
    rc2 = eingang_update_rel_path( dbase_work, rel_path_source, dbase_work, rel_path_dest, del, errmsg );

    if ( rc1 || rc2 ) ERROR_ROLLBACK_BOTH( "eingang_update_path" )

    return 0;
}


static gint
zond_treeviewfm_dbase_end( SondTreeviewFM* stvfm, gboolean suc, gchar** errmsg )
{
    gint rc = 0;

    ZondTreeviewFMPrivate* priv = zond_treeviewfm_get_instance_private( ZOND_TREEVIEWFM(stvfm) );

    rc = SOND_TREEVIEWFM_CLASS(zond_treeviewfm_parent_class)->dbase_end( stvfm, suc, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_store,"dbase_end (work)" )

    if ( suc )
    {
        gint rc = 0;

        rc = dbase_commit( (DBase*) priv->zond->dbase_zond->dbase_store, errmsg );
        if ( rc ) ERROR_ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_store, "dbase_commit (store)" )
    }
    else ROLLBACK( (DBase*) priv->zond->dbase_zond->dbase_store )

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
    SOND_TREEVIEWFM_CLASS(klass)->dbase_update_eingang = zond_treeviewfm_dbase_update_eingang;
    SOND_TREEVIEWFM_CLASS(klass)->dbase_end = zond_treeviewfm_dbase_end;
    SOND_TREEVIEWFM_CLASS(klass)->text_edited = zond_treeviewfm_text_edited;

    return;
}


static void
zond_treeviewfm_init( ZondTreeviewFM* ztvfm )
{
    return;
}


ZondTreeviewFM*
zond_treeviewfm_new( Projekt* zond )
{
    ZondTreeviewFM* ztvfm = g_object_new( ZOND_TYPE_TREEVIEWFM, "Projekt", zond, NULL );

    return ztvfm;
}







