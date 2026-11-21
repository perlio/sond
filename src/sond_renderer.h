/*
 * sond_renderer.h
 *
 *  Created on: 14.12.2025
 *      Author: pkrieger
 */

#ifndef SRC_SOND_RENDERER_H_
#define SRC_SOND_RENDERER_H_

#include <glib.h>

typedef struct _SondFilePart SondFilePart;

gint sond_render(GBytes*, SondFilePart*, gchar const*, GError**);
gint sond_render_with_term(GBytes*, SondFilePart*, gchar const* title,
        gchar const* term, gint char_pos_in_doc, GError**);

/* Öffnet sfp im eigenen Viewer, wo es einen gibt, sonst extern (open_with: immer extern) */
gint sond_renderer_open_file_part(SondFilePart*, gboolean open_with, GError**);

#endif /* SRC_SOND_RENDERER_H_ */
