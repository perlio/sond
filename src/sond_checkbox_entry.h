#ifndef SOND_CHECKBOX_ENTRY_H_INCLUDED
#define SOND_CHECKBOX_ENTRY_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_CHECKBOX_ENTRY sond_checkbox_entry_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondCheckboxEntry, sond_checkbox_entry, SOND, CHECKBOX_ENTRY, GtkCheckButton)


struct _SondCheckboxEntryClass
{
    GtkCheckButtonClass parent_class;
};

GtkWidget* sond_checkbox_entry_new_full( const gchar*, const gint );

const gchar* sond_checkbox_entry_get_text( SondCheckboxEntry* );

gint sond_checkbox_entry_get_ID( SondCheckboxEntry* );

G_END_DECLS


#endif // SOND_CHECKBOX_ENTRY_H_INCLUDED
