/*
 sond (sond_mime.c) - Akten, Beweisstücke, Unterlagen
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

#include <stddef.h>
#include <glib.h>
#include <magic.h>
#include <gmime/gmime.h>


gchar* add_string(gchar *old_string, gchar *add_string) {
	gchar *new_string = NULL;

	if (old_string)
		new_string = g_strconcat(old_string, add_string, NULL);
	else
		new_string = g_strdup(add_string);
	g_free(old_string);
	g_free(add_string);

	return new_string;
}

typedef struct {
    const char* mime_type;
    const char* extension;
} MimeExtMapping;

// Umfassende MIME-Type zu Extension Mapping-Tabelle
static const MimeExtMapping mime_mappings[] = {
    // Text-Formate
    {"text/html", ".html"},
    {"text/plain", ".txt"},
    {"text/css", ".css"},
    {"text/javascript", ".js"},
    {"text/xml", ".xml"},
    {"text/csv", ".csv"},
    {"text/markdown", ".md"},
    {"text/rtf", ".rtf"},
    {"text/calendar", ".ics"},

    // Application - Dokumente
    {"application/pdf", ".pdf"},
    {"application/msword", ".doc"},
    {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", ".docx"},
    {"application/vnd.ms-excel", ".xls"},
    {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", ".xlsx"},
    {"application/vnd.ms-powerpoint", ".ppt"},
    {"application/vnd.openxmlformats-officedocument.presentationml.presentation", ".pptx"},
    {"application/vnd.oasis.opendocument.text", ".odt"},
    {"application/vnd.oasis.opendocument.spreadsheet", ".ods"},
    {"application/vnd.oasis.opendocument.presentation", ".odp"},
    {"application/rtf", ".rtf"},

    // Application - Archive
    {"application/zip", ".zip"},
    {"application/x-7z-compressed", ".7z"},
    {"application/x-rar-compressed", ".rar"},
    {"application/x-tar", ".tar"},
    {"application/gzip", ".gz"},
    {"application/x-bzip2", ".bz2"},
    {"application/x-xz", ".xz"},

    // Application - Executables
    {"application/x-msdownload", ".exe"},
    {"application/x-msdos-program", ".exe"},
    {"application/x-ms-dos-executable", ".exe"},
    {"application/vnd.microsoft.portable-executable", ".exe"},
    {"application/x-dosexec", ".exe"},
    {"application/x-msi", ".msi"},
    {"application/x-bat", ".bat"},
    {"application/x-sh", ".sh"},

    // Application - Data
    {"application/json", ".json"},
    {"application/xml", ".xml"},
    {"application/sql", ".sql"},
    {"application/x-sqlite3", ".db"},

    // Application - Fonts
    {"application/font-woff", ".woff"},
    {"application/font-woff2", ".woff2"},
    {"application/vnd.ms-fontobject", ".eot"},
    {"font/ttf", ".ttf"},
    {"font/otf", ".otf"},
    {"font/woff", ".woff"},
    {"font/woff2", ".woff2"},

    // Images - Common
    {"image/jpeg", ".jpg"},
    {"image/png", ".png"},
    {"image/gif", ".gif"},
    {"image/bmp", ".bmp"},
    {"image/webp", ".webp"},
    {"image/svg+xml", ".svg"},
    {"image/tiff", ".tiff"},
    {"image/x-icon", ".ico"},
    {"image/vnd.microsoft.icon", ".ico"},

    // Images - Raw/Professional
    {"image/x-canon-cr2", ".cr2"},
    {"image/x-canon-crw", ".crw"},
    {"image/x-nikon-nef", ".nef"},
    {"image/x-sony-arw", ".arw"},
    {"image/x-adobe-dng", ".dng"},
    {"image/heic", ".heic"},
    {"image/heif", ".heif"},
    {"image/avif", ".avif"},

    // Images - Adobe
    {"image/vnd.adobe.photoshop", ".psd"},
    {"application/postscript", ".eps"},
    {"image/x-xcf", ".xcf"},

    // Audio
    {"audio/mpeg", ".mp3"},
    {"audio/mp4", ".m4a"},
    {"audio/wav", ".wav"},
    {"audio/x-wav", ".wav"},
    {"audio/ogg", ".ogg"},
    {"audio/flac", ".flac"},
    {"audio/aac", ".aac"},
    {"audio/x-ms-wma", ".wma"},
    {"audio/midi", ".mid"},
    {"audio/x-midi", ".midi"},
    {"audio/webm", ".weba"},
    {"audio/opus", ".opus"},

    // Video
    {"video/mp4", ".mp4"},
    {"video/mpeg", ".mpeg"},
    {"video/x-msvideo", ".avi"},
    {"video/x-ms-wmv", ".wmv"},
    {"video/x-matroska", ".mkv"},
    {"video/webm", ".webm"},
    {"video/quicktime", ".mov"},
    {"video/x-flv", ".flv"},
    {"video/3gpp", ".3gp"},
    {"video/mp2t", ".ts"},
    {"video/x-m4v", ".m4v"},

    // 3D Models
    {"model/gltf+json", ".gltf"},
    {"model/gltf-binary", ".glb"},
    {"model/obj", ".obj"},
    {"model/stl", ".stl"},
    {"application/x-tgif", ".obj"},

    // eBooks
    {"application/epub+zip", ".epub"},
    {"application/x-mobipocket-ebook", ".mobi"},
    {"application/vnd.amazon.ebook", ".azw"},

    // CAD
    {"application/acad", ".dwg"},
    {"application/x-autocad", ".dwg"},
    {"image/vnd.dwg", ".dwg"},
    {"image/vnd.dxf", ".dxf"},

    // Programming/Script
    {"text/x-python", ".py"},
    {"text/x-c", ".c"},
    {"text/x-c++", ".cpp"},
    {"text/x-java", ".java"},
    {"text/x-csharp", ".cs"},
    {"text/x-php", ".php"},
    {"text/x-ruby", ".rb"},
    {"text/x-perl", ".pl"},
    {"text/x-shellscript", ".sh"},
    {"application/x-httpd-php", ".php"},
    {"application/x-python-code", ".pyc"},

    // Markup/Config
    {"application/x-yaml", ".yaml"},
    {"text/yaml", ".yml"},
    {"application/toml", ".toml"},
    {"text/x-ini", ".ini"},

    // Email
    {"message/rfc822", ".eml"},
    {"application/vnd.ms-outlook", ".msg"},

    // Andere Microsoft-Formate
    {"application/x-ms-wim", ".wim"},
    {"application/vnd.ms-cab-compressed", ".cab"},
    {"application/x-ms-shortcut", ".lnk"},
    {"application/vnd.ms-project", ".mpp"},
    {"application/vnd.visio", ".vsd"},
    {"application/vnd.ms-publisher", ".pub"},
    {"application/vnd.ms-access", ".mdb"},

    // Disk Images
    {"application/x-iso9660-image", ".iso"},
    {"application/x-raw-disk-image", ".img"},
    {"application/x-virtualbox-vdi", ".vdi"},
    {"application/x-vmdk", ".vmdk"},

    // Andere
    {"application/octet-stream", ".bin"},
    {"application/x-shockwave-flash", ".swf"},
    {"application/java-archive", ".jar"},
    {"application/vnd.android.package-archive", ".apk"},
    {"application/x-debian-package", ".deb"},
    {"application/x-rpm", ".rpm"},
    {"application/vnd.apple.installer+xml", ".pkg"},
    {"application/x-apple-diskimage", ".dmg"},

    {NULL, NULL} // Terminator
};

// Lookup-Funktion
const gchar* mime_to_extension(const char* mime_type) {
    if (!mime_type) return NULL;

    for (int i = 0; mime_mappings[i].mime_type != NULL; i++) {
        if (strcmp(mime_mappings[i].mime_type, mime_type) == 0) {
            return mime_mappings[i].extension;
        }
    }

    // Fallback für unbekannte MIME-Types
    return ".bin";
}

// Fallback mit case-insensitive Vergleich
const gchar* mime_to_extension_ci(const char* mime_type) {
    if (!mime_type) return NULL;

    for (int i = 0; mime_mappings[i].mime_type != NULL; i++) {
        if (strcasecmp(mime_mappings[i].mime_type, mime_type) == 0) {
            return mime_mappings[i].extension;
        }
    }

    return ".bin";
}

// MIME-Type mit Parameter (z.B. "text/html; charset=utf-8")
const gchar* mime_to_extension_with_params(const char* mime_type) {
    if (!mime_type) return NULL;

    // MIME-Type bis zum ersten Semikolon kopieren
    char mime_base[256];
    const char* semicolon = strchr(mime_type, ';');

    if (semicolon) {
        size_t len = semicolon - mime_type;
        if (len >= sizeof(mime_base)) len = sizeof(mime_base) - 1;
        strncpy(mime_base, mime_type, len);
        mime_base[len] = '\0';

        // Trailing whitespace entfernen
        char* end = mime_base + len - 1;
        while (end > mime_base && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

        return mime_to_extension_ci(mime_base);
    }

    return mime_to_extension_ci(mime_type);
}

gchar* mime_guess_content_type(unsigned char* buffer, gsize size, GError** error) {
    gchar* result = NULL;

    //erst einmal zip-Erkennung
    if (size >= 4 && buffer[0]=='P' && buffer[1]=='K' &&
        (buffer[2]==0x03 || buffer[2]==0x05 || buffer[2]==0x07) &&
        (buffer[3]==0x04 || buffer[3]==0x06 || buffer[3]==0x08)) {
        return g_strdup("application/zip");
    }

    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (!magic) {
    	if (error) *error = g_error_new(g_quark_from_static_string("stdlib"), errno,
    			"%s\nmagic_open fehlgeschlagen: %s", __func__, strerror(errno));

        return NULL;
    }

    if (magic_load(magic, NULL) != 0) {
        g_set_error(error, g_quark_from_static_string("magic"), 0,
				"%s\nmagic_load fehlgeschlagen: %s", __func__,
				magic_error(magic));
        magic_close(magic);

        return NULL;
    }

    // MIME-Typ aus Puffer erkennen
    const char* mime = magic_buffer(magic, buffer, size);

    if (!g_strcmp0(mime, "text/plain")) { //test, ob nicht etwa GMessage
    	GMimeStream* gmime_stream = NULL;
    	GMimeParser* parser = NULL;
    	GMimeMessage* message = NULL;

    	gmime_stream = g_mime_stream_mem_new_with_buffer((const gchar*) buffer, size);

    	parser = g_mime_parser_new_with_stream(gmime_stream);
    	message = g_mime_parser_construct_message (parser, NULL);
    	g_object_unref (parser);
    	g_object_unref(gmime_stream);
    	if (message && g_mime_message_get_sender(message))
			result = g_strdup("message/rfc822");
    	else
    		result = g_strdup(mime);
    }
    else
    	result = mime ? g_strdup(mime) : g_strdup("application/octet-stream");

    magic_close(magic);

    return result;
}
