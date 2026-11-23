#ifndef VIEWER_DRAW_H_INCLUDED
#define VIEWER_DRAW_H_INCLUDED

#include <gtk/gtk.h>

//Handler von "draw" des Bildes einer Seite (data: ViewerPageNew*)
gboolean viewer_draw_image_page(GtkWidget *image, cairo_t *cr, gpointer data);

#endif /* VIEWER_DRAW_H_INCLUDED */
