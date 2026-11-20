#ifndef ZOND_ERROR_H_INCLUDED
#define ZOND_ERROR_H_INCLUDED

#include <glib.h>

#define ZOND_ERROR zond_error_quark()
GQuark zond_error_quark(void);

enum ZondError
{
	ZOND_ERROR_IO,
	ZOND_ERROR_JSON_NO_OBJECT,
	ZOND_ERROR_VTAG_NOT_FOUND,
	ZOND_ERROR_CURL,
	ZOND_ERROR_ZIP,
	NUM_ZOND_ERROR
};

#endif // ZOND_ERROR_H_INCLUDED
