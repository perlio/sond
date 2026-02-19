/*
 zond (zond_update.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2023  pelo america

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

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <zip.h>
#include <sys/stat.h>
#include <errno.h>
#include <utime.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../misc.h"
#include "../../misc_stdlib.h"
#include "../zond_init.h"

#include "../99conv/general.h"
#include "../zond_dbase.h"

#include "project.h"

static gint zond_update_unzip(Projekt *zond, InfoWindow *info_window,
		const gchar *vtag, GError **error) {
	gchar *zipname = NULL; // File path
	struct zip *za; // Zip archive
	int err; // Stores error code

	zipname = g_strconcat(zond->base_dir, "zond-x86_64-", vtag, ".zip", NULL);
	// Open the zip file
	za = zip_open(zipname, 0, &err);
	g_free(zipname);
	if (!za) {
		zip_error_t zip_error = { 0 };
		zip_error_init_with_code(&zip_error, err);
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP, "%s\nzip_open\n%s",
				__func__, zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);

		return -1;
	}

	// Unpack zip
	int num = zip_get_num_entries(za, 0);
	for (int i = 0; i < num; i++) { // Iterate through all files in zip
		struct zip_stat sb; // Stores file info
		gint rc = 0;
		gint len_name = 0;

		if (info_window->cancel) {
			zip_discard(za);
			return 1;
		}

		if (!(i % 10))
			info_window_set_progress_bar_fraction(info_window,
					(gdouble) i / (gdouble) num);

		rc = zip_stat_index(za, i, 0, &sb);
		if (rc) {
			zip_error_t *zip_error = NULL;

			zip_error = zip_get_error(za);
			*error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP,
					"%s\nzip_stat_index\n%s", __func__,
					zip_error_strerror(zip_error));
			zip_error_fini(zip_error);
			zip_discard(za);

			return -1;
		}

		len_name = strlen(sb.name);
		if (sb.name[len_name - 1] == '/') // Check if directory
				{
			gchar *dir = NULL;
			gint rc = 0;

			dir = g_strconcat(zond->base_dir, vtag, "/", sb.name, NULL);
			rc = mkdir_p(dir);
			g_free(dir);
			if (rc && errno != EEXIST) {
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
						"%s\ng_mkdir\n%s", __func__, strerror( errno));
				zip_discard(za);

				return -1;
			}
		} else {
			struct zip_file *zf = NULL; // Stores file to be extracted
			FILE *fd = NULL; // Where file is extracted to
			long long sum; // How much file has been copied so far
			gchar *filename = NULL;
			struct utimbuf s_time = { 0 };
			gint ret = 0;

			zf = zip_fopen_index(za, i, 0); // Open file within zip
			if (!zf) {
				zip_error_t *zip_error = NULL;

				zip_error = zip_get_error(za);
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP,
						"%s\nzip_fopen_index\n%s", __func__,
						zip_error_strerror(zip_error));
				zip_error_fini(zip_error);
				zip_discard(za);

				return -1;
			}

			filename = g_strconcat(zond->base_dir, vtag, "/", sb.name, NULL);
			fd = fopen(filename, "wb"); // Create new file
			g_free(filename);
			if (fd == NULL) {
				gint ret = 0;

				if (error)
					*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
							"%s\nfopen\n%s", __func__, strerror( errno));
				ret = zip_fclose(zf);
				zip_discard(za);
				if (ret) {
					zip_error_t zip_error = { 0 };

					zip_error_init_with_code(&zip_error, ret);
					if (error)
						(*error)->message = add_string((*error)->message,
								g_strdup_printf("\nFehler zip_fclose:\n%s",
										zip_error_strerror(&zip_error)));
					zip_error_fini(&zip_error);
				}

				return -1;
			}

			sum = 0;
			while (sum != sb.size) { // Copy bytes to new file
				char buf[100]; // Buffer to write stuff
				int len;
				gint resp = 0;

				len = zip_fread(zf, buf, 100);
				if (len < 0) {
					gint ret = 0;
					zip_error_t *zip_error = NULL;

					zip_error = zip_file_get_error(zf);

					if (error)
						*error = g_error_new(g_quark_from_static_string("ZIP"),
								zip_error_code_zip(zip_error), "%s\n%s",
								__func__, zip_error_strerror(zip_error));
					zip_error_fini(zip_error);

					ret = fclose(fd);
					if (ret && error)
						(*error)->message = add_string((*error)->message,
								g_strdup_printf("\nFehler fclose:\n%s",
										strerror( errno)));
					ret = zip_fclose(zf);
					zip_discard(za);
					if (ret) {
						zip_error_t zip_error = { 0 };

						zip_error_init_with_code(&zip_error, ret);
						if (error)
							(*error)->message = add_string((*error)->message,
									g_strdup_printf("\nFehler zip_fclose:\n%s",
											zip_error_strerror(&zip_error)));
						zip_error_fini(&zip_error);
					}

					return -1;
				}

				resp = fwrite(buf, 1, len, fd);
				if (resp != len) {
					gint ret = 0;

					if (error)
						*error = g_error_new(
								g_quark_from_static_string("stdlib"), errno,
								strerror( errno));

					ret = fclose(fd);
					if (ret && error)
						(*error)->message = add_string((*error)->message,
								g_strdup_printf("\nFehler fclose:\n%s",
										strerror( errno)));
					ret = zip_fclose(zf);
					zip_discard(za);
					if (ret) {
						zip_error_t zip_error = { 0 };

						zip_error_init_with_code(&zip_error, ret);
						if (error)
							(*error)->message = add_string((*error)->message,
									g_strdup_printf("\nFehler zip_fclose:\n%s",
											zip_error_strerror(&zip_error)));
						zip_error_fini(&zip_error);
					}

					return -1;

				}
				sum += len;
			}

			// Finished copying file
			ret = fflush(fd);
			if (ret) {
				gint res = 0;

				if (error)
					*error = g_error_new(g_quark_from_static_string("stdlib"),
							errno, strerror( errno));

				res = fclose(fd);
				if (res) {
					gint res2 = 0;

					if (error)
						(*error)->message = add_string((*error)->message,
								g_strdup_printf("\nFehler fclose:\n%s",
										strerror( errno)));

					res2 = zip_fclose(zf);
					zip_discard(za);
					if (res2) {
						zip_error_t zip_error = { 0 };

						zip_error_init_with_code(&zip_error, res2);
						if (error)
							(*error)->message = add_string((*error)->message,
									g_strdup_printf("\nFehler zip_fclose:\n%s",
											zip_error_strerror(&zip_error)));
						zip_error_fini(&zip_error);
					}
				}
			}
			ret = fclose(fd);
			if (ret) {
				gint res2 = 0;

				if (error)
					(*error)->message = add_string((*error)->message,
							g_strdup_printf("\nFehler fclose:\n%s",
									strerror( errno)));

				res2 = zip_fclose(zf);
				zip_discard(za);
				if (res2) {
					zip_error_t zip_error = { 0 };

					zip_error_init_with_code(&zip_error, res2);
					if (error)
						(*error)->message = add_string((*error)->message,
								g_strdup_printf("\nFehler zip_fclose:\n%s",
										zip_error_strerror(&zip_error)));
					zip_error_fini(&zip_error);
				}
			}

			ret = zip_fclose(zf);
			zip_discard(za);
			if (ret) {
				zip_error_t zip_error = { 0 };

				zip_error_init_with_code(&zip_error, ret);
				if (error)
					(*error)->message = add_string((*error)->message,
							g_strdup_printf("\nFehler zip_fclose:\n%s",
									zip_error_strerror(&zip_error)));
				zip_error_fini(&zip_error);
			}

			//Änderungsdatum anpassen
			s_time.actime = time( NULL);
			s_time.modtime = sb.mtime;
			filename = g_strconcat(zond->base_dir, vtag, "/", sb.name, NULL);
			rc = utime(filename, &s_time);
			if (rc) {
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
						"%s\nutime (file: %s)\n%s", __func__, filename,
						strerror( errno));
				g_free(filename);
				return -1;
			}
			g_free(filename);
		}
	}

	zip_discard(za);

	return 0;
}

static gint curl_progress(gpointer ptr, curl_off_t dltotal, curl_off_t dlnow,
		curl_off_t ultotal, curl_off_t ulnow) {
	InfoWindow *info_window = (InfoWindow*) ptr;

	if (info_window->cancel)
		return -1; //abbrechen

	if (dltotal == 0)
		return 0;

	info_window_set_progress_bar_fraction(info_window,
			(gdouble) dlnow / (gdouble) dltotal);

	return 0;
}

static gint zond_update_download_newest(Projekt *zond, InfoWindow *info_window,
		const gchar *vtag, GError **error) {
	CURL *curl = NULL;
	FILE *fp = NULL;
	CURLcode res;
	gchar *filename = NULL;
	gchar *url = NULL;
	gchar *outfilename = NULL;

	curl = curl_easy_init();
	if (!curl) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
				"%s\ncurl_easy_init nicht erfolgreich", __func__);
		return -1;
	}

	filename = g_strconcat("zond-x86_64-", vtag, ".zip", NULL);

	outfilename = g_strconcat(zond->base_dir, filename, NULL);
	fp = fopen(outfilename, "wb");
	g_free(outfilename);

	if (!fp) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
				"fopen gibt Fehler zurück: %s", strerror( errno));
		g_free(filename);
		return -1;
	}

	url = g_strconcat("https://github.com/perlio/sond/releases/download/", vtag,
			"/", filename, NULL);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	g_free(url);
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, info_window);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress);

	g_free(filename);

	res = curl_easy_perform(curl);
	/* always cleanup */
	curl_easy_cleanup(curl);
	fclose(fp);

	if (res == CURLE_ABORTED_BY_CALLBACK)
		return 1;
	else if (res != CURLE_OK) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
				"%s\ncurl_easy_perform:\n%s", __func__,
				curl_easy_strerror(res));
		return -1;
	}

	return 0;
}

struct memory {
	gchar *response;
	size_t size;
};

static gchar*
zond_update_get_vtag(Projekt *zond, GError **error) {
	CURL *curl = NULL;
	CURLcode res = 0;
	CurlUserData mem = { 0 };

	gboolean ret = FALSE;
	JsonParser *parser = NULL;
	JsonNode *node = NULL;
	gchar *vtag = NULL;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "perlio");
	curl_easy_setopt(curl, CURLOPT_URL,
			"https://api.github.com/repos/perlio/sond/releases/latest");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);

	res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
				"%s\ncurl_easy_perform:\n%s", __func__,
				curl_easy_strerror(res));
		free(mem.response);

		return NULL;
	}

	parser = json_parser_new();
	ret = json_parser_load_from_data(parser, mem.response, -1, error);
	free(mem.response);
	if (!ret) {
		g_prefix_error(error, "%s\njson_parser_load_from_data\n", __func__);
		g_object_unref(parser);

		return NULL;
	}

	node = json_parser_get_root(parser);
	if (JSON_NODE_HOLDS_OBJECT(node)) {
		JsonObject *object = NULL;

		object = json_node_get_object(node);

		if (json_object_has_member(object, "tag_name"))
			vtag = g_strdup(json_object_get_string_member(object, "tag_name"));
		else {
			*error = g_error_new( ZOND_ERROR, ZOND_ERROR_VTAG_NOT_FOUND,
					"json enthält kein member " "tag_name" "");
			g_object_unref(parser);

			return NULL;
		}
	} else {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_JSON_NO_OBJECT,
				"json ist kein object");
		g_object_unref(parser);

		return NULL;
	}

	g_object_unref(parser);

	return vtag;
}

gint zond_update(Projekt *zond, InfoWindow *info_window, GError **error) {
	gchar **strv_tags = NULL;
	gchar *title = NULL;
	gint rc = 0;
	gchar *argv[4] = { NULL };
	gchar *vtag = NULL;
	gboolean ret = FALSE;
	gboolean res = FALSE;
	gchar *zipname = NULL;

	vtag = zond_update_get_vtag(zond, error);
	if (!vtag) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	//tag mit aktueller version vergleichen
	strv_tags = g_strsplit(vtag + 1, ".", -1);

	if (atoi(strv_tags[0]) < atoi( MAJOR))
		goto no_update;
	else if (atoi(strv_tags[0]) > atoi( MAJOR))
		goto update;
	else {
		if (atoi(strv_tags[1]) < atoi( MINOR))
			goto no_update;
		else if (atoi(strv_tags[1]) > atoi( MINOR))
			goto update;
		else {
			if (atoi(strv_tags[2]) <= atoi( PATCH))
				goto no_update;
			else if (atoi(strv_tags[2]) > atoi( PATCH))
				goto update;
		}
	}

	no_update: g_strfreev(strv_tags);
	g_free(vtag);

	return 1;

	update: g_strfreev(strv_tags);

	title = g_strconcat("Aktewellere Version vorhanden (", vtag, ")", NULL);

	rc = abfrage_frage(zond->app_window, title,
			"Herunterladen und installieren?",
			NULL);
	g_free(title);
	if (rc != GTK_RESPONSE_YES) {
		g_free(vtag);
		return 0;
	}

	info_window_set_message(info_window,
			"Neueste Version wird heruntergeladen");
	info_window_set_progress_bar(info_window);

	//herunterladen
	rc = zond_update_download_newest(zond, info_window, vtag, error);
	if (rc) {
		g_free(vtag);

		if (rc == -1)
			ERROR_Z
		else
			return 0; //abgebrochen
	}

	info_window_set_message(info_window, "Update wird entpackt");
	info_window_set_progress_bar(info_window);

	//entpacken
	rc = zond_update_unzip(zond, info_window, vtag, error);
	if (rc) {
		g_free(vtag);
		if (rc == -1)
			ERROR_Z
		else
			return 0; //ToDo: saubermachen
	}

	info_window_set_message(info_window, "Download wird gelöscht");

	//zip-Datei löschen
	GError* error_rem = NULL;

	zipname = g_strconcat(zond->base_dir, "zond-x86_64-", vtag, ".zip", NULL);

	if (sond_remove(zipname, &error_rem)) {
		gchar *message = NULL;

		message = g_strconcat("Zip-Datei konnte nicht gelöscht werden - ",
				error_rem->message, NULL);
		g_error_free(error_rem);
		info_window_set_message(info_window, message);
		g_free(message);
	};

	info_window_set_message(info_window,
			"Starte Installer und schließe Programm");

	//Projekt schliessen
	if (zond->dbase_zond) {
		gint rc = 0;
		gchar *errmsg = NULL;

		argv[2] = g_strdup(
				zond_dbase_get_path(zond->dbase_zond->zond_dbase_store));

		rc = project_close(zond, &errmsg);
		if (rc) {
			g_free(vtag);
			g_free(argv[2]);

			if (rc == -1) {
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
						"%s\nproject_schliessen\n%s", __func__, errmsg);
				g_free(errmsg);
				return -1;
			} else
				return 0; // if ( rc == 1 )
		}
	}

	//installer starten
#ifdef __WIN32
	argv[0] = g_strconcat(zond->base_dir, vtag, "/bin/zond_installer.exe",
			NULL);
#elifdef __linux__
	argv[0] = g_strdup("ain/zond_installer");
#endif // __linux__

	argv[1] = "v" MAJOR "." MINOR "." PATCH;

	res = g_spawn_async( NULL, argv, NULL, G_SPAWN_DEFAULT,
	NULL, NULL, NULL, error);
	g_free(argv[0]);
	if (!res)
		ERROR_Z

			//Projekt schließen und zond beenden
	g_signal_emit_by_name(zond->app_window, "delete-event", NULL, &ret);

	return 0;
}

