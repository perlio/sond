/*
sond (sond_checkbox_entry.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_checkbox_entry.h"



typedef struct
{
    gint ID;
} SondCheckboxEntryPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondCheckboxEntry, sond_checkbox_entry, GTK_TYPE_CHECK_BUTTON)


static void
sond_checkbox_entry_class_init( SondCheckboxEntryClass* klass )
{
//    GObjectClass *object_class = G_OBJECT_CLASS(klass);

//    object_class->finalize = zond_dbase_finalize;

    return;
}


static void
sond_checkbox_entry_init( SondCheckboxEntry* self )
{
//    SondCheckboxEntryPrivate* priv = NULL;

    return;
}


GtkWidget*
sond_checkbox_entry_new_full( const gchar* text, const gint ID )
{
    SondCheckboxEntry* scbe = NULL;
    SondCheckboxEntryPrivate* priv = NULL;

    scbe = g_object_new( SOND_TYPE_CHECKBOX_ENTRY, "label", text, NULL );

    priv = sond_checkbox_entry_get_instance_private( scbe );

    priv->ID = ID;

    return GTK_WIDGET(scbe);
}


gint
sond_checkbox_entry_get_ID( SondCheckboxEntry* scbe )
{
    SondCheckboxEntryPrivate* priv = NULL;

    priv = sond_checkbox_entry_get_instance_private( scbe );

    return priv->ID;
}
