



static gint viewer_annot_delete(PdfDocumentPageAnnot *pdf_document_page_annot,
		GError** error) {
	fz_context *ctx = NULL;
	pdf_annot *pdf_annot = NULL;
	GArray* arr_journal = NULL;
	JournalEntry entry = { 0, };

	ctx = zond_pdf_document_get_ctx(pdf_document_page_annot->pdf_document_page->document);

	zond_pdf_document_mutex_lock(pdf_document_page_annot->pdf_document_page->document);
	pdf_annot = pdf_document_page_annot_get_pdf_annot(pdf_document_page_annot);
	if (!pdf_annot) {
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
		*error = g_error_new(ZOND_ERROR, 0, "%s\nAnnotation nicht gefunden", __func__);
		return -1;
	}

	fz_try(ctx) {
		gint flags = 0;

		flags = pdf_annot_flags(ctx, pdf_annot);
		pdf_set_annot_flags(ctx, pdf_annot, flags | 2);
	}
	fz_always(ctx)
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
	fz_catch(ctx) {
		if (error) *error = g_error_new( g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	pdf_document_page_annot->deleted = TRUE;

	//Entry fettig machen
	entry.pdf_document_page = pdf_document_page_annot->pdf_document_page;
	entry.type = JOURNAL_TYPE_ANNOT_DELETED;
	entry.annot_changed.pdf_document_page_annot = pdf_document_page_annot;
	entry.annot_changed.annot_before = annot_deep_copy(pdf_document_page_annot->annot);

	arr_journal = zond_pdf_document_get_arr_journal(
			pdf_document_page_annot->pdf_document_page->document);
	g_array_append_val(arr_journal, entry);

	return 0;
}

gint viewer_annot_handle_delete(PdfViewer* pv, GError** error) {
	gint rc = 0;

	ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
			pv->click_pdf_punkt.seite);

	viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

	gtk_popover_popdown(GTK_POPOVER(pv->annot_pop_edit));

	rc = viewer_annot_delete(pv->clicked_annot, error);
	if (rc)
		ERROR_Z

	fz_drop_display_list(
			zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document),
			viewer_page->pdf_document_page->display_list);
	viewer_page->pdf_document_page->display_list = NULL;
	viewer_page->pdf_document_page->thread &= 10; //4 löschen

	viewer_foreach(pv, viewer_page->pdf_document_page,
			viewer_foreach_annot_changed, &pv->clicked_annot->annot);

	pv->clicked_annot = NULL;

	return 0;
}

gint viewer_handle_annot_edit_closed(PdfViewer* pdfv, GtkWidget *popover, GError** error) {
	gchar *text = NULL;
	PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
	GtkTextIter start = { 0, };
	GtkTextIter end = { 0, };
	GtkTextBuffer *text_buffer = NULL;
	JournalEntry entry = { 0, };
	GArray* arr_journal = NULL;
	gint rc = 0;
	pdf_annot *pdf_annot = NULL;
	gchar* text_old = NULL;

	pdf_document_page_annot = g_object_get_data(G_OBJECT(popover),
			"pdf-document-page-annot");

	text_buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(pdfv->annot_textview));
	gtk_text_buffer_get_bounds(text_buffer, &start, &end);

	text = gtk_text_buffer_get_text(text_buffer, &start, &end, TRUE);

	if (!g_strcmp0(text, pdf_document_page_annot->annot.annot_text.content)) {
		g_free(text);
		return 0;
	}

	//Ist-Zustand festhalten
	text_old = pdf_document_page_annot->annot.annot_text.content; //ref übernehmen

	zond_pdf_document_mutex_lock(pdf_document_page_annot->pdf_document_page->document);
	pdf_annot = pdf_document_page_annot_get_pdf_annot(pdf_document_page_annot);
	if (!pdf_annot) {
		zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
		g_free(text);
		g_set_error(error, ZOND_ERROR, 0,
				"%s\nAnnotation nicht gefunden", __func__);

		return -1;
	}

	//Annot selbst ändern
	pdf_document_page_annot->annot.annot_text.content = text; //ref übernommen

	//in pdf_doc einspielen
	rc = pdf_annot_change(zond_pdf_document_get_ctx(
			pdf_document_page_annot->pdf_document_page->document), pdf_annot,
			pdf_document_page_annot->pdf_document_page->rotate,
			pdf_document_page_annot->annot, error);
	zond_pdf_document_mutex_unlock(pdf_document_page_annot->pdf_document_page->document);
	if (rc) {
		pdf_document_page_annot->annot.annot_text.content = text_old;
		g_free(text);

		ERROR_Z
	}

	//Jetzt entry machen
	entry.type = JOURNAL_TYPE_ANNOT_CHANGED;
	entry.pdf_document_page = pdf_document_page_annot->pdf_document_page;
	entry.annot_changed.pdf_document_page_annot = pdf_document_page_annot;

	//alte annot
	entry.annot_changed.annot_before = annot_deep_copy(pdf_document_page_annot->annot);
	//allerdings content ändern
	g_free(entry.annot_changed.annot_before.annot_text.content); //ref aufgebraucht
	entry.annot_changed.annot_before.annot_text.content = text_old; //ref aufgebraucht

	entry.annot_changed.annot_after = annot_deep_copy(pdf_document_page_annot->annot);

	arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page_annot->pdf_document_page->document);
	g_array_append_val(arr_journal, entry);

	gtk_text_buffer_set_text(text_buffer, "", -1);

	//soll nur in sämtlichen betroffenen viewern Speichern aktivieren
	//ansonsten muß nichts gemacht werden
	viewer_foreach(pdfv, pdf_document_page_annot->pdf_document_page,
			viewer_foreach_annot_changed, &entry.annot_changed.annot_after);

	return 0;
}

static gboolean viewer_annot_check_diff(DisplayedDocument* dd,
		PdfDocumentPage *pdf_document_page, fz_rect rect_old,
		fz_rect rect_new) {
	fz_rect crop = pdf_document_page->rect;

	if (pdf_document_page != dd->zpdfd_part->first_page &&
			pdf_document_page != dd->zpdfd_part->last_page) return FALSE;

	crop.y0 = dd->zpdfd_part->first_index;
	crop.y1 = dd->zpdfd_part->last_index;

	if (fz_is_valid_rect(fz_intersect_rect(rect_old, crop)) !=
			fz_is_valid_rect(fz_intersect_rect(rect_new, crop)))
		return TRUE;

	return FALSE;
}

gint viewer_annot_handle_release_clicked_annot(PdfViewer* pv,
		PdfPunkt pdf_punkt, GError** error) {
	//verschoben?
	if (!(pv->click_pdf_punkt.seite == pdf_punkt.seite
			&& pv->click_pdf_punkt.punkt.x == pdf_punkt.punkt.x
			&& pv->click_pdf_punkt.punkt.y == pdf_punkt.punkt.y)) {
		fz_context *ctx = NULL;
		JournalEntry entry = { 0, };
		GArray* arr_journal = NULL;
		gint rc = 0;
		GError* error = NULL;
		pdf_annot *pdf_annot = NULL;
		fz_rect rect_old = fz_empty_rect;

		if (pv->click_pdf_punkt.seite != pdf_punkt.seite) {
			viewer_page = g_ptr_array_index(pv->arr_pages,
					pv->click_pdf_punkt.seite);
			viewer_render_wait_for_transfer(
					viewer_page->pdf_document_page);

			if (!(viewer_page->pdf_document_page->thread & 2))
				return TRUE;
		}

		ctx = zond_pdf_document_get_ctx(
				viewer_page->pdf_document_page->document);

		zond_pdf_document_mutex_lock(
				viewer_page->pdf_document_page->document);

		pdf_annot = pdf_document_page_annot_get_pdf_annot(pv->clicked_annot);
		if (!pdf_annot) {
			zond_pdf_document_mutex_unlock(
					viewer_page->pdf_document_page->document);
			display_message(pv->vf, "Fehler - Annotation editieren\n\n",
					"Bei Aufruf pdf_annot_get_pdf_annot", NULL);

			return TRUE;
		}
		//clicked_annot->rect wurde beim Ziehen laufend angepaßt
		//im JournalEntry soll der bisherige Zustand gespeichert werden
		//der muß dann aus der annot selbt geholt werden
		fz_try(ctx)
			rect_old = pdf_annot_rect(ctx, pdf_annot);
		fz_always(ctx)
			zond_pdf_document_mutex_unlock(viewer_page->pdf_document_page->document);
		fz_catch(ctx) {
			display_message(pv->vf, "Fehler Annot ändern-\n\n",
					"Bei Aufruf pdf_annot_rect: ", fz_caught_message(ctx),
					NULL);

			return TRUE;
		}

		//neues rect kommt ja schon so an, aber clamp machen
		pv->clicked_annot->annot.annot_text.rect =
				viewer_annot_clamp_page(viewer_page, pv->clicked_annot->annot.annot_text.rect);

		//erst einmal prüfen, ob Verschiebung dazu führt, daß Annot in anderem
		//geöffneten Abschnitt aufscheint oder verschwindet
		for (gint i = 0; i < pv->zond->arr_pv->len; i++) {
			DisplayedDocument *dd = NULL;

			PdfViewer *pv_loop = g_ptr_array_index(pv->zond->arr_pv, i);
			dd = pv_loop->dd;
			do {
				if (viewer_annot_check_diff(dd, viewer_page->pdf_document_page,
						rect_old, pv->clicked_annot->annot.annot_text.rect)) {
					display_message(pv->vf,
							"Fehler - Annotation verschieben\n\n",
							"Annotation würde in geöffnerem Abschnitt entfernt oder hinzugefügt\n\n"
							"Bitte Abschnitt schließen und erneut versuchen",
							NULL);

					pv->clicked_annot->annot.annot_text.rect = rect_old;

					//Fenster hervorholen
					gtk_window_present(GTK_WINDOW(pv_loop->vf));

					return TRUE;
				}

			} while ((dd = dd->next) != NULL);
		}

		zond_pdf_document_mutex_lock(
				viewer_page->pdf_document_page->document);
		rc = pdf_annot_change(ctx, pdf_annot, viewer_page->pdf_document_page->rotate,
				pv->clicked_annot->annot, &error);
		zond_pdf_document_mutex_unlock(
				viewer_page->pdf_document_page->document);
		if (rc) {
			display_message(pv->vf, "Fehler Annot ändern-\n\n",
					error->message, NULL);
			g_error_free(error);
			pv->clicked_annot->annot.annot_text.rect = rect_old;

			return TRUE;
		}

		//ins Journal
		entry.pdf_document_page = viewer_page->pdf_document_page;
		entry.type = JOURNAL_TYPE_ANNOT_CHANGED;
		entry.annot_changed.pdf_document_page_annot = pv->clicked_annot;
		entry.annot_changed.annot_before = annot_deep_copy(pv->clicked_annot->annot);
		//rect anpassen
		entry.annot_changed.annot_before.annot_text.rect = rect_old;

		entry.annot_changed.annot_after = annot_deep_copy(pv->clicked_annot->annot);

		arr_journal = zond_pdf_document_get_arr_journal(viewer_page->pdf_document_page->document);
		g_array_append_val(arr_journal, entry);

		fz_drop_display_list(ctx,
				viewer_page->pdf_document_page->display_list);
		viewer_page->pdf_document_page->display_list = NULL;
		viewer_page->pdf_document_page->thread &= 10;

		viewer_foreach(pv, viewer_page->pdf_document_page,
				viewer_foreach_annot_changed, &entry.annot_changed.annot_after);
	} else if (pv->clicked_annot->annot.annot_text.open) {//nicht verschoben, edit-popup geöffnet
		//angeklickt -> textview öffnen
		GdkRectangle gdk_rectangle = { 0, };
		gint x = 0, y = 0, width = 0, height = 0;

		gtk_container_child_get(GTK_CONTAINER(pv->layout),
				GTK_WIDGET(viewer_page->image_page), "y", &y, NULL);
		y += (gint) (pv->clicked_annot->annot.annot_text.rect.y0 * pv->zoom
				/ 100);
		y -= gtk_adjustment_get_value(pv->v_adj);

		gtk_container_child_get(GTK_CONTAINER(pv->layout),
				GTK_WIDGET(viewer_page->image_page), "x", &x, NULL);
		x += (gint) (pv->clicked_annot->annot.annot_text.rect.x0 * pv->zoom
				/ 100);
		x -= gtk_adjustment_get_value(pv->h_adj);

		height = (gint) ((pv->clicked_annot->annot.annot_text.rect.y1
				- pv->clicked_annot->annot.annot_text.rect.y0) * pv->zoom
				/ 100);
		width = (gint) ((pv->clicked_annot->annot.annot_text.rect.x1
				- pv->clicked_annot->annot.annot_text.rect.x0) * pv->zoom
				/ 100);

		gdk_rectangle.x = x;
		gdk_rectangle.y = y;
		gdk_rectangle.width = width;
		gdk_rectangle.height = height;

		gtk_popover_popdown(GTK_POPOVER(pv->annot_pop));

		g_object_set_data(G_OBJECT(pv->annot_pop_edit),
				"pdf-document-page-annot", pv->clicked_annot);
		gtk_popover_set_pointing_to(GTK_POPOVER(pv->annot_pop_edit),
				&gdk_rectangle);
		if (pv->clicked_annot->annot.annot_text.content)
			gtk_text_buffer_set_text(gtk_text_view_get_buffer(
					GTK_TEXT_VIEW(pv->annot_textview)),
					pv->clicked_annot->annot.annot_text.content, -1);
		gtk_popover_popup(GTK_POPOVER(pv->annot_pop_edit));
		gtk_widget_grab_focus(pv->annot_pop_edit);
	}

	return 0;
}

gint viewer_annot_create_markup(PdfViewer *pv, PdfPunkt pdf_punkt, GError **error) {
	if (pv->click_pdf_punkt.seite < pdf_punkt.seite) {
		von = pv->click_pdf_punkt.seite;
		bis = pdf_punkt.seite;
	} else if (pv->click_pdf_punkt.seite > pdf_punkt.seite) {
		von = pdf_punkt.seite;
		bis = pv->click_pdf_punkt.seite;
	} else //gleiche Seite
	{
		von = pdf_punkt.seite;
		bis = pdf_punkt.seite;
	}

	for (gint page = von; page <= bis; page++) {
		ViewerPageNew *viewer_page_loop = NULL;

		if (page == pdf_punkt.seite)
			viewer_page_loop = viewer_page;
		else {
			viewer_page_loop = g_ptr_array_index(pv->arr_pages,
					page);
			viewer_render_wait_for_transfer(
					viewer_page_loop->pdf_document_page);
		}

		if (!(viewer_page_loop->pdf_document_page->thread & 2))
			return TRUE;

		rc = viewer_annot_create(viewer_page_loop, &errmsg);
		if (rc) {
			g_error_set(error, ZOND_ERROR, 0,
					"%s", errmsg);
			g_free(errmsg);

			return -1;
		}
	}
}
