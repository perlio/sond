#include "pdf.h"

#include <glib/gstdio.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "test.h"

#include "../../misc.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"

#include "../zond_pdf_document.h"

/** Nicht thread-safe  **/
gint pdf_document_get_dest(fz_context *ctx, pdf_document *doc, gint page_doc,
		gpointer *ret, gboolean first_occ, gchar **errmsg) {
	pdf_obj *obj_dest_tree = NULL;
	pdf_obj *obj_key = NULL;
	pdf_obj *obj_val = NULL;
	pdf_obj *obj_val_resolved = NULL;
	pdf_obj *obj_page = NULL;
	gint num = 0;
	const gchar *dest_found = NULL;

	fz_try( ctx )
		obj_dest_tree = pdf_load_name_tree(ctx, doc, PDF_NAME(Dests));
	fz_catch(ctx)
		ERROR_MUPDF("pdf_load_name_tree")

	for (gint i = 0; i < pdf_dict_len(ctx, obj_dest_tree); i++) {
		fz_try( ctx )
			obj_key = pdf_dict_get_key(ctx, obj_dest_tree, i);
		fz_catch(ctx) ERROR_MUPDF( "pdf_dict_get_key" )

		fz_try( ctx ) obj_val = pdf_dict_get_val( ctx, obj_dest_tree, i );
		fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_get_val" )

		fz_try( ctx ) obj_val_resolved = pdf_resolve_indirect( ctx, obj_val );
		fz_catch( ctx )
		{
			pdf_drop_obj( ctx, obj_dest_tree );

			ERROR_MUPDF( "pdf_resolve_indirect" )
		}

		//Altmodische PDF verweisen im NameTree auf ein Dict mit dem Schlüssel /D
		if ( pdf_is_array( ctx, obj_val_resolved ) ) obj_page =
				pdf_array_get( ctx, obj_val_resolved, 0 );
		else if ( pdf_is_dict( ctx, obj_val_resolved ) )
		{
			pdf_obj* obj_array = NULL;

			obj_array = pdf_dict_get( ctx, obj_val_resolved, PDF_NAME(D) );
			obj_page = pdf_array_get( ctx, obj_array, 0 );
		}
		else //Name-Tree-Val ist weder array noch dict - es widerspricht der obersten Direktive
		{
			pdf_drop_obj( ctx, obj_dest_tree );
			ERROR_S_MESSAGE( "NamedTree für NamedDests irregulär" )
		}

		fz_try( ctx ) num = pdf_lookup_page_number( ctx, doc, obj_page );
		fz_catch( ctx )
		{
			pdf_drop_obj( ctx, obj_dest_tree );

			ERROR_MUPDF( "pdf_lookup_page_number" )
		}

		if ( num == page_doc )
		{
			dest_found = pdf_to_name( ctx, obj_key );
			if ( g_str_has_prefix( dest_found, "ZND-" ) )
			{
				if ( g_uuid_string_is_valid( dest_found + 4 ) )
				{
					if ( first_occ )
					{
						*ret = (gpointer) g_strdup( dest_found );
						break;
					}
					else g_ptr_array_add( (GPtrArray*) *ret, (gpointer) g_strdup( dest_found ) );
				}
			}
		}
	}

	pdf_drop_obj(ctx, obj_dest_tree);

	return 0;
}

static fz_rect pdf_annot_rotate_rect(gint rotate, fz_rect rect) {
	if (rotate == 90) {
		rect.x0 -= 20;
		rect.x1 -= 20;
	} else if (rotate == 180) {
		rect.x0 -= 20;
		rect.x1 -= 20;
		rect.y0 -= 20;
		rect.y1 -= 20;
	} else if (rotate == 270) {
		rect.y0 -= 20;
		rect.y1 -= 20;
	}

	return rect;
}

gint pdf_annot_change(fz_context* ctx, pdf_annot* pdf_annot, gint rotate,
		Annot annot, GError** error) {
	if (annot.type == PDF_ANNOT_HIGHLIGHT || annot.type == PDF_ANNOT_UNDERLINE) {
		for (gint i = 0; i < annot.annot_text_markup.arr_quads->len; i++) {
			fz_try( ctx )
				pdf_add_annot_quad_point(ctx, pdf_annot,
						g_array_index(annot.annot_text_markup.arr_quads, fz_quad, i));
			fz_catch (ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
						"%s\n%s", __func__, fz_caught_message(ctx));

				return -1;
			}
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) {
			if (annot.annot_text.content)
				pdf_set_annot_contents(ctx, pdf_annot, annot.annot_text.content);
			pdf_set_annot_rect(ctx, pdf_annot, pdf_annot_rotate_rect(rotate, annot.annot_text.rect));
		}
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return -1;
		}
	}

	pdf_update_annot(ctx, pdf_annot);

	return 0;
}

pdf_annot* pdf_annot_create(fz_context* ctx, pdf_page* pdf_page, gint rotate,
		Annot annot, GError** error) {
	pdf_annot* pdf_annot = NULL;
	gint rc = 0;

	fz_try(ctx) pdf_annot = pdf_create_annot(ctx, pdf_page, annot.type);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return NULL;
	}
	pdf_drop_annot(ctx, pdf_annot); //geht schon jetzt; page behält ref!

	if (annot.type == PDF_ANNOT_UNDERLINE) {
		const gfloat color[3] = { 0.1, .85, 0 };
		fz_try(ctx) pdf_set_annot_color(ctx, pdf_annot, 3, color);
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) pdf_set_annot_icon_name( ctx, pdf_annot, "Comment" );
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	}

	rc = pdf_annot_change(ctx, pdf_annot, rotate, annot, error);
	if (rc)
		ERROR_Z_VAL(NULL)

	return pdf_annot;
}

pdf_annot* pdf_annot_lookup_index(fz_context* ctx, pdf_page* pdf_page, gint index) {
	pdf_annot *pdf_annot = NULL;

	pdf_annot = pdf_first_annot(ctx, pdf_page); //kein Fehler

	for (gint i = 0; i < index; i++)
		pdf_annot = pdf_next_annot(ctx, pdf_annot);

	return pdf_annot;
}


/*
// Vergleicht zwei PDF-Strings
int compare_pdf_strings(fz_context *ctx, pdf_obj *a, const char *b)
{
    const char *astr = pdf_to_str_buf(ctx, a);
    return strcmp(astr, b);
}

// Fügt (key, value) sortiert in ein /Names-Array ein
void insert_sorted_into_names_array(fz_context *ctx, pdf_document *doc, pdf_obj *array, const char *key, pdf_obj *value)
{
    int n = pdf_array_len(ctx, array);
    pdf_obj *new_key = pdf_new_string(ctx, doc, key, strlen(key));

    // Suche die richtige Stelle zum Einfügen
    for (int i = 0; i < n; i += 2) {
        pdf_obj *entry_key = pdf_array_get(ctx, array, i);
        if (compare_pdf_strings(ctx, entry_key, key) > 0) {
            pdf_array_insert(ctx, array, i, value);
            pdf_array_insert(ctx, array, i, new_key);
            return;
        }
    }

    // Wenn wir hier sind: hinten anhängen
    pdf_array_push(ctx, array, new_key);
    pdf_array_push(ctx, array, value);
}

// Aktualisiert /Limits eines Leaf-Nodes nach Hinzufügen eines Keys
static void update_limits(fz_context *ctx, pdf_document *doc, pdf_obj *leaf, const char *key)
{
    pdf_obj *limits = pdf_dict_gets(ctx, leaf, "Limits");

    if (!limits || !pdf_is_array(ctx, limits) || pdf_array_len(ctx, limits) != 2) {
        limits = pdf_new_array(ctx, doc, 2);
        pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
        pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
        pdf_dict_puts(ctx, leaf, "Limits", limits);
        return;
    }

    pdf_obj *min = pdf_array_get(ctx, limits, 0);
    pdf_obj *max = pdf_array_get(ctx, limits, 1);

    if (compare_pdf_strings(ctx, min, key) > 0)
        pdf_array_put(ctx, limits, 0, pdf_new_string(ctx, doc, key, strlen(key)));

    if (compare_pdf_strings(ctx, max, key) < 0)
        pdf_array_put(ctx, limits, 1, pdf_new_string(ctx, doc, key, strlen(key)));
}

// Fügt in einen bestehenden Leaf-Node ein
void insert_into_leaf_node(fz_context *ctx, pdf_document *doc, pdf_obj *leaf, const char *key, pdf_obj *value)
{
    pdf_obj *names = pdf_dict_gets(ctx, leaf, "Names");
    if (!names || !pdf_is_array(ctx, names)) {
        names = pdf_new_array(ctx, doc, 2);
        pdf_dict_puts(ctx, leaf, "Names", names);
    }

    insert_sorted_into_names_array(ctx, doc, names, key, value);
    update_limits(ctx, doc, leaf, key);
}

// Erstellt neuen Leaf-Node mit diesem Key/Value
static pdf_obj* create_leaf_node(fz_context *ctx, pdf_document *doc, const char *key, pdf_obj *value)
{
    pdf_obj *leaf = pdf_new_dict(ctx, doc, 2);
    pdf_obj *names = pdf_new_array(ctx, doc, 2);
    pdf_array_push(ctx, names, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_array_push(ctx, names, value);
    pdf_dict_puts(ctx, leaf, "Names", names);

    pdf_obj *limits = pdf_new_array(ctx, doc, 2);
    pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_array_push(ctx, limits, pdf_new_string(ctx, doc, key, strlen(key)));
    pdf_dict_puts(ctx, leaf, "Limits", limits);

    return leaf;
}

// Hauptfunktion: trägt (key, value) korrekt in den Nametree ein
gint pdf_insert_into_nametree(fz_context *ctx, pdf_document *doc,
		pdf_obj *nametree, const char *key, pdf_obj *value, GError **error) {
    pdf_obj *names = NULL;

    fz_try(ctx)
    	names = pdf_dict_gets(ctx, nametree, "Names");
    fz_catch(ctx)
    	ERROR_PDF

    if (names) {
    	gboolean is_array = FALSE;

    	fz_try(ctx)
    		is_array = pdf_is_array(ctx, names);
    	fz_catch(ctx)
    		ERROR_PDF

    	if (is_array) {
    		gint rc = 0;

    		rc = insert_sorted_into_names_array(ctx, doc, names, key, value, error);
    		if (rc)
    			ERROR_Z

			rc = update_limits(ctx, doc, nametree, key, error);
    		if (rc)
				ERROR_Z

			return 0;
    	}
    	else {
    		if (error) *error_ = g_error_new(g_quark_from_static_string("sond"),
					0, "%s\ntree malformed", __func__);

    		return -1;
    	}
    }

    // Kein /Names → vielleicht /Kids
    pdf_obj *kids = pdf_dict_gets(ctx, nametree, "Kids");
    if (!kids || !pdf_is_array(ctx, kids)) {
        // Kein /Kids → neuen Leaf-Node direkt einfügen
        pdf_obj *leaf = create_leaf_node(ctx, doc, key, value);
        kids = pdf_new_array(ctx, doc, 1);
        pdf_array_push(ctx, kids, leaf);
        pdf_dict_puts(ctx, nametree, "Kids", kids);
        return;
    }

    // Suche passenden Kid
    int n = pdf_array_len(ctx, kids);
    for (int i = 0; i < n; ++i) {
        pdf_obj *kid = pdf_array_get(ctx, kids, i);
        pdf_obj *limits = pdf_dict_gets(ctx, kid, "Limits");

        if (!limits || !pdf_is_array(ctx, limits) || pdf_array_len(ctx, limits) != 2) {
            insert_into_leaf_node(ctx, doc, kid, key, value);
            return;
        }

        const char *min = pdf_to_str_buf(ctx, pdf_array_get(ctx, limits, 0));
        const char *max = pdf_to_str_buf(ctx, pdf_array_get(ctx, limits, 1));

        if (strcmp(key, min) >= 0 && strcmp(key, max) <= 0) {
            insert_into_leaf_node(ctx, doc, kid, key, value);
            return;
        }
    }

    // Kein passender Kid → neuen Leaf anlegen
    pdf_obj *new_leaf = create_leaf_node(ctx, doc, key, value);
    pdf_array_push(ctx, kids, new_leaf);
}
*/
