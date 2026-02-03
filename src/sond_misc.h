/*
 sond (sond_mime.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#ifndef SRC_SOND_MISC_H_
#define SRC_SOND_MISC_H_

typedef char gchar;
typedef size_t gsize;
typedef struct _GError GError;
typedef unsigned char guchar;

gchar* add_string(gchar *old_string, gchar *add_string);

const gchar* mime_to_extension(const gchar*);

const gchar* mime_to_extension_ci(const gchar*);

const gchar* mime_to_extension_with_params(const gchar*);

gchar* mime_guess_content_type(guchar* buffer, gsize size, GError** error);

#endif /* SRC_SOND_MISC_H_ */
