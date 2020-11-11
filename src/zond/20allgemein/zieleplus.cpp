/*
zond (zieleplus.cpp) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

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


#include <podofo/podofo.h>
#include <glib/gstdio.h>

#include "../global_types.h"

extern "C" gchar* utf8_to_local_filename( const gchar* );

using namespace PoDoFo;

static char*
AddNamedDest( PdfMemDocument* document, int page_number )
{
    PdfPage* page = NULL;
    const PdfEncoding* const pEncoding = NULL;

    page = document->GetPage( page_number );

    PdfDestination destination( page, ePdfDestinationFit_Fit );

    //Eindeutigen Namen für namedDest erzeugen
    gchar* guuid = g_uuid_string_random( );
    gchar* destname = g_strdup_printf( "ZND-%s", guuid );
    g_free( guuid );

    PdfString pdf_string( destname, pEncoding );

    document->AddNamedDestination( destination, pdf_string );

    return destname;
}


extern "C" gint
SetDestPage( const DisplayedDocument* dd, gint page_number1, gint page_number2,
        gchar** dest1, gchar** dest2, gchar** errmsg )
{
    PdfMemDocument document;

    gchar* current_dir = g_get_current_dir( );
    gchar* abs_path = g_strconcat( current_dir, "/", dd->document->path, NULL );
    g_free( current_dir );
    gchar* abs_path_local = utf8_to_local_filename( abs_path );
    g_free( abs_path );

    if ( !abs_path_local )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf utf8_to_local_filename:\n",
                "Umwandlung fehlgeschlagen", NULL );

        return -1;
    }

    try
    {
        document.Load( abs_path_local );
    }
    catch ( const PdfError& rError )
    {
        *errmsg = g_strconcat( "Bei Aufruf document.Load\n",
                rError.ErrorName(rError.GetError( ) ),
                NULL );
        g_free( abs_path_local );

        return -1;
    }

    if ( page_number1 >= 0 ) *dest1 = AddNamedDest( &document, page_number1 );

    if ( page_number1 == page_number2 ) *dest2 = g_strdup( *dest1 );
    else if ( page_number2 >= 0 ) *dest2 = AddNamedDest( &document, page_number2 );

    try
    {
        document.Write( "ZND_tmp.pdf" );
        document.Write( abs_path_local );
    }
    catch ( PdfError const &rError )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf document.Write\n",
                rError.ErrorName(rError.GetError( ) ),
                "\nDokument möglicherweise geöffnet!", NULL );

        g_free( abs_path_local );

        return -1;
    }
    g_free( abs_path_local );

    if ( g_remove( "ZND_tmp.pdf" ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_remove:\n"
                "Temporäre Datei konnte nicht gelöscht werden", NULL );

        return -1;
    }

    return 0;
}


extern "C" void DisableDebug( void )
{
    PdfError::EnableDebug( false );

    return;
}
