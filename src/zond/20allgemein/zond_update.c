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
#include <unistd.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../misc.h"
#include "../../misc_stdlib.h"
#include "../zond_init.h"
#include "../../zond_version.h"

#include "../../znd/anbindung.h"
#include "../../znd/zond_dbase.h"

#include "../../znd/dbase_zond.h"
#include "project.h"

static gint zond_update_unzip(Projekt *zond, InfoWindow *info_window,
		const gchar *base_dir, const gchar *vtag, GError **error) {
	gchar *zipname = NULL;
	struct zip *za;
	int err;

	// ZIP-Datei oeffnen: liegt direkt in base_dir (z.B. "zond-x86_64-v1.0.2.zip")
	zipname = g_strconcat(base_dir, "/zond-x86_64-", vtag, ".zip", NULL);
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

	// Alle Eintraege im ZIP durchlaufen (Verzeichnisse und Dateien)
	// Das ZIP enthaelt keine Top-Level-Verzeichnis-Wrapper - die Eintraege
	// beginnen direkt mit "bin/", "share/" etc. (erzeugt durch
	// "cd RELEASE_DIR && zip -r ../RELEASE_ZIP ." im Makefile)
	int num = zip_get_num_entries(za, 0);
	for (int i = 0; i < num; i++) {
		struct zip_stat sb;
		gint rc = 0;
		gint len_name = 0;

		// Abbruch-Pruefung: Nutzer hat "Abbrechen" geklickt
		if (*(info_window->cancel)) {
			zip_discard(za);
			return 1;
		}

		// Fortschrittsbalken alle 10 Eintraege aktualisieren
		if (!(i % 10))
			info_window_set_progress_bar_fraction(info_window,
					(gdouble) i / (gdouble) num);

		// Metadaten des aktuellen Eintrags lesen (Name, Groesse, Zeitstempel)
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

		if (sb.name[len_name - 1] == '/') {
			// Verzeichnis-Eintrag: Zielverzeichnis anlegen
			// Zielpfad: base_dir/vtag/bin/ (bzw. share/, etc.)
			gchar *dir = NULL;
			gint rc = 0;

			dir = g_strconcat(base_dir, "/", vtag, "/", sb.name, NULL);
			rc = mkdir_p(dir);
			g_free(dir);
			if (rc && errno != EEXIST) {
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
						"%s\ng_mkdir\n%s", __func__, strerror( errno));
				zip_discard(za);

				return -1;
			}
		} else {
			// Datei-Eintrag: Datei aus ZIP extrahieren
			struct zip_file *zf = NULL;
			FILE *fd = NULL;
			long long sum;
			gchar *filename = NULL;
			struct utimbuf s_time = { 0 };
			gint ret = 0;

			// Datei im ZIP zur Extraktion oeffnen
			zf = zip_fopen_index(za, i, 0);
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

			// Zieldatei anlegen: base_dir/vtag/bin/dateiname.dll (o.ae.)
			// Das Verzeichnis muss bereits existieren (durch den
			// vorangegangenen Verzeichnis-Eintrag angelegt)
			filename = g_strconcat(base_dir, "/", vtag, "/", sb.name, NULL);
			fd = fopen(filename, "wb");
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

			// Dateiinhalt byteweise aus dem ZIP in die Zieldatei kopieren
			// (kleiner Puffer von 100 Bytes - historisch, koennte groesser sein)
			sum = 0;
			while (sum != sb.size) {
				char buf[100];
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

			// Puffer leeren und Datei schliessen
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

			// ZIP-Eintrag schliessen (nicht zip_discard - das wuerde das
			// gesamte Archiv schliessen und naechste Iteration crashen!)
			ret = zip_fclose(zf);
			if (ret) {
				zip_error_t zip_error = { 0 };

				zip_error_init_with_code(&zip_error, ret);
				if (error)
					(*error)->message = add_string((*error)->message,
							g_strdup_printf("\nFehler zip_fclose:\n%s",
									zip_error_strerror(&zip_error)));
				zip_error_fini(&zip_error);
			}

			// Aenderungsdatum der extrahierten Datei auf das Original-Datum
			// aus dem ZIP setzen (wichtig fuer make-Zeitstempel-Vergleiche)
			s_time.actime = time( NULL);
			s_time.modtime = sb.mtime;
			filename = g_strconcat(base_dir, "/", vtag, "/", sb.name, NULL);
			rc = utime(filename, &s_time);
			if (rc) {
				*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
						"%s\nutime (file: %s)\n%s", __func__, filename,
						strerror( errno));
				g_free(filename);
				zip_discard(za);
				return -1;
			}
			g_free(filename);
		}
	}

	// ZIP-Archiv schliessen (einmalig, nach Abschluss aller Eintraege)
	zip_discard(za);

	return 0;
}

static gint curl_progress(gpointer ptr, curl_off_t dltotal, curl_off_t dlnow,
		curl_off_t ultotal, curl_off_t ulnow) {
	InfoWindow *info_window = (InfoWindow*) ptr;

	if (*(info_window->cancel))
		return -1; //abbrechen

	if (dltotal == 0)
		return 0;

	info_window_set_progress_bar_fraction(info_window,
			(gdouble) dlnow / (gdouble) dltotal);

	return 0;
}

static gint zond_update_download_newest(Projekt *zond, InfoWindow *info_window,
		const gchar *base_dir, const gchar *vtag, GError **error) {
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

	outfilename = g_strconcat(base_dir, "/", filename, NULL);
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

typedef struct _CurlUserData {
	char *response;
	size_t size;
} CurlUserData;

static size_t curl_write_cb(void *data, size_t size, size_t nmemb,
		void *clientp) {
	size_t realsize = size * nmemb;
	CurlUserData *mem = (CurlUserData*) clientp;

	char *ptr = realloc(mem->response, mem->size + realsize + 1);
	if (ptr == NULL)
		return 0;

	mem->response = ptr;
	memcpy(&(mem->response[mem->size]), data, realsize);
	mem->size += realsize;
	mem->response[mem->size] = 0;

	return realsize;
}

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
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
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

	if (!mem.response) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
				"%s\nLeere Antwort", __func__);

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

/* TRUE, wenn vtag ("vX.Y.Z") neuer ist als die laufende Version */
static gboolean zond_update_vtag_newer(const gchar *vtag) {
	gboolean newer = FALSE;
	gchar **strv_tags = NULL;
	gint aktuell[3] = { ZOND_VERSION_MAJOR, ZOND_VERSION_MINOR,
			ZOND_VERSION_PATCH };

	if (!vtag || vtag[0] != 'v')
		return FALSE;

	strv_tags = g_strsplit(vtag + 1, ".", -1);
	if (g_strv_length(strv_tags) >= 3) {
		for (gint i = 0; i < 3; i++) {
			gint teil = atoi(strv_tags[i]);

			if (teil > aktuell[i]) {
				newer = TRUE;
				break;
			} else if (teil < aktuell[i])
				break;
		}
	}
	g_strfreev(strv_tags);

	return newer;
}

/* Laedt Version vtag herunter, entpackt sie und startet den Installer.
 Uebernimmt vtag (wird freigegeben).
 Rueckgabe: -1 Fehler, 0 abgebrochen oder Installer gestartet */
static gint zond_update_install(Projekt *zond, InfoWindow *info_window,
		gchar *vtag, GError **error) {
	gint rc = 0;
	gchar *argv[5] = { NULL };
	gboolean ret = FALSE;
	gboolean res = FALSE;
	gchar *zipname = NULL;
	char *exe_dir = NULL;
	gchar *base_dir = NULL;

	exe_dir = get_exe_dir(); // ".../bin" (zond.exe liegt direkt in bin/)
	if (!exe_dir) {
		*error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
				"%s\nKonnte eigenes Verzeichnis nicht ermitteln", __func__);
		g_free(vtag);
		return -1;
	}
	base_dir = g_path_get_dirname(exe_dir); // eine Ebene hoch
	free(exe_dir);

	info_window_set_message(info_window,
			"Neueste Version wird heruntergeladen");
	info_window_set_progress_bar(info_window);

	//herunterladen
	rc = zond_update_download_newest(zond, info_window, base_dir, vtag, error);
	if (rc) {
		g_free(vtag);
		g_free(base_dir);

		if (rc == -1)
			return -1;
		else
			return 0; //abgebrochen
	}

	info_window_set_message(info_window, "Update wird entpackt");
	info_window_set_progress_bar(info_window);

	//entpacken
	rc = zond_update_unzip(zond, info_window, base_dir, vtag, error);
	if (rc) {
		g_free(vtag);
		g_free(base_dir);
		if (rc == -1)
			return -1;
		else
			return 0; //ToDo: saubermachen
	}

	info_window_set_message(info_window, "Download wird gelöscht");

	//zip-Datei löschen
	GError* error_rem = NULL;

	zipname = g_strconcat(base_dir, "/zond-x86_64-", vtag, ".zip", NULL);

	if (!sond_remove(zipname, &error_rem)) {
		gchar *message = NULL;

		message = g_strconcat("Zip-Datei konnte nicht gelöscht werden - ",
				error_rem->message, NULL);
		g_error_free(error_rem);
		info_window_set_message(info_window, message);
		g_free(message);
	};
	g_free(zipname);

	info_window_set_message(info_window,
			"Starte Installer und schließe Programm");

	//Projekt schliessen
	if (zond_project_is_open(zond)) {
		gint rc = 0;

		argv[3] = g_strdup(
				zond_dbase_get_path(zond->proj.dbase_zond->zond_dbase_store));

		rc = project_close(zond, error);
		if (rc) {
			g_free(vtag);
			g_free(base_dir);
			g_free(argv[3]);

			if (rc == -1)
				return -1;
			else
				return 0; // if ( rc == 1 )
		}
	}

	//installer starten (liegt direkt in vtag/, nicht in vtag/bin/)
	//working_directory auf vtag/bin setzen, damit Windows die DLLs findet
#ifdef __WIN32
	argv[0] = g_strconcat(base_dir, "/", vtag, "/zond_installer.exe",
			NULL);
#elifdef __linux__
	argv[0] = g_strdup("ain/zond_installer");
#endif // __linux__

	argv[1] = "v" ZOND_VERSION_STR;
	argv[2] = g_strdup_printf("%lu", (unsigned long) getpid());

	{
		// PATH um vtag/bin erweitern, damit Windows die DLLs findet,
		// ohne das Arbeitsverzeichnis zu aendern (wuerde get_exe_dir()
		// im Installer verfaelschen)
		gchar *bin_dir = g_strconcat(base_dir, "/", vtag, "/bin", NULL);
		gchar **env = g_get_environ();
		const gchar *old_path = g_environ_getenv(env, "PATH");
		gchar *new_path = g_strconcat(bin_dir, ";", old_path, NULL);
		env = g_environ_setenv(env, "PATH", new_path, TRUE);
		g_free(bin_dir);
		g_free(new_path);

		LOG_INFO("Starte Installer: %s", argv[0]);
		LOG_INFO("PATH: %s", g_environ_getenv(env, "PATH"));

		res = g_spawn_async(NULL, argv, env, G_SPAWN_DEFAULT,
				NULL, NULL, NULL, error);
		g_strfreev(env);
	}
	g_free(argv[0]);
	g_free(argv[2]);
	g_free(vtag);
	g_free(base_dir);
	if (!res)
		return -1;

			//Projekt schließen und zond beenden
	g_signal_emit_by_name(zond->win.app_window, "delete-event", NULL, &ret);

	return 0;
}


gint zond_update(Projekt *zond, InfoWindow *info_window, GError **error) {
	gchar *vtag = NULL;
	gchar *title = NULL;
	gint rc = 0;

	vtag = zond_update_get_vtag(zond, error);
	if (!vtag) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	if (!zond_update_vtag_newer(vtag)) {
		g_free(vtag);

		return 1;
	}

	title = g_strconcat("Aktuellere Version vorhanden (", vtag, ")", NULL);
	rc = abfrage_frage(zond->win.app_window, title,
			"Herunterladen und installieren?",
			NULL);
	g_free(title);
	if (rc != GTK_RESPONSE_YES) {
		g_free(vtag);
		return 0;
	}

	return zond_update_install(zond, info_window, vtag, error);
}

/* Update-Pruefung beim Start: Abfrage im Hintergrund, Dialog nur bei
 neuerer Version; ohne Verbindung oder bei Fehlern still */
typedef struct _UpdateStart {
	Projekt *zond;
	gchar *vtag; // nur gesetzt, wenn neuere Version vorhanden
} UpdateStart;

static gboolean zond_update_start_idle(gpointer data) {
	UpdateStart *us = data;

	if (us->vtag) {
		gchar *title = g_strconcat("Aktuellere Version vorhanden (", us->vtag,
				")", NULL);
		gint rc = abfrage_frage(us->zond->win.app_window, title,
				"Herunterladen und installieren?", NULL);
		g_free(title);

		if (rc == GTK_RESPONSE_YES) {
			GError *error = NULL;
			gint cancel = 0;
			InfoWindow *info_window = info_window_open(
					us->zond->win.app_window, &cancel, "Zond Updater");

			rc = zond_update_install(us->zond, info_window, us->vtag, &error);
			us->vtag = NULL; // freigegeben durch zond_update_install
			info_window_kill(info_window);
			if (rc == -1) {
				display_message(us->zond->win.app_window,
						"Update fehlgeschlagen\n\n", error->message, NULL);
				g_error_free(error);
			}
		}
	}

	g_free(us->vtag);
	g_free(us);

	return G_SOURCE_REMOVE;
}

static gpointer zond_update_start_thread(gpointer data) {
	UpdateStart *us = data;
	GError *error = NULL;

	us->vtag = zond_update_get_vtag(us->zond, &error);
	g_clear_error(&error);
	if (us->vtag && !zond_update_vtag_newer(us->vtag)) {
		g_free(us->vtag);
		us->vtag = NULL;
	}

	g_idle_add(zond_update_start_idle, us);

	return NULL;
}

void zond_update_check_on_start(Projekt *zond) {
	UpdateStart *us = g_new0(UpdateStart, 1);

	us->zond = zond;
	curl_global_init(CURL_GLOBAL_DEFAULT); // vor dem Thread, nicht threadsicher
	g_thread_unref(g_thread_new("zond-update-check", zond_update_start_thread,
			us));
}
