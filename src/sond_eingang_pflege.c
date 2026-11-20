/*
 sond (sond_eingang_pflege.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_eingang_pflege.h"

#include "sond_log_and_error.h"

SondEingangAblegen sond_eingang_pflege_ablegen(SondEingangCache *cache, gchar const *project_dir,
		SondEingangZettel *zettel, GError **error) {
	g_autofree gchar *name = sond_eingang_zettel_write(zettel, project_dir, error);

	if (!name)
		return SOND_EINGANG_ABLEGEN_NICHT_GESPEICHERT;

	if (!sond_eingang_cache_apply(cache, zettel, name, error))
		return SOND_EINGANG_ABLEGEN_NICHT_UEBERNOMMEN;

	return SOND_EINGANG_ABLEGEN_OK;
}

gint sond_eingang_pflege_umbenannt(SondEingangCache *cache, gchar const *project_dir,
		gchar const *alt, gchar const *neu, GError **error) {
	SondEingangZettel *zettel = NULL;
	g_autofree gchar *name = NULL;
	guint n = 0;
	gint rc = 0;

	if (!cache || !alt || !neu)
		return 0;

	zettel = sond_eingang_zettel_new(g_get_user_name(), "umbenennen");
	n = sond_eingang_cache_uebertragen_zettel(cache, zettel, alt, neu, TRUE);

	//nichts gestempelt: nichts nachzuführen
	if (n > 0) {
		name = sond_eingang_zettel_write(zettel, project_dir, error);
		if (!name || !sond_eingang_cache_apply(cache, zettel, name, error))
			rc = -1;
	}

	sond_eingang_zettel_free(zettel);

	return rc;
}

/* Stempel an rel_path und darunter aufheben (Zettel mit der angegebenen Quelle).
 * Rückgabe: Anzahl der aufgehobenen Stempel; bei einem Fehler 0 und *error. */
static guint stempel_aufheben(SondEingangCache *cache, gchar const *project_dir,
		gchar const *rel_path, gchar const *quelle, GError **error) {
	GPtrArray *stempel = sond_eingang_cache_stempel_unter(cache, rel_path);
	SondEingangZettel *zettel = NULL;
	g_autofree gchar *name = NULL;
	guint n = 0;

	if (stempel->len == 0) {
		g_ptr_array_unref(stempel);

		return 0;
	}

	zettel = sond_eingang_zettel_new(g_get_user_name(), quelle);
	for (guint i = 0; i < stempel->len; i++)
		sond_eingang_zettel_add_entfernen(zettel, g_ptr_array_index(stempel, i));

	name = sond_eingang_zettel_write(zettel, project_dir, error);
	if (name && sond_eingang_cache_apply(cache, zettel, name, error))
		n = stempel->len;

	sond_eingang_zettel_free(zettel);
	g_ptr_array_unref(stempel);

	return n;
}

gint sond_eingang_pflege_geloescht(SondEingangCache *cache, gchar const *project_dir,
		gchar const *rel_path) {
	GError *error = NULL;

	if (!cache || !rel_path)
		return 0;

	stempel_aufheben(cache, project_dir, rel_path, "loeschen", &error);
	if (error) {
		LOG_WARN("%s: Eingangsangaben von '%s' nicht aufgehoben: %s", __func__, rel_path,
				error->message);
		g_clear_error(&error);

		return -1;
	}

	return 0;
}

gboolean sond_eingang_pflege_verschoben(SondEingangCache *cache, gchar const *project_dir,
		gchar const *alt, gchar const *neu, gboolean ist_ordner, gboolean kopiert,
		SondEingangVerschoben *ergebnis, GError **error) {
	SondEingangZettel *zettel = NULL;
	g_autofree gchar *name = NULL;
	gchar *art = NULL;
	guint n = 0;
	gboolean ok = TRUE;

	*ergebnis = SOND_EINGANG_VERSCHOBEN_KEIN_HINWEIS;

	if (!cache || !alt)
		return FALSE;

	if (!neu) { //Ziel in einem Container: dort gibt es keine Stempel
		gboolean hatte_datum = sond_eingang_cache_wirksam(cache, alt, NULL) > 0; //eigen oder geerbt
		GError *error_int = NULL;

		if (kopiert) { //das Original behält seinen Eingang, die Kopie hat keinen
			if (hatte_datum)
				*ergebnis = SOND_EINGANG_VERSCHOBEN_KOPIE_IN_CONTAINER;

			return FALSE;
		}

		n = stempel_aufheben(cache, project_dir, alt, "verschieben", &error_int);
		if (error_int) {
			g_propagate_error(error, error_int);
			*ergebnis = SOND_EINGANG_VERSCHOBEN_FEHLER_AUFHEBEN;

			return FALSE;
		}

		if (hatte_datum)
			*ergebnis = SOND_EINGANG_VERSCHOBEN_IN_CONTAINER;

		return n > 0;
	}

	zettel = sond_eingang_zettel_new(g_get_user_name(), kopiert ? "kopie" : "verschieben");

	//Stempel des Eintrags und darunter ziehen mit bzw. werden mitkopiert
	n = sond_eingang_cache_uebertragen_zettel(cache, zettel, alt, neu, !kopiert);

	/* Ohne eigenen Stempel erbt der Eintrag. Würde er am Ziel etwas anderes erben, den bisher
	 * wirksamen Stempel dort festschreiben (bei einem Ordner einmal für den Ordner, der Inhalt erbt
	 * weiter). */
	if (!sond_eingang_cache_has_own(cache, alt)) {
		gint bisher = sond_eingang_cache_wirksam(cache, alt, &art);

		if (bisher > 0 && sond_eingang_cache_wirksam(cache, neu, NULL) != bisher) {
			g_autofree gchar *iso = g_strdup_printf("%04d-%02d-%02d", bisher / 10000,
					(bisher / 100) % 100, bisher % 100);

			sond_eingang_zettel_add(zettel, neu, ist_ordner, iso, art, NULL, 0);
			n++;
		}
		g_free(art);
	}

	if (n == 0) { //nichts zu übertragen
		sond_eingang_zettel_free(zettel);

		return FALSE;
	}

	name = sond_eingang_zettel_write(zettel, project_dir, error);
	if (!name || !sond_eingang_cache_apply(cache, zettel, name, error)) {
		*ergebnis = SOND_EINGANG_VERSCHOBEN_FEHLER_UEBERTRAGEN;
		ok = FALSE;
	}

	sond_eingang_zettel_free(zettel);

	return ok;
}
