#ifndef SRC_SOND_STRING_UTIL_H_
#define SRC_SOND_STRING_UTIL_H_

/*
 * add_string() stand bisher in misc.c/h - hierher verschoben, weil die
 * Funktion selbst generisch ist (kein GTK, kein Windows), misc.c/h als
 * Ganzes aber GTK-gebunden sind und deshalb für sond_server nicht in
 * Frage kommen. Bewusst ohne glib.h-Include und mit "char*" statt
 * "gchar*" in der Deklaration, damit diese Datei auch dort benutzt werden
 * kann, wo (wie in misc.h) gchar nur ein eigenes typedef auf char ist -
 * beide Typen sind identisch, es gibt also keinen Konflikt.
 */
char* add_string(char *old_string, char *add_string);

#endif /* SRC_SOND_STRING_UTIL_H_ */
