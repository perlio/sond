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
  gint root_node_id;
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
zond_tree_store_class_init (ZondTreeStoreClass *klass)
{
  GObjectClass *object_class;
  object_class = (GObjectClass *) klass;
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
  priv->root->data = g_malloc0( sizeof( RowData ) );
  ((RowData*) priv->root->data)->tree_store = tree_store;

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
zond_tree_store_new ( gint root_node_id )
{
    ZondTreeStore* tree_store = NULL;

    tree_store = g_object_new (ZOND_TYPE_TREE_STORE, NULL);

    tree_store->priv->root_node_id = root_node_id;

    return tree_store;
}

void zond_tree_store_remove_node( GNode* node );

static gboolean
node_free (GNode *node, gpointer data)
{
    GList* list_links = NULL;
    RowData* row_data = NULL;

    row_data = node->data;

    if ( row_data->target ) //ist link
    {
        GNode* node_target = NULL;

        //link-Ziel abkoppeln
        node_target = row_data->target;
        ((RowData*) node_target->data)->links =
                g_list_remove( ((RowData*) node_target->data)->links, node );
    }
    else if ( row_data->data )//nur wenn kein link und nicht root
    {
        g_free( row_data->data->icon_name );
        g_free( row_data->data->node_text );

        g_free( row_data->data );
    }

    //selbert link-Ziel
    list_links = row_data->links;
    while ( list_links )
    {
        //link, der auf uns verweist, löschen
        zond_tree_store_remove_node( list_links->data );
        list_links = row_data->links; //weiterspulen
    }

    //g_list_free überflüssig, da alle Elemente gelöscht werden und daherlist_links NULL ist

    g_free( row_data );

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
void
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


void
zond_tree_store_remove( GtkTreeIter* iter )
{
    if ( !iter ) return;

    zond_tree_store_remove_node( iter->user_data );

    return;
}


static void
_do_zond_tree_store_insert( GNode* node_parent,
        gint pos, GtkTreeIter* iter_new )
{
    GNode* node_new = NULL;
    GtkTreePath* path = NULL;
    ZondTreeStore* tree_store = NULL;

    g_return_if_fail( iter_new );

    //node_parent muß zu treestore gehören!
//    g_return_if_fail( node_parent == tree_store->priv->root || node_parent->data ||
//            ((RowData*) node_parent->data)->tree_store == tree_store );

    tree_store = ((RowData*) node_parent->data)->tree_store;

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
zond_tree_store_insert_dummy( GNode* parent_node )
{
    GtkTreeIter iter_dummy = { 0 };

    _do_zond_tree_store_insert( parent_node, 0, &iter_dummy );
    //Data-Struktur erzeugen, damit de-Referenzierung in _get nicht abstürzt
    ((RowData*) G_NODE(iter_dummy.user_data)->data)->data = g_malloc0( sizeof( Data ) );
    //head_nr des dummy auf -1 setzen, damit erkannt werden kann
    ((RowData*) G_NODE(iter_dummy.user_data)->data)->head_nr = -1;

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

        //wenn link-head und noch kein Kind: dummy einfügen
        if ( ((RowData*) parent_link->data)->head_nr && !parent_link->children )
                zond_tree_store_insert_dummy( parent_link );
        //oder link-head, aber schon geladen, oder kein link-head: volles programm
        else if ( (((RowData*) parent_link->data)->head_nr && parent_link->children
                && ((RowData*) parent_link->children->data)->head_nr != -1) ||
                ((RowData*) parent_link->data)->head_nr == 0 )
        {
            //nur einfügen, wenn orig_new nicht schon child ist
            _do_zond_tree_store_insert( parent_link, pos, &iter_link );

            ((RowData*) G_NODE(iter_link.user_data)->data)->data =
                    ((RowData*) orig_new->data)->data;

            ((RowData*) G_NODE(iter_link.user_data)->data)->target = orig_new;
            ((RowData*) orig_new->data)->links =
                    g_list_append( ((RowData*) orig_new->data)->links, iter_link.user_data );

            //wenn neuer Knoten Kinder hat (kommt nur bei links in Betracht):
            if ( orig_new->children ) zond_tree_store_insert_dummy( iter_link.user_data );
        }

        //when parent_link iter-head, dann können ja seinerseits links auf ihn zeigen
        //braucht man aber nicht abzufragen, weil sonst hat parent_link keine Liste mit auf ihn zeigenden Links, ne
        zond_tree_store_insert_linked_nodes( parent_link, pos, orig_new );

        list = list->next;
    }

    return;
}


void
zond_tree_store_insert( ZondTreeStore* tree_store, GtkTreeIter* iter,
        gboolean child, GtkTreeIter* iter_inserted )
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
            pos = 0; //sonst funktioniert neue Anbindung einfügen ggf. nicht!
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

    _do_zond_tree_store_insert( node_parent, pos, &iter_new );
    ((RowData*) G_NODE(iter_new.user_data)->data)->data = g_malloc0( sizeof( Data ) );

    if ( node_parent != tree_store->priv->root )
            zond_tree_store_insert_linked_nodes( node_parent, pos, iter_new.user_data );

    if ( iter_inserted ) *iter_inserted = iter_new;

    return;
}


static void
zond_tree_store_insert_link_at_pos (GNode* node_target,
                                    gint head_nr,
                                    GNode* node_parent,
                                    gint pos,
                                    GtkTreeIter* iter_new,
                                    gboolean insert_link )
{
    GtkTreeIter iter_dest_new = { 0 };
    RowData* row_data = NULL;

    //Hauptknoten erzeugen
    _do_zond_tree_store_insert( node_parent, pos, &iter_dest_new );
    if ( iter_new ) *iter_new = iter_dest_new;

    //Daten
    row_data = G_NODE(iter_dest_new.user_data)->data;

    row_data->data = ((RowData*) node_target->data)->data;
    row_data->target = node_target;
    row_data->head_nr = head_nr;

    ((RowData*) node_target->data)->links =
            g_list_append( ((RowData*) node_target->data)->links,
            iter_dest_new.user_data );

    //target hat Kinder? Dann Dummy-child einfügen:
    if ( node_target->children ) zond_tree_store_insert_dummy( iter_dest_new.user_data );

    //Falls Knoten, in den link eingefügt wird, verlinkt ist:
    if ( insert_link && node_parent != ((RowData*) node_parent->data)->tree_store->priv->root )
            zond_tree_store_insert_linked_nodes( node_parent, pos, iter_dest_new.user_data );

    return;
}


void
zond_tree_store_insert_link (GtkTreeIter* iter_target,
                             gint head_nr,
                             ZondTreeStore* tree_store,
                               GtkTreeIter* iter_anchor,
                               gboolean child,
                               GtkTreeIter* iter_new )
{
    gint pos = 0;
    GNode* node_parent = NULL;

    if ( iter_anchor && !child )
    {
        pos = g_node_child_position( G_NODE(iter_anchor->user_data)->parent, iter_anchor->user_data ) + 1;

        if ( G_NODE(iter_anchor->user_data)->parent != tree_store->priv->root )
                node_parent = G_NODE(iter_anchor->user_data)->parent;
        else node_parent = tree_store->priv->root;
    }
    else if ( iter_anchor && child ) node_parent = iter_anchor->user_data;
    else node_parent = tree_store->priv->root; //if ! iter_anchor

    zond_tree_store_insert_link_at_pos( iter_target->user_data, head_nr, node_parent, pos, iter_new, TRUE );

    return;
}




static void
zond_tree_store_load_node( GNode* node_parent, GNode* node_parent_target )
{
    gint pos = 0;
    GNode* node_target_child = NULL;
/*
    //Falls node_parent_target auf head_link zeigt
    if ( ((RowData*) node_parent_target->data)->head_nr > 0 )
            node_parent_target = ((RowData*) node_parent_target->data)->target;
*/
    node_target_child = node_parent_target->children;

    while ( node_target_child )
    {
        //Kind ist selbert link-head:
        if ( ((RowData*) node_target_child->data)->head_nr )
                zond_tree_store_insert_link_at_pos( node_target_child, 0,
                node_parent, pos, NULL, FALSE );
        //oder kind zeigt auf head
        else if ( ((RowData*) node_target_child->data)->target &&
                ((RowData*) ((RowData*) node_target_child->data)->target->data)->head_nr > 0 )
                zond_tree_store_insert_link_at_pos( ((RowData*) node_target_child->data)->target, 0,
                node_parent, pos, NULL, FALSE );
        else //Kind ist kein link
        {
            GtkTreeIter iter_new = { 0 };
            RowData* row_data = NULL;

            //Hauptknoten erzeugen
            _do_zond_tree_store_insert( node_parent, pos, &iter_new );

            //Daten
            row_data = G_NODE(iter_new.user_data)->data;

            row_data->data = ((RowData*) node_target_child->data)->data;
            row_data->target = node_target_child;

            //in target link-Liste ergänzen
            ((RowData*) node_target_child->data)->links =
                    g_list_append( ((RowData*) node_target_child->data)->links,
                    iter_new.user_data );

            //Falls Knoten, in den link eingefügt wird, verlinkt ist:
            if ( node_parent != ((RowData*) node_parent->data)->tree_store->priv->root )
                    zond_tree_store_insert_linked_nodes( node_parent, pos, iter_new.user_data );

            //Kinder durchgehen
            if ( node_target_child->children )
                    zond_tree_store_load_node( iter_new.user_data,
                    node_target_child );
        }

        node_target_child = node_target_child->next;
        pos++;
    }

    return;
}


void
zond_tree_store_load_link( GtkTreeIter* iter_head )
{
    GNode* node_target = NULL;
    GtkTreeIter iter_dummy = { 0 };

    //iter_dummy ist kind von iter_head - umbiegen
    iter_dummy = *iter_head;
    iter_dummy.user_data = G_NODE(iter_head->user_data)->children;

    //Ziel ist ziel des link-heads
    node_target = ((RowData*) G_NODE(iter_head->user_data)->data)->target;
    //Falls zu öffnender link auf Head-link zeigt, und nicht unmittelbar auf Knoten:
//    if ( ((RowData*) node_target->data)->head_nr ) node_target = ((RowData*) node_target->data)->target;

    zond_tree_store_load_node( iter_head->user_data, node_target );

    //iter_dummy entfernen
    zond_tree_store_remove( &iter_dummy );

    return;
}



static void
copy_node_data ( GtkTreeIter  *src_iter, ZondTreeStore* tree_store_dest,
        GtkTreeIter *dest_iter)
{
  GtkTreePath *path = NULL;
  RowData* data = NULL;

  data = G_NODE(dest_iter->user_data)->data;
  data->tree_store = tree_store_dest;
  data->data->icon_name = g_strdup( ((RowData*) G_NODE(src_iter->user_data)->data)->data->icon_name );
  data->data->node_id = ((RowData*) G_NODE(src_iter->user_data)->data)->data->node_id;
  data->data->node_text = g_strdup( ((RowData*) G_NODE(src_iter->user_data)->data)->data->node_text );

  path = zond_tree_store_get_path (GTK_TREE_MODEL (tree_store_dest), dest_iter);
  gtk_tree_model_row_changed (GTK_TREE_MODEL (tree_store_dest), path, dest_iter);
  gtk_tree_path_free (path);

  return;
}


void
zond_tree_store_copy_node( GtkTreeIter* iter_src, ZondTreeStore* tree_store_dest,
        GtkTreeIter* iter_dest, gboolean kind, GtkTreeIter* iter_new )
{
    GtkTreeIter child = { 0 };
    GtkTreeModel *model = NULL;

    model = GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter_src ));

    zond_tree_store_insert( tree_store_dest, iter_dest, kind, iter_new );
    copy_node_data ( iter_src, tree_store_dest, iter_new);
    if (zond_tree_store_iter_children (model,
                                    &child,
                                    iter_src))
    {
      // Need to create children and recurse. Note our
      // dependence on persistent iterators here.
      kind = TRUE;

        do
        {
          GtkTreeIter copy = { 0 };

          copy = *iter_new;
          zond_tree_store_copy_node( &child, tree_store_dest, &copy, kind, iter_new );
          kind = FALSE;
        }
        while (zond_tree_store_iter_next (model, &child));
    }

    return;
}



static void
zond_tree_store_walk_tree( GNode* node, gint pos )
{
    GNode* child = NULL;
    GtkTreeIter iter = { 0 };
    GtkTreePath* path = NULL;
    GtkTreeModel* model = NULL;
    gint pos_child = 0;

    model = GTK_TREE_MODEL(((RowData*) node->data)->tree_store);
    iter.stamp = ((RowData*) node->data)->tree_store->priv->stamp;
    iter.user_data = node;
    path = gtk_tree_model_get_path( model, &iter );

    gtk_tree_model_row_inserted( model, path, &iter );

    if ( node->parent != ((RowData*) node->data)->tree_store->priv->root )
    {
        /* child_toggled */
        if ( node->prev == NULL && node->next == NULL ) //keineGeschwister
        {
            GtkTreeIter new_iter = {0,};
            gtk_tree_path_up (path);
            new_iter.stamp = ((RowData*) node->data)->tree_store->priv->stamp;
            new_iter.user_data = node->parent;
            gtk_tree_model_row_has_child_toggled( model, path, &new_iter );
        }
    }
    gtk_tree_path_free (path);

    //wenn Knoten in link eingefügt wirf...
    zond_tree_store_insert_linked_nodes( node->parent, pos, node );

    //Kinder durchgehen
    child = node->children;
    while ( child )
    {
        zond_tree_store_walk_tree( child, pos_child );

        child = child->next;
        pos_child++;
    }

    return;
}


void
zond_tree_store_move_node( GtkTreeIter* iter_src, GtkTreeIter* iter_anchor,
        gboolean child, GtkTreeIter* iter_new )
{
    GNode* node_src = NULL;
    GNode* node_src_parent = NULL;
    GtkTreePath* path = NULL;
    GtkTreeModel* model_src = NULL;
    GList* list = NULL;
    gint pos = 0;

    node_src = iter_src->user_data;
    node_src_parent = node_src->parent;
    model_src = GTK_TREE_MODEL( ((RowData*) node_src->data)->tree_store);

    path = zond_tree_store_get_path( model_src, iter_src );

    //node ausklinken
    g_node_unlink( node_src );

    //und im treeview bekanntgeben
    gtk_tree_model_row_deleted( model_src, path);

    if( node_src_parent != G_NODE(ZOND_TREE_STORE(model_src)->priv->root) )
    {
        /* child_toggled */
        if (node_src_parent->children == NULL ) //keineGeschwister
        {
            GtkTreeIter new_iter = {0,};
            gtk_tree_path_up (path);
            new_iter.stamp = ZOND_TREE_STORE(model_src)->priv->stamp;
            new_iter.user_data = node_src_parent;
            gtk_tree_model_row_has_child_toggled( model_src, path, &new_iter );
        }
    }
    gtk_tree_path_free (path);

    ((RowData*) node_src->data)->tree_store = ((RowData*) G_NODE(iter_anchor->user_data)->data)->tree_store;

    //jetzt Knoten, die auf ausgelösten Knoten zeigen, löschen
    list = ((RowData*) node_src->data)->links;
    while ( list )
    {
        GNode* link = NULL;
        GtkTreeIter iter_link = { 0 };

        link = list->data;

        if ( ((RowData*) link->data)->head_nr <= 0 )
        {
            iter_link.stamp = ((RowData*) node_src->data)->tree_store->priv->stamp;
            iter_link.user_data = link;

            zond_tree_store_remove( &iter_link );
        }
        list = list->next;
    }

    //jetzt Knoten wieder einfügen
    if ( child ) g_node_insert_after( G_NODE(iter_anchor->user_data), NULL, node_src );
    else
    {
        g_node_insert_after( G_NODE(iter_anchor->user_data)->parent, G_NODE(iter_anchor->user_data), node_src );
        pos = g_node_child_position( G_NODE(iter_anchor->user_data)->parent, node_src );
    }

    //im treeview bekannt geben
    zond_tree_store_walk_tree( node_src, pos );

    if ( iter_new )
    {
        iter_new->stamp = iter_anchor->stamp;
        iter_new->user_data = node_src;
    }

    return;
}


gint
zond_tree_store_get_link_head_nr( GtkTreeIter* iter )
{
    gint head_nr = 0;

    if ( !iter ) return 0;

    head_nr = ((RowData*) (G_NODE(iter->user_data)->data))->head_nr;

    //head_nr == -1 bei dummy
    return (head_nr > 0) ? head_nr : 0;
}


/* simple ripoff from g_node_traverse_post_order */
static void
zond_tree_store_clear_traverse (GNode        *node,
                               ZondTreeStore *store)
{
    GtkTreeIter iter = { 0 };

    iter.stamp = store->priv->stamp;

    while ( node->children )
    {
        iter.user_data = node->children;
        zond_tree_store_remove( &iter );
    }

    return;
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
      zond_tree_store_remove (&iter);
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


//Gibt iter zurück, falls iter kein link
void
zond_tree_store_get_iter_target( GtkTreeIter* iter, GtkTreeIter* iter_target)
{
    GNode* node = NULL;
    GNode* node_orig = NULL;

    g_return_if_fail( iter );
    g_return_if_fail( iter_target );

    node = iter->user_data;

    while ( (node_orig = ((RowData*) node->data)->target) ) node = node_orig;

    iter_target->stamp = ((RowData*) node->data)->tree_store->priv->stamp;
    iter_target->user_data = node;

    return;
}


ZondTreeStore*
zond_tree_store_get_tree_store( GtkTreeIter* iter )
{
    if ( !iter ) return NULL;

    return ((RowData*) G_NODE(iter->user_data)->data)->tree_store;
}


gint
zond_tree_store_get_root_id( ZondTreeStore* tree_store )
{
    return tree_store->priv->root_node_id;
}


GNode*
zond_tree_store_get_root_node( GtkTreeIter* iter )
{
    return ((RowData*) G_NODE(iter->user_data)->data)->tree_store->priv->root;
}

GList*
zond_tree_store_get_linked_nodes( GtkTreeIter* iter )
{
    return ((RowData*) G_NODE(iter->user_data)->data)->links;
}


gboolean
zond_tree_store_link_is_unloaded( GtkTreeIter* iter )
{
    GNode* node_children = NULL;

    g_return_val_if_fail( iter, FALSE );

    if ( !(node_children = G_NODE(iter->user_data)->children) ) return FALSE;

    if ( ((RowData*) node_children->data)->head_nr != -1 ) return FALSE;

    return TRUE;
}


gint
zond_tree_store_get_node_id( GtkTreeIter* iter )
{
    gint node_id = 0;

    if ( ((RowData*) G_NODE(iter->user_data)->data)->target )
    {
        node_id = ((RowData*) G_NODE(iter->user_data)->data)->head_nr;
        if ( node_id <= 0 ) return 0;
    }
    else node_id = ((RowData*) G_NODE(iter->user_data)->data)->data->node_id;

    return node_id;
}
