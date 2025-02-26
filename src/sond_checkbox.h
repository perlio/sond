#ifndef SOND_CHECKBOX_H_INCLUDED
#define SOND_CHECKBOX_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_CHECKBOX sond_checkbox_get_type( )
G_DECLARE_DERIVABLE_TYPE( SondCheckbox, sond_checkbox, SOND, CHECKBOX, GtkFrame)

struct _SondCheckboxClass {
	GtkFrameClass parent_class;

	//Signale
	guint signal_alle_toggled;
	guint signal_entry_toggled;
};

void sond_checkbox_add_entry(SondCheckbox*, const gchar*, gint);

GtkWidget* sond_checkbox_new(const gchar *title);

GArray* sond_checkbox_get_active_IDs(SondCheckbox*);

G_END_DECLS

#endif // SOND_CHECKBOX_H_INCLUDED
