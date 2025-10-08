/*
 zond (zond_installer.c) - Akten, Beweisstücke, Unterlagen
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
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#elif defined __linux__
#include <limits.h>
#endif // _WIN32

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

#include "../misc_stdlib.h"

static int rename_files(const char *filename, const struct stat *stat_new_file,
		int flag, struct FTW *ftwbuf) {
	if (flag == FTW_F) {
		struct stat stat_old_file = { 0 };
		int rc = 0;
		char *ptr = NULL;
		char garbage_path[PATH_MAX] = { 'g', 'a', 'r', 'b', 'a', 'g', 'e', '/',
				0 };

		ptr = strchr(filename, '/') + 1; //vtag abschneiden
		strcat(garbage_path, ptr);

		rc = stat(ptr, &stat_old_file);
		if (rc && errno != ENOENT)
			printf("Konnte Eigenschaften von Datei %s nicht lesen - %s\n", ptr,
					strerror( errno));

		if (stat_new_file->st_mtime > stat_old_file.st_mtime + 1) //+1 wg. Rundungsfehlern
				{ //Datei aus zip ist neuer -> verschieben
			int rc = 0;

			rc = remove(ptr);
			if (rc && errno != ENOENT) {
				int rc = 0;
				char garbage_dir[PATH_MAX] = { 0 };

				strncpy(garbage_dir, garbage_path,
						strlen(garbage_path)
								- strlen(strrchr(garbage_path, '/')));

				rc = mkdir_p(garbage_dir);
				if (rc)
					printf(
							"Konnte Unterverzeichnis " "%s" " in garbage-Dir nicht erzeugen -%s\n",
							garbage_dir, strerror( errno));

				rc = rename(ptr, garbage_path); //verschieben in garbage
				if (rc) {
					printf(
							"Konnte Datei %s nicht in Verzeichnis garbage verschieben - %s\n",
							ptr, strerror( errno));

					return 0; //da kann man nix machen
				}
			}

			rc = rename(filename, ptr);
			if (rc)
				printf("Konnte downgeloadetes File %s nicht verschieben - %s\n",
						filename, strerror( errno));
			else
				printf("Datei " "%s" " upgedated\n", ptr);
		} else //downgeloadetes File nicht neuer
		{
			int rc = 0;

			rc = remove(filename);
			if (rc) //Abfrage auf ENOENT nicht nötig - nftw würde nicht ausgelöst
			{
				int rc = 0;
				char garbage_dir[PATH_MAX] = { 0 };

				strncpy(garbage_dir, garbage_path,
						strlen(garbage_path)
								- strlen(strrchr(garbage_path, '/')));

				rc = mkdir_p(garbage_dir);
				if (rc)
					printf(
							"Konnte Unterverzeichnis " "%s" " in garbage-Dir nicht erzeugen -%s\n",
							garbage_dir, strerror( errno));

				rc = rename(filename, garbage_path); //verschieben in garbage
				if (rc) {
					printf(
							"Konnte Datei %s nicht in Verzeichnis garbage verschieben - %s\n",
							ptr, strerror( errno));

					return 0; //da kann man nix machen
				}
			}
		}
	} else if (flag == FTW_DP) {
		int rc = 0;

		rc = rmdir(filename);
		if (rc && errno != ENOTEMPTY)
			printf("Konnte directory %s nicht löschen - %s\n", filename,
					strerror( errno));
	} else
		printf("Flag %i zurückgegeben\n", flag);

	return 0;
}

int main(int argc, char **argv) {
	int rc = 0;
	char *vtag_dir = NULL;
	char vtag_dir_tmp[MAX_PATH] = { 0 };
	char base_dir[PATH_MAX] = { 0 };
	char *vtag = NULL;

	vtag_dir = get_base_dir(); // mit / am Ende
	if (!vtag_dir) {
		printf("Konnte base-dir nicht ermitteln\n");
		goto end;
	}

	strncpy(vtag_dir_tmp, vtag_dir, strlen(vtag_dir) - 1); // letztes Zeichen (/) abschneiden
	free(vtag_dir);

	vtag = strrchr(vtag_dir_tmp, '\\') + 1;

	strncpy(base_dir, vtag_dir_tmp, strlen(vtag_dir_tmp) - strlen(vtag));

	rc = chdir(base_dir);
	if (rc) {
		printf("Konnte Arbeitsverzeichnis nicht festlegen - %s\n",
				strerror( errno));

		goto end;
	}

	rc = rm_r("garbage");
	if (rc && errno != ENOENT) {
		printf("Konnte Verzeichnis " "garbage" " nicht löschen - %s",
				strerror( errno));

		goto end;
	}

#ifdef __WIN32__
	rc = mkdir("garbage");
#elif defined __linux__
	rc = mkdir("garbage", 0700);
#endif // __WIN32__
	if (rc) {
		printf("Konnte Verzeichnis " "garbage" " nicht erzeugen - %s\n",
				strerror( errno));

		goto end;
	}

	//prüfen, ob gschemas.compiled ersetzt werden muß
	if (argv[1]) //argv[1] ist die Version des Programms, das upgedated werden soll
	{ //schema wurde zuletzt geändert nach Version v0.11.2, d.h. mit v0.11.3
	  //d.h.: wenn Version < 0.11.3, dann muß geändert werden

		int rc = 0;
		char *major = NULL;
		char *minor = NULL;
		char *patch = NULL;
		char tmp[PATH_MAX] = { 0 };
		char schema[PATH_MAX] = { 0 };

		major = argv[1] + 1; //"x.y.z", reicht für atoi!

		patch = strrchr(argv[1], '.') + 1;

		strncpy(tmp, argv[1], strlen(argv[1]) - strlen(patch) - 1);
		minor = strrchr(tmp, '.') + 1;

		if (atoi(major) < 0)
			goto del_schemas;
		else if (atoi(major) > 0)
			goto skip;
		else {
			if (atoi(minor) < 11)
				goto del_schemas;
			else if (atoi(minor) > 11)
				goto skip;
			else {
				if (atoi(patch) < 3)
					goto del_schemas;
				else
					goto skip;
			}
		}

		del_schemas: strcpy(schema, vtag);
		strcat(schema, "/share/glib-2.0/schemas/gschemas.compiled");
		rc = remove(schema);
		if (rc)
			printf("gschema.compiled konnte nicht entfernt werden - %s",
					strerror( errno));

	}

	skip:
	//zond_installer.exe aus altem Bestand löschen,
	//damit auf jeden Fall neue Datei in bin und nicht in garbage verschoben wird
	//denn die läuft und kann nicht gelöscht werden
	rc = remove("bin/zond_installer.exe");
	if (rc)
		printf(
				"Alte Datei " "zond_installer.exe" " konnte nicht gelöscht werden - %s",
				strerror( errno));

	//kopieren/löschen
//	nftw(vtag, rename_files, 10, FTW_DEPTH);

	end: rc = rmdir("garbage");
	if (rc) {
		if ( errno == ENOTEMPTY)
			printf("Verzeichnis " "%s\\garbage" " "
					"nach Beendigung des Updaters von Hand löschen!\n\n",
					base_dir);
		else
			printf(
					"Fehler beim Löschen des Verzeichnisses " "garbage" " - %s\n\n",
					strerror( errno));
	}

	//ToDo: neuen Prozeß starten, der garbage löscht (ggf. zond_installer) und zond wieder startet
	printf("Bitte Fenster schließen");

	while (1)
		;

	return 0;
}
