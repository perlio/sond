/*
 sond (sond_gmessage_helper.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

#ifndef SRC_SOND_GMESSAGE_HELPER_H_
#define SRC_SOND_GMESSAGE_HELPER_H_

typedef struct _GMimeMessage GMimeMessage;
typedef struct _GMimeObject GMimeObject;
typedef struct _GError GError;
typedef unsigned char guchar;
typedef char gchar;
typedef size_t gsize;


GMimeMessage* gmessage_open(const guchar* data, gsize len);

GMimeObject* gmessage_lookup_part_by_path(GMimeMessage* message,
		gchar const* path, GError** error);

gint gmessage_mod_part(GMimeMessage *message, const gchar *path,
		const guchar *data, gsize len, GError **error);

gint gmessage_set_filename(GMimeMessage* message, gchar const* path,
		gchar const* filename, GError** error);

#endif /* SRC_SOND_GMESSAGE_HELPER_H_ */
