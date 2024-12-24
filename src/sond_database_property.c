/*
 sond (sond_database_property.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2022  pelo america

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

#include "sond_database_property.h"

#include "sond_database.h"
#include "misc.h"

typedef struct {
	gboolean editable;
	GtkWidget *entry_value;
} SondDatabasePropertyPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondDatabaseProperty, sond_database_property,
		SOND_TYPE_DATABASE_ENTITY)

static void sond_database_property_class_init(SondDatabasePropertyClass *klass) {

	return;
}

static void sond_database_property_init(SondDatabaseProperty *self) {
	GtkWidget *frame_value = NULL;

	SondDatabasePropertyPrivate *priv =
			sond_database_property_get_instance_private(self);

	frame_value = gtk_frame_new("Wert");
	priv->entry_value = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame_value), priv->entry_value);

	gtk_box_pack_start(GTK_BOX(self), frame_value, FALSE, FALSE, 0);
	gtk_box_reorder_child(GTK_BOX(self), frame_value, 1);

	gtk_widget_set_sensitive(priv->entry_value, FALSE);

	return;
}

static SondDatabaseProperty*
sond_database_property_new(void) {
	SondDatabaseProperty *sdp = NULL;

	sdp = g_object_new( SOND_TYPE_DATABASE_PROPERTY, NULL, NULL);
	gtk_orientable_set_orientation(GTK_ORIENTABLE(sdp),
			GTK_ORIENTATION_VERTICAL);

	return sdp;
}

SondDatabaseProperty*
sond_database_property_load_new(gpointer database, gint ID_property,
		gchar **errmsg) {
	SondDatabaseProperty *sdp = NULL;
	SondDatabasePropertyPrivate *priv = NULL;
	gchar *value = NULL;
	gint rc = 0;

	sdp = sond_database_property_new();
	priv = sond_database_property_get_instance_private(sdp);

	rc = sond_database_entity_load(SOND_DATABASE_ENTITY(sdp), database,
			ID_property, errmsg);
	if (rc) {
		g_object_unref(sdp);
		ERROR_S_VAL(NULL)
	}

	rc = sond_database_get_property_value(database, ID_property, &value,
			errmsg);
	if (rc) {
		g_object_unref(sdp);
		ERROR_S_VAL(NULL)
	}

	gtk_entry_set_text(GTK_ENTRY(priv->entry_value), value);
	g_free(value);

	return sdp;
}

void sond_database_property_set_editable(SondDatabaseProperty *sdp,
		gboolean editable) {
	SondDatabasePropertyPrivate *priv =
			sond_database_property_get_instance_private(sdp);

	priv->editable = editable;

	return;
}
