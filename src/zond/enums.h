#ifndef ENUMS_H_INCLUDED
#define ENUMS_H_INCLUDED


typedef enum _ICON_NUMS
{
    ICON_NOTHING = 0,
    ICON_NORMAL,
    ICON_ORDNER,
    ICON_DATEI,
    ICON_PDF,
    ICON_ANBINDUNG,
    ICON_AKTE,
    ICON_EXE,
    ICON_TEXT,
    ICON_DOC,
    ICON_PPP,
    ICON_SPREAD,
    ICON_IMAGE,
    ICON_VIDEO,
    ICON_AUDIO,
    ICON_EMAIL,
    ICON_HTML, //16
    ICON_DURCHS = 25,
    ICON_ORT,
    ICON_PHONE,
    ICON_WICHTIG,
    ICON_OBS,
    ICON_CD,
    ICON_PERSON,
    ICON_PERSONEN,
    ICON_ORANGE,
    ICON_BLAU,
    ICON_ROT,
    ICON_GRUEN,
    ICON_TUERKIS,
    ICON_MAGENTA,
    NUMBER_OF_ICONS
} IconNums;


typedef enum BAEUME
{
    KEIN_BAUM = -1,
    BAUM_INHALT = 0,
    BAUM_AUSWERTUNG,
    BAUM_FS,
    BAUM_ANZAHL
} Baum;



#endif // ENUMS_H_INCLUDED
