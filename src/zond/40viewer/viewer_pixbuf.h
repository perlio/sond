#ifndef VIEWER_PIXBUF_H_INCLUDED
#define VIEWER_PIXBUF_H_INCLUDED

typedef struct _GdkPixbuf ViewerPixbuf;

ViewerPixbuf* viewer_pixbuf_new_from_pixmap( fz_context*, fz_pixmap* );

#endif // VIEWER_PIXBUF_H_INCLUDED
