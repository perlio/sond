/*
 sond (sond_gmessage_helper.c) - Akten, Beweisstücke, Unterlagen
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

#include <glib.h>
#include <gmime/gmime.h>

#include "sond_log_and_error.h"

GMimeMessage* gmessage_open(const guchar* data, gsize len) {
	GMimeStream* stream = NULL;
	GMimeParser* parser = NULL;
	GMimeMessage* message = NULL;

	stream = g_mime_stream_mem_new_with_buffer((const gchar*) data, len);
	parser = g_mime_parser_new_with_stream (stream);
	message = g_mime_parser_construct_message (parser, NULL);
	g_object_unref (parser);
	g_object_unref (stream);

	return message;
}

static GMimeObject* lookup_path(GMimeObject* root, gchar const* path, GError** error) {
	GMimeObject* object = NULL;
	gchar** strv = NULL;
	gint zaehler = 0;

	object = g_object_ref(root);

	if (!path)
		return object;

	strv = g_strsplit(path, "/", -1);

	if (GMIME_IS_MULTIPART(object))
		do {
			GMimeObject* part = NULL;

			part = g_object_ref(g_mime_multipart_get_part(
					GMIME_MULTIPART(object), atoi(strv[zaehler])));
			g_object_unref(object);
			if (!part) {
				if (error)
					*error = g_error_new(SOND_ERROR, 0,
							"%s\nMimePart mit Index %s nicht gefunden",
							__func__, strv[zaehler]);
				return NULL;
			}

			object = part;
			zaehler++;
		} while (strv[zaehler] != NULL);
	else if (zaehler == 0 && strv[0] && !strv[1]) { //nur wenn root-object
		if (GMIME_IS_MESSAGE_PART(object)) {
			GMimeMessage* msg = NULL;
			GMimeObject* part = NULL;

			msg = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));
			part = g_object_ref(g_mime_message_get_mime_part(GMIME_MESSAGE(msg)));
			g_object_unref(object);
			object = part;
		}
		else if (!GMIME_IS_PART(object)) {
			g_object_unref(object);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nroot-Object ist weder MimePart noch Message",
					__func__);

			return NULL;
		}
	}
	else {
		g_object_unref(object);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nunzulässiger Pfad",
				__func__);

		return NULL;
	}

	g_strfreev(strv);

	return object;
}

GMimeObject* gmessage_lookup_part_by_path(GMimeMessage* message,
		gchar const* path, GError** error) {
	GMimeObject* object = NULL;
	GMimeObject* root = NULL;

	root = g_mime_message_get_mime_part(message);
	if (!root) {
		if (error)
			*error = g_error_new(SOND_ERROR, 0,
					"%s\nNachricht hat keinen MIME-Teil", __func__);

		return NULL;
	}

	object = lookup_path(root, path, error);

	return object;
}

static gboolean replace_part_content_safe(GMimeObject *part,
		const gchar* data, gsize len, GError **error) {
    if (GMIME_IS_PART(part)) {
        GMimeStream *stream = g_mime_stream_mem_new_with_buffer(
            data, len);

        GMimeContentEncoding encoding = g_mime_part_get_content_encoding(GMIME_PART(part));

        GMimeDataWrapper *content = g_mime_data_wrapper_new_with_stream(stream, encoding);
        g_mime_part_set_content(GMIME_PART(part), content);

        g_object_unref(content);
        g_object_unref(stream);

        return TRUE;
    }
    else if (GMIME_IS_MESSAGE_PART(part)) {
        GMimeStream *stream = g_mime_stream_mem_new_with_buffer(
            (const gchar*) data, len);

        GMimeParser *parser = g_mime_parser_new_with_stream(stream);
        GMimeMessage *message = g_mime_parser_construct_message(parser, NULL);

        if (!message) {
            g_set_error(error, g_quark_from_static_string("gmime"), 2,
                       "Failed to parse message from buffer");
            g_object_unref(parser);
            g_object_unref(stream);
            return FALSE;
        }

        g_mime_message_part_set_message(GMIME_MESSAGE_PART(part), message);

        g_object_unref(message);
        g_object_unref(parser);
        g_object_unref(stream);

        return TRUE;
    }

    g_set_error(error, g_quark_from_static_string("gmime"), 3,
               "Object is neither GMimePart nor GMimeMessagePart");

    return FALSE;
}

/**
 * Die Funktion gmessage_mod_part() ersetzt den Inhalt eines Parts oder löscht ihn,
 * wenn buffer NULL ist.
 */
gint gmessage_mod_part(GMimeMessage *message, const char *path,
		const gchar *data, gsize len, GError **error) {
	GMimeObject *root = NULL;
	GMimeObject *object = NULL;
	GMimeObject *obj_parent = NULL;
	gchar* parent_path = NULL;
	guint index = 0;
	guint count = 0;

	root = g_mime_message_get_mime_part(message);

	if (!data && !GMIME_IS_MULTIPART(root)) {
		g_set_error(error, SOND_ERROR, 0,
				"%s\nLöschen des einzigen body-Part nicht zulässig", __func__);

		return -1;
	}

	if (path) {
		if (strrchr(path, '/')) {
			parent_path = g_path_get_dirname(path);
			index = atoi(strrchr(path, '/'));
		}
		else
			index = atoi(path);
	}

	obj_parent = lookup_path(root, parent_path, error);
    if (!obj_parent)
    	ERROR_Z

    if (GMIME_IS_MULTIPART(obj_parent))
    	count = g_mime_multipart_get_count(GMIME_MULTIPART(obj_parent));
    else count = 1;

    if (index > count - 1) {
    	g_set_error(error, SOND_ERROR, 0,
    			"%s\n index %u out of range (0 - %u)", __func__, index, count - 1);
    	g_object_unref(obj_parent);

    	return -1;
    }

	if (!data && count <= 1) { //letzter/einziger MimePart
		g_set_error(error, g_quark_from_static_string("gmime"), 0,
				"%s\nEinziger mimepart kann nicht gelöscht werden", __func__);
    	g_object_unref(obj_parent);

		return -1;
	}

    if (GMIME_IS_MULTIPART(obj_parent))
    	object = g_mime_multipart_get_part(GMIME_MULTIPART(obj_parent), index);
	else
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(obj_parent));
    if (!object) {
    	g_set_error(error, SOND_ERROR, 0,
    			"%s\nKein GMime-Objekt gefunden", __func__);
        g_object_unref(obj_parent);

    	return -1;
    }

    if (!data) {
    	GMimeObject* obj_removed = NULL;

    	obj_removed = g_mime_multipart_remove_at(GMIME_MULTIPART(obj_parent), index);
    	g_object_unref(obj_parent);
    	if (!obj_removed) {
    		g_set_error(error, SOND_ERROR, 0,
    				"%s\nKonnte MimePart mit index %u nicht entfernen", __func__, index);

    		return -1;
    	}

    	g_object_unref(obj_removed);
    }
    else {
		g_object_unref(obj_parent);
		if (!replace_part_content_safe(object, data, len, error))
			ERROR_Z
    }

    return 0;
}

gint gmessage_set_filename(GMimeMessage* message, gchar const* path,
		gchar const* filename, GError** error) {
	GMimeObject* object = NULL;
	GMimeObject* root = NULL;
	GMimeContentDisposition* disposition = NULL;

	root = g_mime_message_get_mime_part(message);
	if (!root)
		ERROR_Z

	object = lookup_path(root, path, error);
    if (!object)
    	ERROR_Z

    if (GMIME_IS_PART(object)) {
    	g_mime_part_set_filename(GMIME_PART(object), filename);
    }

	// Für jedes GMimeObject: Setze den Filename in der Content-Disposition
	disposition = g_mime_object_get_content_disposition(object);

	if (!disposition) {
		// Erstelle eine neue Content-Disposition, falls keine existiert
		disposition = g_mime_content_disposition_new();
		g_mime_object_set_content_disposition(object, disposition);
		g_object_unref(disposition);
	}

	g_mime_content_disposition_set_parameter(disposition, "filename", filename);

	return 0;
}
