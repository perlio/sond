#include "sond_string_util.h"

#include <glib.h>

char* add_string(char *old_string, char *add_string) {
	char *new_string = NULL;

	if (old_string)
		new_string = g_strconcat(old_string, add_string, NULL);
	else
		new_string = g_strdup(add_string);
	g_free(old_string);
	g_free(add_string);

	return new_string;
}
