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
 * vollständig indiziert, Orange = teilweise, NULL (kein Pixbuf) bei
 * SOND_INDEX_STATUS_NONE. */
GdkPixbuf* sond_icon_util_status_badge_pixbuf(SondIndexStatus status, gint size);

/* SeaDrive-Cloud-Status, per Cairo als einfacher, voll gefüllter Farbkreis
 * gezeichnet (wie SondIndexStatus) statt über Icon-Theme-Namen
 * ("view-refresh", "process-stop", "emblem-default") oder feinere Formen
 * wie Haken/Ring - beides war bei den winzigen Overlay-Größen kaum
 * unterscheidbar/erkennbar. Nur zwei Zustände bekommen ein Icon, der Rest
 * (lokal vorhanden, weder gepinnt noch offline - der Normalfall) bleibt
 * bewusst ohne Icon, um nicht unnötig visuelles Rauschen zu erzeugen:
 * Violett = nicht lokal (wird bei Zugriff heruntergeladen), Grün = dauerhaft
 * gehalten (gepinnt).
 *
 * Wichtig: OFFLINE hat Vorrang vor PINNED, weil "nicht lokal vorhanden"
 * (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) die für den Nutzer entscheidende
 * Tatsache ist - eine zusätzliche "unpinned"-Markierung sagt nichts darüber
 * aus, ob die Datei GERADE lokal vorhanden ist (das ist nur eine Absicht/
 * Markierung, keine Zustandsgarantie). "unpinned, aber noch lokal
 * vorhanden" fällt bewusst in NONE - wie der unmarkierte Normalzustand. */
typedef enum {
	SOND_SEADRIVE_BADGE_NONE = 0,
	SOND_SEADRIVE_BADGE_OFFLINE, /* nicht lokal - wird bei Zugriff heruntergeladen */
	SOND_SEADRIVE_BADGE_PINNED   /* lokal vorhanden UND dauerhaft gehalten */
} SondSeadriveBadge;

GdkPixbuf* sond_icon_util_seadrive_badge_pixbuf(SondSeadriveBadge badge, gint size);

/* Ecke, in der ein Overlay-Icon auf dem Basis-Icon plaziert wird
 * (gdk_pixbuf_composite). Aktuell nur die beiden unteren Ecken gebraucht
 * (SeaDrive-Status unten rechts, Indizierungsstatus unten links). */
typedef enum {
	SOND_ICON_CORNER_BOTTOM_LEFT,
	SOND_ICON_CORNER_BOTTOM_RIGHT
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
