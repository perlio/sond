/*
 sond (sond_icon_util.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_ICON_UTIL_H_
#define SRC_SOND_ICON_UTIL_H_

#include <gtk/gtk.h>

#include "sond_index.h"
#include "seadrive/sond_seadrive_core.h" /* SondSeadriveBadge, SondSeadriveDirStatus */

G_BEGIN_DECLS

/* Lädt ein benanntes Icon aus dem (um den Ressourcenpfad "/icons"
 * erweiterten) Icon-Theme als GdkPixbuf in der gewünschten Kantenlänge,
 * oder NULL bei Fehler/unbekanntem Namen. Gemeinsam genutzt von allen
 * Baum-Widgets, die Icons manuell zu einem Overlay zusammensetzen
 * (gdk_pixbuf_composite) statt sie per "icon-name"-Property zu setzen. */
GdkPixbuf* sond_icon_util_load_pixbuf(GtkWidget *widget,
		gchar const *icon_name, gint size);

/* Zeichnet ein kleines farbiges Kreis-Badge für den Indizierungsstatus
 * direkt per Cairo, statt über einen Icon-Theme-Namen zu gehen (Standard-
 * Freedesktop-Namen wie "emblem-ok" sind auf einem minimalen Windows/
 * MSYS2-Setup ohne vollständiges Icon-Theme oft nicht auflösbar). Grün =
 * vollständig indiziert, Grau = teilweise (Orange wirkte als Warn-/
 * Unfertig-Signal statt eines reinen Ist-Zustands, s. auch
 * SEADRIVE_DIR_STATUS_MIXED), NULL (kein Pixbuf) bei
 * SOND_INDEX_STATUS_NONE. */
GdkPixbuf* sond_icon_util_status_badge_pixbuf(SondIndexStatus status, gint size);

/* Attachment-Badge: einfaches, über das
 * Icon-Theme geladenes Symbol ("mail-attachment-symbolic") statt eines
 * Farbkreises wie bei den Status-Badges oben - hier gibt es keine
 * mehrwertige Zustandsskala (indiziert/teilweise/SeaDrive-Status), sondern
 * nur eine binäre Eigenschaft (Content-Disposition: attachment ja/nein,
 * s. sond_file_part_get_is_attachment()), für die ein wiedererkennbares
 * Symbol (Büroklammer) klarer ist als eine weitere Farbe. NULL, wenn das
 * Icon-Theme das Symbol nicht kennt (Aufrufer zeigt dann einfach kein
 * Badge, analog zum Verhalten bei den übrigen *_badge_pixbuf()-Funktionen
 * im Fehlerfall). */
GdkPixbuf* sond_icon_util_attachment_badge_pixbuf(GtkWidget *widget, gint size);

/* Schloss-Badge für passwortgeschützte Elemente (s. SondLock in
 * sond_fileparts.h); wie das Attachment-Badge ein Symbol aus dem Icon-Theme,
 * NULL wenn das Theme es nicht kennt. unlocked: entsperrt - dasselbe Symbol
 * in Grün, sonst in der Theme-Farbe. */
GdkPixbuf* sond_icon_util_locked_badge_pixbuf(GtkWidget *widget, gint size,
		gboolean unlocked);


/* SeaDrive-Zustand als Farbkreis (Aufzählung SondSeadriveBadge, s. seadrive/sond_seadrive_core.h);
 * der Normalfall (lokal, nicht gepinnt) bleibt ohne Icon. */
GdkPixbuf* sond_icon_util_seadrive_badge_pixbuf(SondSeadriveBadge badge, gint size);


/* Teilbaum-Status eines Ordners als Farbkreis (SondSeadriveDirStatus, s. seadrive/sond_seadrive_core.h) */
GdkPixbuf* sond_icon_util_seadrive_dir_badge_pixbuf(SondSeadriveDirStatus status, gint size);

/* Ecke, in der ein Overlay-Icon auf dem Basis-Icon plaziert wird
 * (gdk_pixbuf_composite). Unten rechts steht der SeaDrive-Status, unten
 * links der Indizierungsstatus; TOP_RIGHT ist für das Attachment-Badge
 * (E-Mail-Mimeparts, s. sond_file_part_get_is_attachment()) da, TOP_LEFT für
 * das Schloss (s. SondLock in sond_fileparts.h) - unten wären
 * beide Ecken für Dateien mit SeaDrive-Status UND Indizierungsstatus schon
 * belegt. */
typedef enum {
	SOND_ICON_CORNER_BOTTOM_LEFT,
	SOND_ICON_CORNER_BOTTOM_RIGHT,
	SOND_ICON_CORNER_TOP_LEFT,
	SOND_ICON_CORNER_TOP_RIGHT
} SondIconCorner;

/* Ein einzelnes Overlay für sond_icon_util_render_with_overlays():
 * pixbuf ist geliehen (wird dort nicht ge-unreft, bleibt Eigentum des
 * Aufrufers) und muss bereits in der passenden Overlay-Größe vorliegen
 * (s. sond_icon_util_renderer_get_size()/MAX(px/2,8)). pixbuf == NULL wird
 * ignoriert (z.B. wenn das Laden vorher fehlgeschlagen ist). */
typedef struct {
	GdkPixbuf *pixbuf;
	SondIconCorner corner;
} SondIconOverlay;

/* Liest die zu renderer passende Icon-Kantenlänge in Pixeln aus dessen
 * "stock-size"-Property (mit Fallback 16), gemeinsam benutzt, um vor dem
 * Erzeugen der Overlay-Pixbufs (die halb so groß sind) die Zielgröße zu
 * kennen. */
gint sond_icon_util_renderer_get_size(GtkCellRenderer *renderer);

/* Ältere Projekte enthalten als Icon-Name statt eines Theme-Icons die
 * Registry-Angabe des Windows-Dateisymbols (z.B.
 * "C:\\Program Files\\Mozilla Thunderbird\\thunderbird.exe,0"). Bildet solche
 * Namen auf ein Theme-Icon ab; alle übrigen Namen kommen unverändert zurück.
 * Der Rückgabewert gehört dem Aufrufer nicht (statisch oder name selbst). */
gchar const* sond_icon_util_normalize_name(gchar const *name);

/* Lädt base_icon_name als Pixbuf in der zu renderer passenden Größe,
 * komponiert die angegebenen Overlays (je halb so groß wie das Basis-Icon,
 * s. sond_icon_util_renderer_get_size()) an ihrer jeweiligen Ecke darauf
 * und setzt das Ergebnis als "pixbuf"-Property von renderer. Schlägt das
 * Laden des Basis-Icons fehl (z.B. unvollständiges Icon-Theme), wird
 * stattdessen die einfache "icon-name"-Property gesetzt (keine Overlays
 * sichtbar dann) und eine Warnung geloggt; Rückgabe FALSE in diesem Fall,
 * sonst TRUE. Zentralisiert das Compositing-Muster, das SeaDrive-Status
 * (sond_treeviewfm.c) und Indizierungsstatus (sond_treeviewfm.c,
 * zond_treeview.c) beide brauchen. */
gboolean sond_icon_util_render_with_overlays(GtkWidget *widget,
		GtkCellRenderer *renderer, gchar const *base_icon_name,
		SondIconOverlay const *overlays, guint n_overlays);

G_END_DECLS

#endif /* SRC_SOND_ICON_UTIL_H_ */
