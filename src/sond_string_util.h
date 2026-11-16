#ifndef SRC_SOND_STRING_UTIL_H_
#define SRC_SOND_STRING_UTIL_H_

#include <glib.h>

/*
 * Hängt add_string an old_string an und gibt BEIDE Argumente frei (old_string
 * darf NULL sein, dann ist das Ergebnis eine Kopie von add_string). Generisch
 * (kein GTK, kein Windows), deshalb nicht in misc.h, das an GTK hängt.
 */
static inline gchar* add_string(gchar *old_string, gchar *add_string) {
	gchar *new_string = NULL;

	if (old_string)
		new_string = g_strconcat(old_string, add_string, NULL);
	else
		new_string = g_strdup(add_string);
	g_free(old_string);
	g_free(add_string);

	return new_string;
}

#endif /* SRC_SOND_STRING_UTIL_H_ */
