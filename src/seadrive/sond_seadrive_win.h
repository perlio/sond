/*
 sond (sond_seadrive_win.h) - Akten, Beweisstücke, Unterlagen
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

/* Gemeinsame Windows-Definitionen der SeaDrive-Module (nur innerhalb von seadrive/) */

#ifndef SOND_SEADRIVE_WIN_H_INCLUDED
#define SOND_SEADRIVE_WIN_H_INCLUDED

#ifdef _WIN32

#include <windows.h>

/* FILE_ATTRIBUTE_* fehlen in älteren MinGW-Headern */
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS  0x00400000
#endif
#ifndef FILE_ATTRIBUTE_PINNED
#define FILE_ATTRIBUTE_PINNED                 0x00080000
#endif
#ifndef FILE_ATTRIBUTE_UNPINNED
#define FILE_ATTRIBUTE_UNPINNED               0x00100000
#endif

/* aus cfapi.h (nicht eingebunden, die Funktionen werden zur Laufzeit geladen) */
#define HRESULT_MORE_DATA      ((HRESULT)0x800700EAL)

#endif /* _WIN32 */

#endif /* SOND_SEADRIVE_WIN_H_INCLUDED */
