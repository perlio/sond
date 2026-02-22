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

#endif /* SRC_SOND_RENDERER_H_ */
