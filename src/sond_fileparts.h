/*
 sond (sond_fileparts.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#ifndef SRC_SOND_FILEPARTS_H_
#define SRC_SOND_FILEPARTS_H_

#include <glib-object.h>
#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gmime/gmime.h>

G_BEGIN_DECLS

#define SOND_TYPE_FILE_PART sond_file_part_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePart, sond_file_part, SOND,
		FILE_PART, GObject)

struct _SondFilePartClass {
	GObjectClass parent_class;

	/* DESIGNFEHLER: path_root liegt auf Klassenebene und gilt damit global
	 * für alle SondFilePart-Instanzen. Wenn zwei SondTreeviewFM-Widgets
	 * gleichzeitig mit unterschiedlichen Roots existieren würden, würde
	 * das zweite das erste überschreiben. Aktuell kein praktisches Problem
	 * da immer nur ein Projektverzeichnis gleichzeitig geöffnet ist.
	 * Korrekte Lösung wäre path_root als Parameter durch alle betroffenen
	 * Funktionen durchzureichen. */
	gchar* path_root;
	GPtrArray* arr_opened_files;
};

SondFilePart* sond_file_part_is_open(SondFilePart* sfp, const gchar* path);

SondFilePart* sond_file_part_create(SondFilePart* sfp_parent, const gchar* path,
		GError** error);

SondFilePart* sond_file_part_create_from_mime_type(gchar const* path,
		SondFilePart*, gchar const*);

SondFilePart* sond_file_part_create_leaf(gchar const* path,
		SondFilePart* parent, gchar const* mime_type);

SondFilePart* sond_file_part_get_parent(SondFilePart *);

void sond_file_part_set_parent(SondFilePart*, SondFilePart*);

gchar const* sond_file_part_get_path(SondFilePart *);

void sond_file_part_set_path(SondFilePart*, const gchar*);

gboolean sond_file_part_get_has_children(SondFilePart*);

void sond_file_part_set_has_children(SondFilePart*, gboolean);

/* Generisches Attribut auf Basisklassen-Ebene (wie path/parent), obwohl
 * aktuell einziger Erzeuger sond_tvfm_item_load_gmessage_dir()
 * (sond_treeviewfm.c) ist: markiert ein sfp, dessen Erzeugung aus einem
 * MIME-Part mit Content-Disposition "attachment" stammt (Anzeige-
 * Unterscheidung Attachment/Inline im Baum). Muss
 * auf der Basisklasse liegen, nicht auf SondFilePartLeaf, weil ein
 * Attachment je nach Inhalt zu jedem SondFilePart-Subtyp werden kann
 * (PDF/ZIP/GMessage/Leaf - s. sond_file_part_create_from_mime_type()).
 * Default FALSE (kein Setzen nötig für alle anderen Erzeugungswege -
 * Dateisystem, ZIP-Einträge, PDF-Embeddings kennen den Begriff
 * "Attachment" schlicht nicht). */
gboolean sond_file_part_get_is_attachment(SondFilePart*);

void sond_file_part_set_is_attachment(SondFilePart*, gboolean);

/* Passwortschutz, Basisklassen-Attribut mit zwei Schichten:
 * - Eintrag:  das Element ist ein verschlüsselter Eintrag seines ZIP-Archivs.
 *             Ein solcher sfp wird ohne Inhaltszugriff als Leaf angelegt (Typ
 *             nur aus der Endung).
 * - Inhalt:   das Element ist selbst passwortgeschützt (PDF, die ein
 *             Benutzerpasswort verlangt).
 * Zustände: NONE (nicht verschlüsselt), LOCKED (Passwort nötig, noch nicht
 * bekannt), UNLOCKED (verschlüsselt, in dieser Sitzung erfolgreich
 * entschlüsselt). Default beider Schichten NONE. */
typedef enum {
	SOND_LOCK_NONE = 0,
	SOND_LOCK_LOCKED,
	SOND_LOCK_UNLOCKED
} SondLock;

SondLock sond_file_part_get_lock_entry(SondFilePart*);

void sond_file_part_set_lock_entry(SondFilePart*, SondLock);

SondLock sond_file_part_get_lock_content(SondFilePart*);

void sond_file_part_set_lock_content(SondFilePart*, SondLock);

/* Für die Anzeige: LOCKED, solange irgendeine Schicht ein Passwort braucht,
 * sonst UNLOCKED, wenn eine Schicht entsperrt ist, sonst NONE. */
SondLock sond_file_part_get_lock(SondFilePart*);

/* TRUE, wenn das Element vor der Nutzung entsperrt werden muss: Platzhalter
 * (als Leaf nach Endung angelegt, echter Typ unbekannt) oder eine Schicht
 * gesperrt. */
gboolean sond_file_part_needs_unlock(SondFilePart*);

/* TRUE für einen Platzhalter: als Leaf nach Endung angelegt, weil der Inhalt
 * eines verschlüsselten ZIP-Eintrags nicht lesbar war. */
gboolean sond_file_part_is_placeholder(SondFilePart*);

/* Entsperrt ein gesperrtes Element mit Dialog (nur im Hauptthread). Rückgabe
 * ein SondFilePart mit echtem Typ (Freigabe durch den Aufrufer): bei einem
 * verschlüsselten ZIP-Eintrag ein neues Objekt, bei einer PDF mit
 * Dokumentpasswort dasselbe mit nun bekannten Anhängen, bei einem nicht
 * gesperrten Element dasselbe. NULL: Nutzer hat abgebrochen (error ist dann
 * G_IO_ERROR_CANCELLED) oder Fehler. Der Aufrufer ersetzt danach ggf. das
 * Baumelement. */
SondFilePart* sond_file_part_unlock(SondFilePart*, GError**);

/* Prüft, ob der Eintrag path im ZIP-Archiv sfp_zip verschlüsselt ist (nur
 * Verzeichnisdaten, kein Inhaltszugriff). FALSE bei Verzeichnissen, fehlendem
 * Eintrag oder wenn das Archiv nicht zu öffnen ist. */
gboolean sond_file_part_zip_entry_is_encrypted(SondFilePart *sfp_zip,
		gchar const *path);

/* Die verschlüsselten Dateieinträge unter prefix (NULL = ganzes Archiv,
 * sonst Verzeichnis ohne abschließendes '/'): GPtrArray mit den Pfaden im
 * Archiv (g_free). Nur Verzeichnisdaten, kein Inhaltszugriff. */
GPtrArray* sond_file_part_zip_locked_entries(SondFilePart *sfp_zip,
		gchar const *prefix);

/* Stellt sicher, dass der Eintrag lesbar ist, ohne ihn vollständig zu lesen:
 * erst still mit gemerkten Passwörtern, sonst mit Dialog (der das Passwort
 * bestätigt und merkt). FALSE bei Überspringen/Abbruch (error:
 * G_IO_ERROR_CANCELLED, die Wahl liefert sond_password_last_result()) oder
 * wenn nicht gefragt werden darf (SOND_ERROR_LOCKED). TRUE auch, wenn der
 * Eintrag gar nicht verschlüsselt ist. */
gboolean sond_file_part_zip_probe_entry(SondFilePart *sfp_zip,
		gchar const *path, GError **error);

/* Entfernt den Passwortschutz der Einträge paths (Pfade im Archiv) dauerhaft:
 * entschlüsselt sie mit den gemerkten Passwörtern (vorher z.B. mit
 * sond_file_part_zip_probe_entry() sicherstellen) und schreibt das Archiv
 * einmal neu; nicht verschlüsselte Pfade werden übersprungen, andere Einträge
 * bleiben unverändert verschlüsselt. Im Erfolgsfall ersetzt *new_sfps (darf
 * NULL sein; GPtrArray, Freigabe durch den Aufrufer) die bisherigen
 * Platzhalter durch Elemente mit echtem Typ. Fehler: SOND_ERROR_LOCKED, wenn
 * ein Passwort nicht bekannt ist (dann bleibt das Archiv unverändert). */
gint sond_file_part_zip_decrypt_entries(SondFilePart *sfp_zip,
		GPtrArray *paths, GPtrArray **new_sfps, GError **error);

/* Entfernt den Passwortschutz einer PDF dauerhaft: öffnet sie (gemerktes
 * Passwort, sonst Dialog) und schreibt sie unverschlüsselt an Ort und Stelle
 * zurück. Bei Abbruch des Dialogs G_IO_ERROR_CANCELLED. */
gint sond_file_part_pdf_decrypt(SondFilePart *sfp, GError **error);

GPtrArray* sond_file_part_get_arr_opened_files(SondFilePart*);

GBytes* sond_file_part_get_bytes(SondFilePart*, GError**);

/* Wie sond_file_part_get_bytes(); *cancel (darf NULL sein) wird beim Lesen
 * ganzer Dateien und ZIP-Einträge blockweise geprüft. Bei Abbruch: NULL mit
 * G_IO_ERROR_CANCELLED. */
GBytes* sond_file_part_get_bytes_cancellable(SondFilePart*, gint const* cancel,
		GError**);

gint sond_file_part_replace(SondFilePart*, GBytes*, GError**);

/* Schreibt den Inhalt in eine Temp-Datei (sond_tmpfiles.h); read_only: schreibgeschützt,
 * damit eine Anwendung die Kopie nicht stillschweigend als Original behandelt. */
gchar* sond_file_part_write_to_tmp_file(SondFilePart*, gboolean read_only, GError**);

gint sond_file_part_open(SondFilePart*, gboolean, GError**);

gchar* sond_file_part_get_filepart(SondFilePart*);

SondFilePart* sond_file_part_from_filepart(gchar const*, GError**);

/* Wie sond_file_part_from_filepart(), aber ohne jeden Dateizugriff (nur
 * endungsbasierte MIME-Erkennung, s. Doc-Kommentar an der Implementierung) -
 * für den hydrierungsfreien Indizierungs-Sammelpfad bei BAUM_INHALT/
 * BAUM_AUSWERTUNG. */
SondFilePart* sond_file_part_from_filepart_leaf(gchar const*, GError**);

gint sond_file_part_delete(SondFilePart*, GError**);

gint sond_file_part_rename(SondFilePart*, gchar const* path_new,
		gchar const* base_new, GError**);

gint sond_file_part_copy(SondFilePart*, SondFilePart*, gchar const*, GError**);

//SondFilePartZip definieren
#include <zip.h>

#define SOND_TYPE_FILE_PART_ZIP sond_file_part_zip_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartZip, sond_file_part_zip, SOND,
		FILE_PART_ZIP, SondFilePart)

struct _SondFilePartZipClass {
	SondFilePartClass parent_class;
};

zip_t* sond_file_part_zip_open_archive(SondFilePartZip*, gboolean writeable,
		zip_source_t**, GError**);

/* Benennt das Verzeichnis path_old im Archiv in path_new um (beide ohne
 * abschließendes '/'). G_IO_ERROR_EXISTS, wenn path_new schon vorhanden ist.
 * Verschlüsselte Einträge werden nicht gelesen, kein Passwort nötig. */
gint sond_file_part_zip_rename_dir(SondFilePartZip*, gchar const* path_old,
		gchar const* path_new, GError**);

/* Legt ein leeres Verzeichnis dir_parent/base im Archiv an (dir_parent NULL =
 * Wurzel). Ist der Name belegt, wird " (n)" angehängt. *path_new: voller Pfad
 * des neuen Verzeichnisses (ohne '/'), Freigabe durch Aufrufer. */
gint sond_file_part_zip_mkdir(SondFilePartZip*, gchar const* dir_parent,
		gchar const* base, gchar** path_new, GError**);

/* Ein direktes Kind (Datei oder Unterverzeichnis) innerhalb eines ZIP-
 * Archivs, wie von sond_file_part_zip_list_dir() geliefert. path: bei
 * Verzeichnissen der volle Pfad inkl. abschließendem '/', sonst der volle
 * Eintragsname. */
typedef struct {
	gchar* path;
	gboolean is_dir;
} SondZipDirEntry;

/* Liefert die direkten Kinder von prefix (ohne abschließendes '/', NULL =
 * Archiv-Wurzel) im ZIP-Archiv sfp_zip - GPtrArray<SondZipDirEntry*>
 * (Freigabe durch Aufrufer via g_ptr_array_unref(), Elemente werden dabei
 * automatisch mitfreigegeben). Leeres Verzeichnis: gültiges, leeres Array
 * (nicht NULL); NULL nur bei echtem Fehler (Archiv nicht zu öffnen).
 *
 * Baut den vollständigen Verzeichnis-Index des Archivs beim ersten Aufruf
 * EINMAL in einem einzigen Durchlauf über alle Einträge und hält ihn auf
 * sfp_zip gecacht (analog zum gecachten GMimeMessage bei
 * SondFilePartGMessage, s. sond_file_part_gmessage_open() in
 * sond_fileparts.c) - jeder weitere Aufruf für dasselbe Archiv, gleich für
 * welchen Unterpfad/welche Tiefe, bedient sich per O(1)-Hashtable-Lookup
 * statt das Archiv erneut vollständig zu scannen bzw. sogar erneut zu
 * öffnen. Ohne das wäre das rekursive Aufschlüsseln eines ganzen Archivs
 * (Anbinden von BAUM_FS nach BAUM_INHALT) ein weiterer O(Einträge)-Durchlauf
 * UND ein erneutes Öffnen je besuchtem Verzeichnisknoten - bei Archiven mit
 * mehreren tausend Einträgen praktisch endlos. Der Cache
 * wird bei jeder Archiv-Änderung (Einfügen/Ersetzen/Löschen/Umbenennen
 * eines Eintrags) invalidiert. */
GPtrArray* sond_file_part_zip_list_dir(SondFilePartZip* sfp_zip,
		gchar const* prefix, GError** error);

//Sond_File_Part_PDF definieren
#define SOND_TYPE_FILE_PART_PDF sond_file_part_pdf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartPDF, sond_file_part_pdf, SOND,
		FILE_PART_PDF, SondFilePart)

struct _SondFilePartPDFClass {
	SondFilePartClass parent_class;
};

pdf_document* sond_file_part_pdf_open_document(fz_context*,
		SondFilePartPDF*, gboolean prompt_for_path,
		GError **);

/* Schaltet ein geöffnetes Dokument frei (ohne Passwort, mit dem gemerkten oder
 * - bei prompt und im Hauptthread - mit Dialog). 0 freigeschaltet, 1 Passwort
 * fehlt oder Nutzer hat abgebrochen, -1 Fehler. */
gint sond_file_part_pdf_authenticate(SondFilePartPDF*, fz_context*,
		pdf_document*, gboolean prompt, GError**);

fz_buffer* pdf_doc_to_buf(fz_context* ctx, pdf_document* doc, GError** error);

gint sond_file_part_pdf_save_and_close(fz_context*, pdf_document*, SondFilePartPDF*, GError**);

/* arr_display_names (darf NULL sein): Anzeigenamen (Dateinamen) parallel zu
 * arr_children; die Pfade der Kinder sind Adressen (Schlüssel im
 * Namensbaum, s. pdf_emb_get_address()). */
gint sond_file_part_pdf_load_embedded_files(SondFilePartPDF*, GPtrArray**,
		GPtrArray**, GError**);

/* Adressen aller eingebetteten Dateien (s. pdf_emb_addresses_new()), neu
 * alloziert; NULL bei Fehler. Öffnet die PDF nur lesend. */
GPtrArray* sond_file_part_pdf_get_emb_addresses(SondFilePartPDF*, GError**);

/* Adressänderungen der übrigen eingebetteten Dateien, wenn die Datei mit
 * address gelöscht/herausverschoben (filename_new NULL) oder in
 * filename_new umbenannt wird - s. pdf_emb_address_changes(). Öffnet die
 * PDF nur lesend. */
gint sond_file_part_pdf_emb_address_changes(SondFilePartPDF*, gchar const*,
		gchar const*, GPtrArray**, GPtrArray**, GError**);

//Sond_File_Part_GMessage definieren
#define SOND_TYPE_FILE_PART_GMESSAGE sond_file_part_gmessage_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartGMessage, sond_file_part_gmessage, SOND,
		FILE_PART_GMESSAGE, SondFilePart)

struct _SondFilePartGMessageClass {
	SondFilePartClass parent_class;
};

gint sond_file_part_gmessage_load_path(SondFilePartGMessage*,
		gchar const*, GPtrArray**, GError**);

//Sond_File_Part_Leaf definieren
#define SOND_TYPE_FILE_PART_LEAF sond_file_part_leaf_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondFilePartLeaf, sond_file_part_leaf, SOND,
		FILE_PART_LEAF, SondFilePart)

struct _SondFilePartLeafClass {
	SondFilePartClass parent_class;
};

gchar const* sond_file_part_leaf_get_mime_type(SondFilePartLeaf*);

void sond_file_part_leaf_set_mime_type(SondFilePartLeaf*, gchar const*);

G_END_DECLS

#endif /* SRC_SOND_FILEPARTS_H_ */
