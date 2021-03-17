/*
zond (zond_datei.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

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

#include "zond_datei.h"

#include "../misc.h"


typedef struct
{
    gchar* rel_path;

} ZondDateiPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondDatei, zond_datei, G_TYPE_OBJECT)


static void
zond_datei_class_init( ZondDateiClass* klass )
{
    return;
}


static void
zond_datei_init( ZondDatei* self )
{

    return;
}
