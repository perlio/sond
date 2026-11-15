#ifndef VIEWER_HOST_H_INCLUDED
#define VIEWER_HOST_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _PdfViewerSession PdfViewerSession;

/* Verbindet zond als Host mit den Signalen der Viewer-Session. */
void viewer_host_connect(Projekt *zond, PdfViewerSession *session);

#endif // VIEWER_HOST_H_INCLUDED
