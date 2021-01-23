#ifdef MAIN_C
#define EXTERN
#else
#define EXTERN extern
#endif // MAIN_C

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <cairo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <mariadb/mysql.h>

#include "global_types_sojus.h"

#include "00misc/auswahl.h"
#include "00misc/settings.h"
#include "00misc/sql.h"
#include "00misc/zeit.h"

#include "01Desktop/callbacks_adressen.h"
#include "01Desktop/callbacks_akten.h"
#include "01Desktop/callbacks_einstellungen.h"

#include "02Akten/aktenbet.h"
#include "02Akten/aktenbetwidgets.h"
#include "02Akten/akten.h"
#include "02Akten/aktenfenster.h"
#include "02Akten/widgets_akte.h"

#include "03Adressen/adresse_auswahl_neu.h"
#include "03Adressen/adressen.h"
#include "03Adressen/adressenfenster.h"
#include "03Adressen/widgets_adresse.h"

#include "20Einstellungen/db.h"
#include "20Einstellungen/sachbearbeiterverwaltung.h"
#include "20Einstellungen/widgets_einstellungen.h"
