/*
zond (zond_tree_store.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <gtk/gtk.h>
#include <string.h>

#include "zond_tree_store.h"


/**
 * SECTION:gtktreestore
 * @Short_description: A tree-like data structure that can be used with the GtkTreeView
 * @Title: GtkTreeStore
 * @See_also: #GtkTreeModel
 *
 * The #GtkTreeStore object is a list model for use with a #GtkTreeView
 * widget.  It implements the #GtkTreeModel interface, and consequentially,
 * can use all of the methods available there.  It also implements the
 * #GtkTreeSortable interface so it can be sorted by the view.  Finally,
 * it also implements the tree
 * [drag and drop][gtk3-GtkTreeView-drag-and-drop]
 * interfaces.
 *
 * # GtkTreeStore as GtkBuildable
 *
 * The GtkTreeStore implementation of the #GtkBuildable interface allows
 * to specify the model columns with a <columns> element that may contain
 * multiple <column> elements, each specifying one model column. The “type”
 * attribute specifies the data type for the column.
 *
 * An example of a UI Definition fragment for a tree store:
 * |[
 * <object class="GtkTreeStore">
 *   <columns>
 *     <column type="gchararray"/>
 *     <column type="gchararray"/>
 *     <column type="gint"/>
 *   </columns>
 * </object>
 * ]|
 */
struct _ZondTreeStorePrivate
{
  gint stamp;
  GNode* root;
};

typedef struct _Data
{
    gchar* icon_name;
    gchar* node_text;
    gint node_id;
} Data;

typedef struct _Row_Data
{
    ZondTreeStore* tree_store;
    gint head_nr;
    GNode* target;
    GList* links; //(list of nodes)
    Data* data;
} RowData;


#define G_NODE(node) ((GNode *)node)
#define VALID_ITER(iter, tree_store) ((iter)!= NULL && (iter)->user_data != NULL && ((ZondTreeStore*)(tree_store))->priv->stamp == (iter)->stamp)

static void         zond_tree_store_tree_model_init (GtkTreeModelIface *iface);
static void         zond_tree_store_drag_source_init(GtkTreeDragSourceIface *iface);
static void         zond_tree_store_drag_dest_init  (GtkTreeDragDestIface   *iface);
static void         zond_tree_store_finalize        (GObject           *object);
static GtkTreeModelFlags zond_tree_store_get_flags  (GtkTreeModel      *tree_model);
static gint         zond_tree_store_get_n_columns   (GtkTreeModel      *tree_model);
static GType        zond_tree_store_get_column_type (GtkTreeModel      *tree_model,
                                                    gint               index);
static gboolean     zond_tree_store_get_iter        (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter,
                                                    GtkTreePath       *path);
static GtkTreePath *zond_tree_store_get_path        (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter);
static void         zond_tree_store_get_value       (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter,
                                                    gint               column,
                                                    GValue            *value);
static gboolean     zond_tree_store_iter_next       (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter);
static gboolean     zond_tree_store_iter_previous   (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter);
static gboolean     zond_tree_store_iter_children   (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter,
                                                    GtkTreeIter       *parent);
static gboolean     zond_tree_store_iter_has_child  (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter);
static gint         zond_tree_store_iter_n_children (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter);
static gboolean     zond_tree_store_iter_nth_child  (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter,
                                                    GtkTreeIter       *parent,
                                                    gint               n);
static gboolean     zond_tree_store_iter_parent     (GtkTreeModel      *tree_model,
                                                    GtkTreeIter       *iter,
                                                    GtkTreeIter       *child);
static void zond_tree_store_increment_stamp (ZondTreeStore  *tree_store);
/* DND interfaces */
static gboolean real_zond_tree_store_row_draggable   (GtkTreeDragSource *drag_source,
                                                   GtkTreePath       *path);
static gboolean zond_tree_store_drag_data_delete   (GtkTreeDragSource *drag_source,
                                                   GtkTreePath       *path);
static gboolean zond_tree_store_drag_data_get      (GtkTreeDragSource *drag_source,
                                                   GtkTreePath       *path,
                                                   GtkSelectionData  *selection_data);
static gboolean zond_tree_store_drag_data_received (GtkTreeDragDest   *drag_dest,
                                                   GtkTreePath       *dest,
                                                   GtkSelectionData  *selection_data);
static gboolean zond_tree_store_row_drop_possible  (GtkTreeDragDest   *drag_dest,
                                                   GtkTreePath       *dest_path,
                                                   GtkSelectionData  *selection_data);

#ifdef G_ENABLE_DEBUG
static void
validate_gnode (GNode* node)
{
  GNode *iter;
  iter = node->children;
  while (iter != NULL)
    {
      g_assert (iter->parent == node);
      if (iter->prev)
        g_assert (iter->prev->next == iter);
      validate_gnode (iter);
      iter = iter->next;
    }
}


static inline void
validate_tree (ZondTreeStore *tree_store)
{
//  if (GTK_DEBUG_CHECK (TREE))
//    {
      g_assert (G_NODE (tree_store->priv->root)->parent == NULL);
      validate_gnode (G_NODE (tree_store->priv->root));
//    }
}
#else
#define validate_tree(store)
#endif
G_DEFINE_TYPE_WITH_CODE (ZondTreeStore, zond_tree_store, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (ZondTreeStore)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                zond_tree_store_tree_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_SOURCE,
                                                zond_tree_store_drag_source_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_DRAG_DEST,
                                                zond_tree_store_drag_dest_init))

static void
zond_tree_store_class_init (ZondTreeStoreClass *class)
{
  GObjectClass *object_class;
  object_class = (GObjectClass *) class;
  object_class->finalize = zond_tree_store_finalize;
}
static void
zond_tree_store_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = zond_tree_store_get_flags;
  iface->get_n_columns = zond_tree_store_get_n_columns;
  iface->get_column_type = zond_tree_store_get_column_type;
  iface->get_iter = zond_tree_store_get_iter;
  iface->get_path = zond_tree_store_get_path;
  iface->get_value = zond_tree_store_get_value;
  iface->iter_next = zond_tree_store_iter_next;
  iface->iter_previous = zond_tree_store_iter_previous;
  iface->iter_children = zond_tree_store_iter_children;
  iface->iter_has_child = zond_tree_store_iter_has_child;
  iface->iter_n_children = zond_tree_store_iter_n_children;
  iface->iter_nth_child = zond_tree_store_iter_nth_child;
  iface->iter_parent = zond_tree_store_iter_parent;
}
static void
zond_tree_store_drag_source_init (GtkTreeDragSourceIface *iface)
{
  iface->row_draggable = real_zond_tree_store_row_draggable;
  iface->drag_data_delete = zond_tree_store_drag_data_delete;
  iface->drag_data_get = zond_tree_store_drag_data_get;
}
static void
zond_tree_store_drag_dest_init (GtkTreeDragDestIface *iface)
{
  iface->drag_data_received = zond_tree_store_drag_data_received;
  iface->row_drop_possible = zond_tree_store_row_drop_possible;
}

static void
zond_tree_store_init (ZondTreeStore *tree_store)
{
  ZondTreeStorePrivate *priv;
  priv = zond_tree_store_get_instance_private (tree_store);
  tree_store->priv = priv;
  priv->root = g_node_new (NULL);
  /* While the odds are against us getting 0...  */
  do
    {
      priv->stamp = g_random_int ();
    }
  while (priv->stamp == 0);
}

/**
 * gtk_tree_store_new:
 * @n_columns: number of columns in the tree store
 * @...: all #GType types for the columns, from first to last
 *
 * Creates a new tree store as with @n_columns columns each of the types passed
 * in.  Note that only types derived from standard GObject fundamental types
 * are supported.
 *
 * As an example, `gtk_tree_store_new (3, G_TYPE_INT, G_TYPE_STRING,
 * GDK_TYPE_PIXBUF);` will create a new #GtkTreeStore with three columns, of type
 * #gint, #gchararray, and #GdkPixbuf respectively.
 *
 * Returns: a new #GtkTreeStore
 **/
ZondTreeStore *
zond_tree_store_new ( void )
{
    return (ZondTreeStore*) g_object_new (ZOND_TYPE_TREE_STORE, NULL);
}


static gboolean
node_free (GNode *node, gpointer data)
{
    if (node->data)
    {
        RowData* row_data = (RowData*) node->data;
        if ( row_data->data && !(row_data->target) ) //nur wenn kein link
        {
            g_free( row_data->data->icon_name );
            g_free( row_data->data->node_text );

            g_free( row_data->data );
            row_data->data = NULL;
        }

        g_list_free( row_data->links );

      g_free( row_data );
      node->data = NULL;
  }

  return FALSE;
}

static void
zond_tree_store_finalize (GObject *object)
{
  ZondTreeStore *tree_store = ZOND_TREE_STORE (object);
  ZondTreeStorePrivate *priv = tree_store->priv;
  g_node_traverse (priv->root, G_POST_ORDER, G_TRAVERSE_ALL, -1,
                   node_free, NULL);
  g_node_destroy (priv->root);

  /* must chain up */
  G_OBJECT_CLASS (zond_tree_store_parent_class)->finalize (object);
}
/* fulfill the GtkTreeModel requirements */
/* NOTE: GtkTreeStore::root is a GNode, that acts as the parent node.  However,
 * it is not visible to the tree or to the user., and the path “0” refers to the
 * first child of GtkTreeStore::root.
 */
static GtkTreeModelFlags
zond_tree_store_get_flags (GtkTreeModel *tree_model)
{
  return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint
zond_tree_store_get_n_columns (GtkTreeModel *tree_model)
{
  return 3;
}

static GType
zond_tree_store_get_column_type (GtkTreeModel *tree_model,
                                gint          index)
{
    if ( index == 0 ) return G_TYPE_STRING;
    else if ( index == 1 ) return G_TYPE_STRING;
    else if ( index == 2 ) return G_TYPE_INT;

    return G_TYPE_INVALID;
}

static gboolean
zond_tree_store_get_iter (GtkTreeModel *tree_model,
                         GtkTreeIter  *iter,
                         GtkTreePath  *path)
{
  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;
  ZondTreeStorePrivate *priv = tree_store->priv;
  GtkTreeIter parent;
  gint *indices;
  gint depth, i;
  indices = gtk_tree_path_get_indices (path);
  depth = gtk_tree_path_get_depth (path);
  g_return_val_if_fail (depth > 0, FALSE);
  parent.stamp = priv->stamp;
  parent.user_data = priv->root;
  if (!zond_tree_store_iter_nth_child (tree_model, iter, &parent, indices[0]))
    {
      iter->stamp = 0;
      return FALSE;
    }
  for (i = 1; i < depth; i++)
    {
      parent = *iter;
      if (!zond_tree_store_iter_nth_child (tree_model, iter, &parent, indices[i]))
        {
          iter->stamp = 0;
          return FALSE;
        }
    }
  return TRUE;
}
static GtkTreePath *
zond_tree_store_get_path (GtkTreeModel *tree_model,
                         GtkTreeIter  *iter)
{
  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;
  ZondTreeStorePrivate *priv = tree_store->priv;
  GtkTreePath *retval;
  GNode *tmp_node;
  gint i = 0;
  g_return_val_if_fail (iter->user_data != NULL, NULL);
  g_return_val_if_fail (iter->stamp == priv->stamp, NULL);
  validate_tree (tree_store);
  if (G_NODE (iter->user_data)->parent == NULL &&
      G_NODE (iter->user_data) == priv->root)
    return gtk_tree_path_new ();
  g_assert (G_NODE (iter->user_data)->parent != NULL);
  if (G_NODE (iter->user_data)->parent == G_NODE (priv->root))
    {
      retval = gtk_tree_path_new ();
      tmp_node = G_NODE (priv->root)->children;
    }
  else
    {
      GtkTreeIter tmp_iter = *iter;
      tmp_iter.user_data = G_NODE (iter->user_data)->parent;
      retval = zond_tree_store_get_path (tree_model, &tmp_iter);
      tmp_node = G_NODE (iter->user_data)->parent->children;
    }
  if (retval == NULL)
    return NULL;
  if (tmp_node == NULL)
    {
      gtk_tree_path_free (retval);
      return NULL;
    }
  for (; tmp_node; tmp_node = tmp_node->next)
    {
      if (tmp_node == G_NODE (iter->user_data))
        break;
      i++;
    }
  if (tmp_node == NULL)
    {
      /* We couldn't find node, meaning it's prolly not ours */
      /* Perhaps I should do a g_return_if_fail here. */
      gtk_tree_path_free (retval);
      return NULL;
    }
  gtk_tree_path_append_index (retval, i);
  return retval;
}

static void
zond_tree_store_get_value (GtkTreeModel *tree_model,
                          GtkTreeIter  *iter,
                          gint          column,
                          GValue       *value)
{
    RowData* row_data = NULL;

  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;

  g_return_if_fail (VALID_ITER (iter, tree_store));
  row_data = (RowData*) G_NODE (iter->user_data)->data;

  if ( column == 0 )
  {
      g_value_init(value, G_TYPE_STRING );
      g_value_set_string( value, g_strdup( row_data->data->icon_name ) );
  }
  else if ( column == 1 )
  {
      g_value_init(value, G_TYPE_STRING );
      g_value_set_string( value, g_strdup( row_data->data->node_text ) );
  }
  else if ( column == 2 )
  {
      g_value_init( value, G_TYPE_INT );
      g_value_set_int( value, row_data->data->node_id );
  }

  return;
}

static gboolean
zond_tree_store_iter_next (GtkTreeModel  *tree_model,
                          GtkTreeIter   *iter)
{
  g_return_val_if_fail (iter->user_data != NULL, FALSE);
  g_return_val_if_fail (iter->stamp == ZOND_TREE_STORE (tree_model)->priv->stamp, FALSE);
  if (G_NODE (iter->user_data)->next == NULL)
    {
      iter->stamp = 0;
      return FALSE;
    }
  iter->user_data = G_NODE (iter->user_data)->next;
  return TRUE;
}

static gboolean
zond_tree_store_iter_previous (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter)
{
  g_return_val_if_fail (iter->user_data != NULL, FALSE);
  g_return_val_if_fail (iter->stamp == ZOND_TREE_STORE (tree_model)->priv->stamp, FALSE);
  if (G_NODE (iter->user_data)->prev == NULL)
    {
      iter->stamp = 0;
      return FALSE;
    }
  iter->user_data = G_NODE (iter->user_data)->prev;
  return TRUE;
}

static gboolean
zond_tree_store_iter_children (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreeIter  *parent)
{
  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;
  ZondTreeStorePrivate *priv = tree_store->priv;
  GNode *children;
  if (parent)
    g_return_val_if_fail (VALID_ITER (parent, tree_store), FALSE);
  if (parent)
    children = G_NODE (parent->user_data)->children;
  else
    children = G_NODE (priv->root)->children;
  if (children)
    {
      iter->stamp = priv->stamp;
      iter->user_data = children;
      return TRUE;
    }
  else
    {
      iter->stamp = 0;
      return FALSE;
    }
}

static gboolean
zond_tree_store_iter_has_child (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter)
{
  g_return_val_if_fail (iter->user_data != NULL, FALSE);
  g_return_val_if_fail (VALID_ITER (iter, tree_model), FALSE);
  return G_NODE (iter->user_data)->children != NULL;
}

static gint
zond_tree_store_iter_n_children (GtkTreeModel *tree_model,
                                GtkTreeIter  *iter)
{
  GNode *node;
  gint i = 0;
  g_return_val_if_fail (iter == NULL || iter->user_data != NULL, 0);
  if (iter == NULL)
    node = G_NODE (ZOND_TREE_STORE (tree_model)->priv->root)->children;
  else
    node = G_NODE (iter->user_data)->children;
  while (node)
    {
      i++;
      node = node->next;
    }
  return i;
}

static gboolean
zond_tree_store_iter_nth_child (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter,
                               GtkTreeIter  *parent,
                               gint          n)
{
  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;
  ZondTreeStorePrivate *priv = tree_store->priv;
  GNode *parent_node;
  GNode *child;
  g_return_val_if_fail (parent == NULL || parent->user_data != NULL, FALSE);
  if (parent == NULL)
    parent_node = priv->root;
  else
    parent_node = parent->user_data;
  child = g_node_nth_child (parent_node, n);
  if (child)
    {
      iter->user_data = child;
      iter->stamp = priv->stamp;
      return TRUE;
    }
  else
    {
      iter->stamp = 0;
      return FALSE;
    }
}

static gboolean
zond_tree_store_iter_parent (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter,
                            GtkTreeIter  *child)
{
  ZondTreeStore *tree_store = (ZondTreeStore *) tree_model;
  ZondTreeStorePrivate *priv = tree_store->priv;
  GNode *parent;
  g_return_val_if_fail (iter != NULL, FALSE);
  g_return_val_if_fail (VALID_ITER (child, tree_store), FALSE);
  parent = G_NODE (child->user_data)->parent;
  g_assert (parent != NULL);
  if (parent != priv->root)
    {
      iter->user_data = parent;
      iter->stamp = priv->stamp;
      return TRUE;
    }
  else
    {
      iter->stamp = 0;
      return FALSE;
    }
}

/**
 * gtk_tree_store_set:
 * @tree_store: A #GtkTreeStore
 * @iter: A valid #GtkTreeIter for the row being modified
 * @...: pairs of column number and value, terminated with -1
 *
 * Sets the value of one or more cells in the row referenced by @iter.
 * The variable argument list should contain integer column numbers,
 * each column number followed by the value to be set.
 * The list is terminated by a -1. For example, to set column 0 with type
 * %G_TYPE_STRING to “Foo”, you would write
 * `gtk_tree_store_set (store, iter, 0, "Foo", -1)`.
 *
 * The value will be referenced by the store if it is a %G_TYPE_OBJECT, and it
 * will be copied if it is a %G_TYPE_STRING or %G_TYPE_BOXED.
 **/
static void
zond_tree_store_set_changed( ZondTreeStore* tree_store, GNode* node )
{
    GtkTreeIter iter = { 0 };
    GtkTreePath* path = NULL;

    iter.stamp = tree_store->priv->stamp;
    iter.user_data = node;

    path = gtk_tree_model_get_path( GTK_TREE_MODEL(tree_store), &iter );
    gtk_tree_model_row_changed( GTK_TREE_MODEL(tree_store), path, &iter );
    gtk_tree_path_free( path );

    return;
}


static void
zond_tree_store_linked_nodes_set_changed( GNode* node )
{
    GList* list = NULL;

    list = ((RowData*) node->data)->links;

    while ( list )
    {
        GNode* node_link = NULL;

        node_link = list->data;
        zond_tree_store_set_changed( ((RowData*) node_link->data)->tree_store,
                node_link );
        zond_tree_store_linked_nodes_set_changed( node_link );

        list = list->next;
    }

    return;
}


void
zond_tree_store_set (GtkTreeIter  *iter,
                    const gchar* icon_name,
                    const gchar* node_text,
                    const gint node_id )
{
    GNode* node = NULL;
    GNode* node_orig = NULL;

    node = iter->user_data;

    RowData* row_data = (RowData*) node->data;

    if ( icon_name )
    {
        g_free( row_data->data->icon_name );
        row_data->data->icon_name = g_strdup( icon_name );
    }
    if ( node_text )
    {
        g_free( row_data->data->node_text );
        row_data->data->node_text = g_strdup( node_text );
    }

    if ( node_id ) row_data->data->node_id = node_id;

    //ursprünglichen orig_link ermitteln
    while ( (node_orig = ((RowData*) node->data)->target) ) node = node_orig;

    //geändert setzten - könnte anderer tree_store sein!
    zond_tree_store_set_changed( ((RowData*) node->data)->tree_store, node );

    //Dann die linked_nodes rekursiv durchgehen
    zond_tree_store_linked_nodes_set_changed( node );

    return;
}

/**
 * gtk_tree_store_remove:
 * @tree_store: A #GtkTreeStore
 * @iter: A valid #GtkTreeIter
 *
 * Removes @iter from @tree_store.  After being removed, @iter is set to the
 * next valid row at that level, or invalidated if it previously pointed to the
 * last one.
 *
 * Returns: %TRUE if @iter is still valid, %FALSE if not.
 **/
static void
zond_tree_store_remove_node( GNode* node )
{
    GtkTreePath *path;
    GNode *parent = NULL;
    GtkTreeIter iter = { 0, };
    ZondTreeStore* tree_store = NULL;

    parent = node->parent;
    g_assert (parent != NULL);

    //iter basteln, für path abfragen
    tree_store = ((RowData*) node->data)->tree_store;
    iter.stamp = tree_store->priv->stamp;
    iter.user_data = node;

    g_node_traverse (node, G_POST_ORDER, G_TRAVERSE_ALL,
                     -1, node_free, NULL);

    path = zond_tree_store_get_path( GTK_TREE_MODEL(tree_store), &iter);
    g_node_destroy (node);
    gtk_tree_model_row_deleted( GTK_TREE_MODEL(tree_store), path);

    if( parent != G_NODE(tree_store->priv->root) )
    {
        /* child_toggled */
        if (parent->children == NULL)
        {
            GtkTreeIter new_iter = {0,};
            gtk_tree_path_up (path);
            new_iter.stamp = tree_store->priv->stamp;
            new_iter.user_data = parent;
            gtk_tree_model_row_has_child_toggled( GTK_TREE_MODEL(tree_store), path, &new_iter );
        }
    }
    gtk_tree_path_free (path);

    return;
}


static void
zond_tree_store_remove_links( GNode* node )
{
    GList* list = NULL;

    if ( (list = ((RowData*) node->data)->links) )
    {
        GList* ptr = NULL;

        ptr = list;
        do zond_tree_store_remove_links( ptr->data );
        while ( (ptr = ptr->next) );
    }

    zond_tree_store_remove_node( node );

    return;
}


void
zond_tree_store_remove( ZondTreeStore* tree_store, GtkTreeIter* iter )
{
    GNode* node = NULL;
    GNode* node_orig = NULL;

    g_return_if_fail( ZOND_IS_TREE_STORE(tree_store));
    g_return_if_fail( VALID_ITER(iter, tree_store) );

    //zu Grunde liegenden node ermitteln
    node = iter->user_data;
    while ( (node_orig = ((RowData*) node->data)->target) ) node = node_orig;

    //remove linked nodes
    zond_tree_store_remove_links( node );

    return;
}

void
zond_tree_store_remove_link( ZondTreeStore* tree_store, GtkTreeIter* link )
{
    g_return_if_fail( ZOND_IS_TREE_STORE(tree_store));
    g_return_if_fail( VALID_ITER(link, tree_store) );
    g_return_if_fail( zond_tree_store_is_link( link ) );

    zond_tree_store_remove_links( link->user_data );

    return;
}



static void
_do_zond_tree_store_insert( ZondTreeStore* tree_store, GNode* node_parent,
        gint pos, GtkTreeIter* iter_new )
{
    GNode* node_new = NULL;
    GtkTreePath* path = NULL;

    g_return_if_fail( iter_new );

    //node_parent muß zu treestore gehören!
//    g_return_if_fail( node_parent == tree_store->priv->root || node_parent->data ||
//            ((RowData*) node_parent->data)->tree_store == tree_store );

    node_new = g_node_new (NULL);
    node_new->data = g_malloc0( sizeof( RowData ) );
    ((RowData*) node_new->data)->tree_store = tree_store;

    g_node_insert( node_parent, pos, node_new );

    iter_new->stamp = tree_store->priv->stamp;
    iter_new->user_data = node_new;

    path = zond_tree_store_get_path (GTK_TREE_MODEL (tree_store), iter_new);
    gtk_tree_model_row_inserted (GTK_TREE_MODEL (tree_store), path, iter_new);

    if (node_parent != tree_store->priv->root)
    {
        if (node_new->prev == NULL && node_new->next == NULL)
        {
            GtkTreeIter iter_parent = { 0, };

            iter_parent.stamp = tree_store->priv->stamp;
            iter_parent.user_data = node_parent;

            gtk_tree_path_up (path);
            gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (tree_store), path, &iter_parent );
        }
    }
    gtk_tree_path_free (path);

    return;
}


static void
zond_tree_store_insert_linked_nodes( GNode* node_parent, gint pos, GNode* orig_new )
{
    GList* list = NULL;

    list = ((RowData*) node_parent->data)->links;

    while ( list )
    {
        GNode* parent_link = NULL;
        GtkTreeIter iter_link = { 0, };

        parent_link = list->data;
        _do_zond_tree_store_insert( ((RowData*) parent_link->data)->tree_store,
                parent_link, pos, &iter_link );

        ((RowData*) G_NODE(iter_link.user_data)->data)->data =
                ((RowData*) orig_new->data)->data;

        ((RowData*) G_NODE(iter_link.user_data)->data)->target = orig_new;
        ((RowData*) orig_new->data)->links =
                g_list_append( ((RowData*) orig_new->data)->links, iter_link.user_data );

        if ( ((RowData*) parent_link->data)->links )
                zond_tree_store_insert_linked_nodes( parent_link, pos, iter_link.user_data );

        list = list->next;
    }

    return;
}


void
zond_tree_store_insert( ZondTreeStore* tree_store, GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_inserted )
{
    GNode* node_parent = NULL;
    gint pos = 0;
    GtkTreeIter iter_new = { 0, };

    g_return_if_fail (ZOND_IS_TREE_STORE (tree_store));
    g_return_if_fail( iter == NULL || ((RowData*) G_NODE(iter->user_data)->data)->tree_store == tree_store );

    if ( iter )
    {
        GNode* node = NULL;

        node = iter->user_data;

        //orig herausfinden
        if ( child )
        {
            GNode* node_orig = NULL;

            while ( (node_orig = ((RowData*) node->data)->target) )
                    node = node_orig;

            node_parent = node;
            pos = -1;
        }
        else
        {
            pos = g_node_child_position( node->parent, node ) + 1;
            node = node->parent;

            if ( node != tree_store->priv->root )
            {
                GNode* node_orig = NULL;

                while ( (node_orig = ((RowData*) node->data)->target) )
                        node = node_orig;
            }

            node_parent = node;
        }
    }
    else
    {
        node_parent = tree_store->priv->root;
        pos = -1;
    }

    _do_zond_tree_store_insert( tree_store, node_parent, pos, &iter_new );
    ((RowData*) G_NODE(iter_new.user_data)->data)->data = g_malloc0( sizeof( Data ) );

    if ( node_parent != tree_store->priv->root )
            zond_tree_store_insert_linked_nodes( node_parent, pos, iter_new.user_data );

    if ( iter_inserted ) *iter_inserted = iter_new;

    return;
}


void
zond_tree_store_insert_link (GNode* node_link,
                             gint node_id_link,
                             ZondTreeStore* tree_store,
                       GtkTreeIter* iter_dest,
                       gboolean child,
                       GtkTreeIter* iter_new )
{
    GtkTreeIter iter_dest_new = { 0 };
    RowData* row_data = NULL;
    GNode* node_parent = NULL;
    gint pos = 0;

    if ( iter_dest )
    {
        if ( child )
        {
            node_parent = iter_dest->user_data;
            pos = -1;
        }
        else
        {
            pos = g_node_child_position( G_NODE(iter_dest->user_data)->parent, iter_dest->user_data ) + 1;

            node_parent = G_NODE(iter_dest->user_data)->parent;
        }
    }
    else
    {
        pos = -1;
        node_parent = tree_store->priv->root;
    }

    //Hauptknoten erzeugen
    _do_zond_tree_store_insert( tree_store, node_parent, pos, &iter_dest_new );
    if ( iter_new ) *iter_new = iter_dest_new;

    //Daten
    row_data = G_NODE(iter_dest_new.user_data)->data;

    row_data->data = ((RowData*) node_link->data)->data;
    row_data->target = node_link;
    row_data->head_nr = node_id_link;

    ((RowData*) node_link->data)->links =
            g_list_append( ((RowData*) node_link->data)->links,
            iter_dest_new.user_data );

    //iter link hat Kinder?
    if ( (node_link = node_link->children) )
    {
        child = TRUE;
        GtkTreeIter iter_child_new = { 0, };

        //dann: Kinder durchgehen
        do
        {
            zond_tree_store_insert_link( node_link, 0, tree_store, &iter_dest_new, child, &iter_child_new );

            child = FALSE;
            iter_dest_new = iter_child_new;
        }
        while ( (node_link = node_link->next) );
    }

    return;
}


gint
zond_tree_store_get_link_head_nr( GNode* link )
{
    return ((RowData*) link->data)->head_nr;
}


/**
 * gtk_tree_store_is_ancestor:
 * @tree_store: A #GtkTreeStore
 * @iter: A valid #GtkTreeIter
 * @descendant: A valid #GtkTreeIter
 *
 * Returns %TRUE if @iter is an ancestor of @descendant.  That is, @iter is the
 * parent (or grandparent or great-grandparent) of @descendant.
 *
 * Returns: %TRUE, if @iter is an ancestor of @descendant
 **/
gboolean
zond_tree_store_is_ancestor (ZondTreeStore *tree_store,
                            GtkTreeIter  *iter,
                            GtkTreeIter  *descendant)
{
  g_return_val_if_fail (ZOND_IS_TREE_STORE (tree_store), FALSE);
  g_return_val_if_fail (VALID_ITER (iter, tree_store), FALSE);
  g_return_val_if_fail (VALID_ITER (descendant, tree_store), FALSE);
  return g_node_is_ancestor (G_NODE (iter->user_data),
                             G_NODE (descendant->user_data));
}

/**
 * gtk_tree_store_iter_depth:
 * @tree_store: A #GtkTreeStore
 * @iter: A valid #GtkTreeIter
 *
 * Returns the depth of @iter.  This will be 0 for anything on the root level, 1
 * for anything down a level, etc.
 *
 * Returns: The depth of @iter
 **/
gint
zond_tree_store_iter_depth (ZondTreeStore *tree_store,
                           GtkTreeIter  *iter)
{
  g_return_val_if_fail (GTK_IS_TREE_STORE (tree_store), 0);
  g_return_val_if_fail (VALID_ITER (iter, tree_store), 0);
  return g_node_depth (G_NODE (iter->user_data)) - 2;
}

/* simple ripoff from g_node_traverse_post_order */
static gboolean
zond_tree_store_clear_traverse (GNode        *node,
                               ZondTreeStore *store)
{
  GtkTreeIter iter;
  if (node->children)
    {
      GNode *child;
      child = node->children;
      while (child)
        {
          register GNode *current;
          current = child;
          child = current->next;
          if (zond_tree_store_clear_traverse (current, store))
            return TRUE;
        }
      if (node->parent)
        {
          iter.stamp = store->priv->stamp;
          iter.user_data = node;
          zond_tree_store_remove (store, &iter);
        }
    }
  else if (node->parent)
    {
      iter.stamp = store->priv->stamp;
      iter.user_data = node;
      zond_tree_store_remove (store, &iter);
    }
  return FALSE;
}
static void
zond_tree_store_increment_stamp (ZondTreeStore *tree_store)
{
  ZondTreeStorePrivate *priv = tree_store->priv;
  do
    {
      priv->stamp++;
    }
  while (priv->stamp == 0);
}

/**
 * gtk_tree_store_clear:
 * @tree_store: a #GtkTreeStore
 *
 * Removes all rows from @tree_store
 **/
void
zond_tree_store_clear (ZondTreeStore *tree_store)
{
  g_return_if_fail (ZOND_IS_TREE_STORE (tree_store));
  zond_tree_store_clear_traverse (tree_store->priv->root, tree_store);
  zond_tree_store_increment_stamp (tree_store);
}
static gboolean
zond_tree_store_iter_is_valid_helper (GtkTreeIter *iter,
                                     GNode       *first)
{
  GNode *node;
  node = first;
  do
    {
      if (node == iter->user_data)
        return TRUE;
      if (node->children)
        if (zond_tree_store_iter_is_valid_helper (iter, node->children))
          return TRUE;
      node = node->next;
    }
  while (node);
  return FALSE;
}
/**
 * gtk_tree_store_iter_is_valid:
 * @tree_store: A #GtkTreeStore.
 * @iter: A #GtkTreeIter.
 *
 * WARNING: This function is slow. Only use it for debugging and/or testing
 * purposes.
 *
 * Checks if the given iter is a valid iter for this #GtkTreeStore.
 *
 * Returns: %TRUE if the iter is valid, %FALSE if the iter is invalid.
 *
 * Since: 2.2
 **/
gboolean
zond_tree_store_iter_is_valid (ZondTreeStore *tree_store,
                              GtkTreeIter  *iter)
{
  g_return_val_if_fail (ZOND_IS_TREE_STORE (tree_store), FALSE);
  g_return_val_if_fail (iter != NULL, FALSE);
  if (!VALID_ITER (iter, tree_store))
    return FALSE;
  return zond_tree_store_iter_is_valid_helper (iter, tree_store->priv->root);
}
/* DND */
static gboolean real_zond_tree_store_row_draggable (GtkTreeDragSource *drag_source,
                                                   GtkTreePath       *path)
{
  return TRUE;
}

static gboolean
zond_tree_store_drag_data_delete (GtkTreeDragSource *drag_source,
                                 GtkTreePath       *path)
{
  GtkTreeIter iter;
  if (zond_tree_store_get_iter (GTK_TREE_MODEL (drag_source),
                               &iter,
                               path))
    {
      zond_tree_store_remove (ZOND_TREE_STORE (drag_source),
                             &iter);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}
static gboolean
zond_tree_store_drag_data_get (GtkTreeDragSource *drag_source,
                              GtkTreePath       *path,
                              GtkSelectionData  *selection_data)
{/*
  // Note that we don't need to handle the GTK_TREE_MODEL_ROW
   * target, because the default handler does it for us, but
   * we do anyway for the convenience of someone maybe overriding the
   * default handler.

  if (zond_tree_set_row_drag_data (selection_data,
                                  GTK_TREE_MODEL (drag_source),
                                  path))
    {
      return TRUE;
    }
  else
    {
      // FIXME handle text targets at least.
    }
    */
  return FALSE;
}
static void
copy_node_data (ZondTreeStore *tree_store,
                GtkTreeIter  *src_iter,
                GtkTreeIter  *dest_iter)
{/*
  GtkTreeDataList *dl = G_NODE (src_iter->user_data)->data;
  GtkTreeDataList *copy_head = NULL;
  GtkTreeDataList *copy_prev = NULL;
  GtkTreeDataList *copy_iter = NULL;
  GtkTreePath *path;
  gint col;
  col = 0;
  while (dl)
    {
      copy_iter = _zond_tree_data_list_node_copy (dl, tree_store->priv->column_headers[col]);
      if (copy_head == NULL)
        copy_head = copy_iter;
      if (copy_prev)
        copy_prev->next = copy_iter;
      copy_prev = copy_iter;
      dl = dl->next;
      ++col;
    }
  G_NODE (dest_iter->user_data)->data = copy_head;
  path = zond_tree_store_get_path (GTK_TREE_MODEL (tree_store), dest_iter);
  gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store), path, dest_iter);
  gtk_tree_path_free (path);
  */
}
static void
recursive_node_copy (ZondTreeStore *tree_store,
                     GtkTreeIter  *src_iter,
                     GtkTreeIter  *dest_iter)
{ /*
  GtkTreeIter child;
  GtkTreeModel *model;
  model = GTK_TREE_MODEL (tree_store);
  copy_node_data (tree_store, src_iter, dest_iter);
  if (zond_tree_store_iter_children (model,
                                    &child,
                                    src_iter))
    {
      // Need to create children and recurse. Note our
       * dependence on persistent iterators here.

      do
        {
          GtkTreeIter copy;
          // Gee, a really slow algorithm... ;-) FIXME
          zond_tree_store_append (tree_store,
                                 &copy,
                                 dest_iter);
          recursive_node_copy (tree_store, &child, &copy);
        }
      while (zond_tree_store_iter_next (model, &child));
    }
    */
}

static gboolean
zond_tree_store_drag_data_received (GtkTreeDragDest   *drag_dest,
                                   GtkTreePath       *dest,
                                   GtkSelectionData  *selection_data)
{ /*
  GtkTreeModel *tree_model;
  ZondTreeStore *tree_store;
  GtkTreeModel *src_model = NULL;
  GtkTreePath *src_path = NULL;
  gboolean retval = FALSE;
  tree_model = GTK_TREE_MODEL (drag_dest);
  tree_store = ZOND_TREE_STORE (drag_dest);
  validate_tree (tree_store);
  if (gtk_tree_get_row_drag_data (selection_data,
                                  &src_model,
                                  &src_path) &&
      src_model == tree_model)
    {
      // Copy the given row to a new position
      GtkTreeIter src_iter;
      GtkTreeIter dest_iter;
      GtkTreePath *prev;
      if (!zond_tree_store_get_iter (src_model,
                                    &src_iter,
                                    src_path))
        {
          goto out;
        }
      // Get the path to insert _after_ (dest is the path to insert _before_)
      prev = gtk_tree_path_copy (dest);
      if (!gtk_tree_path_prev (prev))
        {
          GtkTreeIter dest_parent;
          GtkTreePath *parent;
          GtkTreeIter *dest_parent_p;
          // dest was the first spot at the current depth; which means
           * we are supposed to prepend.

          // Get the parent, NULL if parent is the root
          dest_parent_p = NULL;
          parent = gtk_tree_path_copy (dest);
          if (gtk_tree_path_up (parent) &&
              gtk_tree_path_get_depth (parent) > 0)
            {
              zond_tree_store_get_iter (tree_model,
                                       &dest_parent,
                                       parent);
              dest_parent_p = &dest_parent;
            }
          gtk_tree_path_free (parent);
          parent = NULL;
          zond_tree_store_prepend (tree_store,
                                  &dest_iter,
                                  dest_parent_p);
          retval = TRUE;
        }
      else
        {
          if (zond_tree_store_get_iter (tree_model, &dest_iter, prev))
            {
              GtkTreeIter tmp_iter = dest_iter;
              zond_tree_store_insert_after (tree_store, &dest_iter, NULL,
                                           &tmp_iter);
              retval = TRUE;
            }
        }
      gtk_tree_path_free (prev);
      // If we succeeded in creating dest_iter, walk src_iter tree branch,
       * duplicating it below dest_iter.

      if (retval)
        {
          recursive_node_copy (tree_store,
                               &src_iter,
                               &dest_iter);
        }
    }
  else
    {
      // FIXME maybe add some data targets eventually, or handle text
       * targets in the simple case.

    }
 out:
  if (src_path)
    gtk_tree_path_free (src_path);
  return retval;
  */
  return FALSE;
}

static gboolean
zond_tree_store_row_drop_possible (GtkTreeDragDest  *drag_dest,
                                  GtkTreePath      *dest_path,
                                  GtkSelectionData *selection_data)
{/*
  GtkTreeModel *src_model = NULL;
  GtkTreePath *src_path = NULL;
  GtkTreePath *tmp = NULL;
  gboolean retval = FALSE;

  // don't accept drops if the tree has been sorted
  if (!zond_tree_get_row_drag_data (selection_data,
                                   &src_model,
                                   &src_path))
    goto out;

  // can only drag to ourselves
  if (src_model != GTK_TREE_MODEL (drag_dest))
    goto out;
  // Can't drop into ourself.
  if (gtk_tree_path_is_ancestor (src_path,
                                 dest_path))
    goto out;
  // Can't drop if dest_path's parent doesn't exist
  {
    GtkTreeIter iter;
    if (gtk_tree_path_get_depth (dest_path) > 1)
      {
        tmp = gtk_tree_path_copy (dest_path);
        gtk_tree_path_up (tmp);

        if (!gtk_tree_store_get_iter (GTK_TREE_MODEL (drag_dest),
                                      &iter, tmp))
          goto out;
      }
  }

  // Can otherwise drop anywhere.
  retval = TRUE;
 out:
  if (src_path)
    gtk_tree_path_free (src_path);
  if (tmp)
    gtk_tree_path_free (tmp);
  return retval; */
  return FALSE;
}

gboolean
zond_tree_store_is_link( GtkTreeIter* iter )
{
    if ( ((RowData*) G_NODE(iter->user_data)->data)->target ) return TRUE;

    return FALSE;
}


GList*
zond_tree_store_get_linked_nodes( GtkTreeIter* iter )
{
    GNode* node = NULL;

    node = iter->user_data;

    return ((RowData*) node->data)->links;
}


void
zond_tree_store_get_orig( GtkTreeIter* iter, GtkTreeIter* iter_orig )
{
    GNode* node = NULL;
    GNode* node_orig = NULL;

    g_return_if_fail( iter );
    g_return_if_fail( iter_orig );

    node = iter->user_data;

    while ( (node_orig = ((RowData*) node->data)->target) ) node = node_orig;

    iter_orig->stamp = ((RowData*) node->data)->tree_store->priv->stamp;
    iter_orig->user_data = node;

    return;
}


ZondTreeStore*
zond_tree_store_get_tree_store( GNode* node )
{
    g_return_val_if_fail( node, NULL );

    return ((RowData*) node->data)->tree_store;
}