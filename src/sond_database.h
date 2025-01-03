#ifndef SOND_DATABASE_H_INCLUDED
#define SOND_DATABASE_H_INCLUDED

#include <glib.h>

#define ERROR_ZOND_DBASE(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf ", __func__, ":\n", \
                       "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)), NULL ), *errmsg ); \
                       return -1; }

#define SOND_DATABASE_ERROR sond_database_error_quark()
G_DEFINE_QUARK(sond-database-error-quark,sond_database_error)

enum SondDatabaseError
{
	SOND_DATABASE_ERROR_NORESULT,
	SOND_DATABASE_ERROR_TOOMANYRESULTS,
	SOND_DATABASE_ERROR_ROLLBACKFAILED,
	NUM_SOND_DATABASE_ERROR
};

typedef enum {
	AKTE = 1,
	_HAT_ = 1000,
	_BEGINN_ = 10000,
	_ENDE_,
	_NAME_,
	_VORNAME_,
	_REG_JAHR_,
	_REG_NR_,
	_AKTENRUBRUM_,
	_AKTENKURZBEZ_,
	_ABLAGENR_,
	NUM_TYPES
} Type;

//Folgende Strukturen beschreiben den Datensatz, nicht unbedingt den "Knoten" oder die "Kante"
typedef struct _Entity {
	gint ID;
	gint type;
} Entity;

typedef struct _Property {
	Entity entity;
	gint ID_subject;
	gchar *value;
} Property;

typedef struct _Rel {
	Entity entity;
	gint ID_subject;
	gint ID_object;
} Rel;

void sond_database_clear_property(Property *property) {
	g_free(property->value);

	return;
}

typedef struct _Segment {
	gint direction;
	union {
		Entity entity_subject;
		Entity entity_object;
	};
	Entity entity_rel;
} Segment;

typedef struct _Node {
	Entity entity_subject;
	GArray *arr_properties;

} Node;

gint sond_database_add_to_database(gpointer, GError**);

gint sond_database_begin(gpointer, GError**);

gint sond_database_commit(gpointer, GError**);

gint sond_database_rollback(gpointer, GError**);

gint sond_database_insert_entity(gpointer, Type, GError**);

gint sond_database_insert_rel(gpointer, Type, gint, gint, GError**);

gint sond_database_insert_property(gpointer, Type, gint, const gchar*, GError**);

GArray* sond_database_get_properties(gpointer, gint, GError**);

GArray* sond_database_get_properties_of_type(gpointer, gint, gint, GError**);

gint sond_database_get_only_property_of_type(gpointer, gint, gint, Property*,
		GError**);

gint sond_database_update_label(gpointer, gint, gint, gchar**);

gint sond_database_label_is_equal_or_parent(gpointer, gint, gint, gchar**);

gint sond_database_get_ID_label_for_entity(gpointer, gint, gchar**);

gint sond_database_get_entities_for_label(gpointer, gint, GArray**, gchar**);

gint sond_database_get_object_for_subject(gpointer, gint, GArray**, gchar**,
		...);

gint sond_database_get_entities_for_property(gpointer, gint, const gchar*,
		GArray**, gchar**);

gint sond_database_get_entities_for_properties_and(gpointer, GArray**, gchar**,
		...);

gint sond_database_get_first_property_value_for_subject(gpointer, gint, gint,
		gchar**, gchar**);

gint sond_database_get_subject_and_first_property_value_for_labels(gpointer,
		gint, gint, GArray**, GPtrArray**, gchar**);

gint sond_database_get_objects_from_labels(gpointer, gint, gint, gint, GArray**,
		gchar**);

gint sond_database_get_outgoing_rels(gpointer, gint, GArray**, gchar**);

gint sond_database_get_incoming_rels(gpointer, gint, GArray**, gchar**);

gint sond_database_get_subject_from_rel(gpointer, gint, gchar**);

gint sond_database_get_object_from_rel(gpointer, gint, gchar**);

#endif //SOND_DATABASE_H_INCLUDED
