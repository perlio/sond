#ifndef MISC_STDLIB_H_INCLUDED
#define MISC_STDLIB_H_INCLUDED

int rm_r(const char*);

int mkdir_p(const char*);

char* get_exe_dir(void);

/* Hängt add_string an old_string an und gibt BEIDE Argumente frei (old_string
 * darf NULL sein). */
char* add_string(char*, char*);

/* 0 und *zahl gesetzt, wenn string nur aus Ziffern besteht (und nicht leer ist), sonst -1 */
int string_to_guint(const char*, unsigned int*);

#endif // MISC_STDLIB_H_INCLUDED

