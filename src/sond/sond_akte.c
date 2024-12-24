#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "sond_akte.h"

/*
 static void
 akte_beteiligter_free( AkteBeteiligter* akte_beteiligter )
 {
 person_kurz_free( akte_beteiligter->person_kurz );
 g_ptr_array_unref( akte_beteiligter->arr_betreffs );

 g_free( akte_beteiligter );

 return;
 }


 static void
 akte_sachbearbeiter_free( AkteSachbearbeiter* akte_sachbearbeiter )
 {
 person_kurz_free( akte_sachbearbeiter->person_kurz );
 g_free( akte_sachbearbeiter->sb_kuerzel );
 g_date_time_unref( akte_sachbearbeiter->von );
 g_date_time_unref( akte_sachbearbeiter->bis );

 g_free( akte_sachbearbeiter );

 return;
 }
 */

void sond_akte_free(SondAkte *sond_akte) {
	if (!sond_akte)
		return;

	g_free(sond_akte->aktenrubrum);
	g_free(sond_akte->aktenkurzbez);

	g_ptr_array_unref(sond_akte->arr_leben);
	/*
	 akte_sachbearbeiter_free( akte->akte_sachbearbeiter );

	 g_ptr_array_unref( akte->arr_beteiligte );
	 */
	g_free(sond_akte);

	return;
}

SondAkte*
sond_akte_new(void) {
	SondAkte *sond_akte = g_malloc0(sizeof(SondAkte));

	sond_akte->arr_leben = g_ptr_array_new();
	/*
	 akte->arr_beteiligte = g_ptr_array_new_with_free_func( (GDestroyNotify) akte_beteiligter_free );

	 */
	return sond_akte;
}

SondAkte*
sond_akte_new_from_json(const gchar *json, GError **error) {
	JsonNode *node = NULL;
	JsonObject *object = NULL;
	SondAkte *sond_akte = NULL;

	node = json_from_string(json, error);
	if (!node) {
		if (error)
			g_prefix_error(error, "%s\n%s\n", __func__, "json_from_string");
		else
			*error = g_error_new(g_quark_from_string("GLIB_JSON"), 0,
					"%s\njson == NULL", __func__);
		return NULL;
	}

	if (!JSON_NODE_HOLDS_OBJECT(node)) {
		*error = g_error_new( SOND_AKTE_ERROR, SOND_AKTE_ERROR_PARSEJSON,
				"%s\n%s", __func__, "Root-Knoten ist kein object");

		return NULL;
	}

	object = json_node_get_object(node);

	sond_akte = sond_akte_new();

	if (json_object_has_member(object, "ID_entity"))
		sond_akte->ID_entity = (gint) json_object_get_int_member(object,
				"ID_entity");

	if (json_object_has_member(object, "reg_nr"))
		sond_akte->reg_nr = (gint) json_object_get_int_member(object, "reg_nr");
	if (json_object_has_member(object, "reg_jahr"))
		sond_akte->reg_jahr = (gint) json_object_get_int_member(object,
				"reg_jahr");

	if (json_object_has_member(object, "aktenrubrum"))
		sond_akte->aktenrubrum = g_strdup(
				json_object_get_string_member(object, "aktenrubrum"));

	if (json_object_has_member(object, "aktenkurzbez"))
		sond_akte->aktenkurzbez = g_strdup(
				json_object_get_string_member(object, "aktenkurzbez"));

	if (json_object_has_member(object, "leben")) {
		JsonArray *array = NULL;

		array = json_object_get_array_member(object, "leben");
		for (gint i = 0; i < json_array_get_length(array); i++)
			g_ptr_array_add(sond_akte->arr_leben,
					GUINT_TO_POINTER(
							(guint) json_array_get_int_element(array, i)));
	}

	json_node_unref(node);

	return sond_akte;
}

JsonObject*
sond_akte_to_json_object(SondAkte *sond_akte) {
	JsonObject *object = NULL;

	object = json_object_new();

	json_object_set_int_member(object, "ID_entity",
			(gint64) sond_akte->ID_entity);

	json_object_set_int_member(object, "reg_jahr",
			(gint64) sond_akte->reg_jahr);
	json_object_set_int_member(object, "reg_nr", (gint64) sond_akte->reg_nr);

	json_object_set_string_member(object, "aktenrubrum",
			sond_akte->aktenrubrum);
	json_object_set_string_member(object, "aktenkurzbez",
			sond_akte->aktenkurzbez);

	if (sond_akte->arr_leben->len > 0) {
		JsonArray *array = NULL;

		array = json_array_new();
		for (gint i = 0; i < sond_akte->arr_leben->len; i++)
			json_array_add_int_element(array,
					(gint64) GPOINTER_TO_UINT(
							g_ptr_array_index(sond_akte->arr_leben, i)));

		json_object_set_array_member(object, "leben", array); //takes ownership
	}

	return object;
}

gchar*
sond_akte_to_json_string(SondAkte *sond_akte) {
	JsonObject *object = NULL;
	JsonNode *node = NULL;
	gchar *json_string = NULL;

	object = sond_akte_to_json_object(sond_akte);

	node = json_node_alloc();
	json_node_init_object(node, object);
	json_string = json_to_string(node, FALSE);
	json_node_unref(node);
	json_object_unref(object);

	return json_string;
}
