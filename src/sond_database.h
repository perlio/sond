#ifndef SOND_DATABASE_H_INCLUDED
#define SOND_DATABASE_H_INCLUDED

typedef char gchar;


const gchar* sond_database_sql_create_database( void );

const gchar* sond_database_sql_insert_labels( void );

const gchar* sond_database_sql_insert_adm_rels( void );

#endif //SOND_DATABASE_H_INCLUDED
