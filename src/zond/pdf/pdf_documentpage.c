/*
zond (pdf_documentpage.c) - Akten, Beweisstücke, Unterlagen
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

#include "pdf_documentpage.h"

#include "../../misc.h"


typedef struct
{/*
    Document* document;
    fz_page* page;
    fz_rect rect;
    fz_display_list* display_list;
    fz_stext_page* stext_page;
    PVAnnotPage* pv_annot_page;
*/
} PdfDocumentpagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(PdfDocumentpage, pdf_documentpage, G_TYPE_OBJECT)


static void
pdf_documentpage_class_init( PdfDocumentpageClass* klass )
{
    return;
}


static void
pdf_documentpage_init( PdfDocumentpage* self )
{

    return;
}
