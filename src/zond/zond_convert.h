gint zond_dbase_kopieren_nach_auswertung( ZondDBase*, Baum, gint, gint, gboolean,
        gchar** );

gint zond_dbase_verschieben_knoten( ZondDBase*, gint, gint, gboolean, gchar** );

gint zond_dbase_get_icon_name_and_node_text( ZondDBase*, Baum, gint, gchar**,
        gchar**, gchar** );

gint zond_dbase_get_text( ZondDBase*, gint, gchar**, gchar** );

gint zond_dbase_set_ziel( ZondDBase*, Ziel*, gint, gchar** );

gint zond_dbase_set_datei( ZondDBase*, gint, const gchar*, gchar** );

gint zond_dbase_get_parent( ZondDBase*, gint, gchar** );

gint zond_dbase_get_older_sibling( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_younger_sibling( ZondDBase*, gint, gchar** );

gint zond_dbase_get_first_child( ZondDBase*, gint, gchar** );

gint zond_dbase_get_ref_id( ZondDBase*, gint, gchar** );

gint zond_dbase_get_rel_path( ZondDBase*, Baum, gint, gchar**, gchar** );

gint zond_dbase_get_ziel( ZondDBase*, Baum, gint, Ziel*, gchar** );

gint zond_dbase_get_node_id_from_rel_path( ZondDBase*, const gchar*, gchar** );

gint zond_dbase_check_id( ZondDBase*, const gchar*, gchar** );

gint zond_dbase_set_node_text( ZondDBase*, Baum, gint, const gchar*, gchar** );

gint zond_dbase_set_link( ZondDBase*, const gint, const gint, const gchar*,
        const gint, const gint, gchar** );

gint zond_dbase_check_link( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_link( ZondDBase*, gint, Baum*, gint*, gchar**, Baum*, gint*,
        gchar** );

gint zond_dbase_update_path( ZondDBase*, const gchar*, const gchar*, gchar** );

gint zond_dbase_test_path( ZondDBase*, const gchar*, gchar** );

