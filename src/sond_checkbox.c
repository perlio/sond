/*
 sond (sond_checkbox.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_checkbox.h"
#include "sond_checkbox_entry.h"

typedef struct {
	GtkWidget *check_alle;
	GtkWidget *box_entries;
	gulong signal_handler_entry_toggled;
} SondCheckboxPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondCheckbox, sond_checkbox, GTK_TYPE_FRAME)

static void sond_checkbox_class_init(SondCheckboxClass *klass) {
//    GObjectClass *object_class = G_OBJECT_CLASS(klass);

	klass->signal_alle_toggled = g_signal_new("alle-toggled",
	SOND_TYPE_CHECKBOX, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
			G_TYPE_BOOLEAN);

	klass->signal_entry_toggled = g_signal_new("entry-toggled",
	SOND_TYPE_CHECKBOX, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 4,
			GTK_TYPE_CHECK_BUTTON, G_TYPE_BOOLEAN, //ob active oder nicht
			G_TYPE_STRING, //label von CheckButton
			G_TYPE_INT //ID_entity
			);

//    object_class->finalize = sond_checkbox_finalize;

	return;
}

static void sond_checkbox_check_entry_toggled(GtkWidget *check_entry,
		gpointer data) {
	g_signal_emit(data, SOND_CHECKBOX_GET_CLASS(data)->signal_entry_toggled, 0,
			check_entry,
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_entry)),
			gtk_button_get_label(GTK_BUTTON(check_entry)),
			sond_checkbox_entry_get_ID(SOND_CHECKBOX_ENTRY(check_entry)));

	return;
}

static void sond_checkbox_check_alle_toggled(GtkWidget *check_alle,
		gpointer data) {
	gboolean active = FALSE;
	GList *list_entries = NULL;
	GList *list_act = NULL;

	SondCheckbox *scb = SOND_CHECKBOX(data);
	SondCheckboxPrivate *priv = sond_checkbox_get_instance_private(scb);

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_alle));

	list_entries = gtk_container_get_children(GTK_CONTAINER(priv->box_entries));

	list_act = list_entries;
	while (list_act) {
		GtkWidget *check_entry = NULL;

		check_entry = list_act->data;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_entry), active);

		list_act = list_act->next;
	}

	g_signal_emit(scb, SOND_CHECKBOX_GET_CLASS(scb)->signal_alle_toggled, 0,
			active);

	return;
}

static void sond_checkbox_init(SondCheckbox *self) {
	SondCheckboxPrivate *priv = NULL;
	GtkWidget *box = NULL;
	GtkWidget *swindow_entries = NULL;

	priv = sond_checkbox_get_instance_private(self);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(self), box);

	priv->check_alle = gtk_check_button_new_with_label("Alle");
	gtk_box_pack_start(GTK_BOX(box), priv->check_alle, FALSE, FALSE, 1);

	swindow_entries = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_propagate_natural_height(
			GTK_SCROLLED_WINDOW(swindow_entries), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(
			GTK_SCROLLED_WINDOW(swindow_entries), TRUE);
	gtk_box_pack_start(GTK_BOX(box), swindow_entries, FALSE, FALSE, 0);

	priv->box_entries = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(swindow_entries), priv->box_entries);

	g_signal_connect(priv->check_alle, "toggled",
			G_CALLBACK(sond_checkbox_check_alle_toggled), self);

	return;
}

void sond_checkbox_add_entry(SondCheckbox *scb, const gchar *label,
		gint ID_entity) {
	GtkWidget *check_entry = NULL;

	SondCheckboxPrivate *priv = sond_checkbox_get_instance_private(scb);

	check_entry = sond_checkbox_entry_new_full(label, ID_entity);
	gtk_box_pack_start(GTK_BOX(priv->box_entries), check_entry, FALSE, FALSE,
			0);

	g_signal_connect(check_entry, "toggled",
			G_CALLBACK(sond_checkbox_check_entry_toggled), scb);

	return;
}

GtkWidget*
sond_checkbox_new(const gchar *title) {
	SondCheckbox *scb = NULL;

	scb = g_object_new( SOND_TYPE_CHECKBOX, "label", title, NULL);

	return GTK_WIDGET(scb);
}

GArray*
sond_checkbox_get_active_IDs(SondCheckbox *scb) {
	SondCheckboxPrivate *priv = NULL;
	GList *list_entries = NULL;
	GList *ptr = NULL;
	GArray *arr_IDs = NULL;

	priv = sond_checkbox_get_instance_private(scb);

	list_entries = gtk_container_get_children(GTK_CONTAINER(priv->box_entries));

	if (!list_entries)
		return NULL;

	arr_IDs = g_array_new(FALSE, FALSE, sizeof(gint));

	ptr = list_entries;
	do {
		GtkWidget *checkbox_entry = NULL;
		gint ID_entry = 0;

		checkbox_entry = ptr->data;

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox_entry))) {
			ID_entry = sond_checkbox_entry_get_ID(
					SOND_CHECKBOX_ENTRY(checkbox_entry));

			g_array_append_val(arr_IDs, ID_entry);
		}
	} while ((ptr = ptr->next));

	g_list_free(list_entries);

	return arr_IDs;
}

