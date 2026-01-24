/*
 sond (sond_server_auth.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

#include "sond_server_auth.h"
#include "../sond_log_and_error.h"

/* ========================================================================
 * SessionData
 * ======================================================================== */

static SessionData* session_data_new(const gchar *username) {
    SessionData *data = g_new0(SessionData, 1);
    data->username = g_strdup(username);
    data->created = g_date_time_new_now_local();
    data->last_activity = g_date_time_ref(data->created);
    return data;
}

static void session_data_free(SessionData *data) {
    if (!data) return;
    
    g_free(data->username);
    g_date_time_unref(data->created);
    g_date_time_unref(data->last_activity);
    g_free(data);
}

/* ========================================================================
 * SessionManager
 * ======================================================================== */

SessionManager* session_manager_new(guint session_lifetime_hours,
                                     guint session_inactivity_minutes) {
    SessionManager *manager = g_new0(SessionManager, 1);
    
    manager->sessions = g_hash_table_new_full(g_str_hash, g_str_equal,
                                               g_free,
                                               (GDestroyNotify)session_data_free);
    manager->session_lifetime_hours = session_lifetime_hours;
    manager->session_inactivity_minutes = session_inactivity_minutes;
    g_mutex_init(&manager->mutex);
    
    LOG_INFO("SessionManager initialized (lifetime=%uh, inactivity=%umin)\n",
             session_lifetime_hours, session_inactivity_minutes);
    
    return manager;
}

void session_manager_free(SessionManager *manager) {
    if (!manager) return;
    
    g_hash_table_destroy(manager->sessions);
    g_mutex_clear(&manager->mutex);
    g_free(manager);
}

gchar* session_manager_create(SessionManager *manager,
                               const gchar *username) {
    g_return_val_if_fail(manager != NULL, NULL);
    g_return_val_if_fail(username != NULL, NULL);
    
    /* UUID als Session-Token generieren */
    gchar *session_token = g_uuid_string_random();
    
    SessionData *data = session_data_new(username);
    
    g_mutex_lock(&manager->mutex);
    g_hash_table_insert(manager->sessions, g_strdup(session_token), data);
    g_mutex_unlock(&manager->mutex);
    
    LOG_INFO("Session created for user '%s'\n", username);
    
    return session_token;
}

gboolean session_manager_validate(SessionManager *manager,
                                   const gchar *session_token,
                                   const gchar **username) {
    g_return_val_if_fail(manager != NULL, FALSE);
    g_return_val_if_fail(session_token != NULL, FALSE);
    
    g_mutex_lock(&manager->mutex);
    
    SessionData *data = g_hash_table_lookup(manager->sessions, session_token);
    
    if (!data) {
        g_mutex_unlock(&manager->mutex);
        return FALSE;
    }
    
    GDateTime *now = g_date_time_new_now_local();
    
    /* Prüfe Lifetime (0 = unlimited) */
    if (manager->session_lifetime_hours > 0) {
        GTimeSpan diff = g_date_time_difference(now, data->created);
        gint64 hours = diff / G_TIME_SPAN_HOUR;
        
        if (hours >= manager->session_lifetime_hours) {
            LOG_INFO("Session expired by lifetime (%.8s... / %s)\n",
                    session_token, data->username);
            g_date_time_unref(now);
            g_hash_table_remove(manager->sessions, session_token);
            g_mutex_unlock(&manager->mutex);
            return FALSE;
        }
    }
    
    /* Prüfe Inactivity (0 = disabled) */
    if (manager->session_inactivity_minutes > 0) {
        GTimeSpan diff = g_date_time_difference(now, data->last_activity);
        gint64 minutes = diff / G_TIME_SPAN_MINUTE;
        
        if (minutes >= manager->session_inactivity_minutes) {
            LOG_INFO("Session expired by inactivity (%.8s... / %s)\n",
                    session_token, data->username);
            g_date_time_unref(now);
            g_hash_table_remove(manager->sessions, session_token);
            g_mutex_unlock(&manager->mutex);
            return FALSE;
        }
    }
    
    g_date_time_unref(now);
    
    /* Session ist gültig */
    if (username) {
        *username = data->username;
    }
    
    g_mutex_unlock(&manager->mutex);
    return TRUE;
}

void session_manager_update_activity(SessionManager *manager,
                                      const gchar *session_token) {
    g_return_if_fail(manager != NULL);
    g_return_if_fail(session_token != NULL);
    
    g_mutex_lock(&manager->mutex);
    
    SessionData *data = g_hash_table_lookup(manager->sessions, session_token);
    
    if (data) {
        g_date_time_unref(data->last_activity);
        data->last_activity = g_date_time_new_now_local();
    }
    
    g_mutex_unlock(&manager->mutex);
}

void session_manager_invalidate(SessionManager *manager,
                                 const gchar *session_token) {
    g_return_if_fail(manager != NULL);
    g_return_if_fail(session_token != NULL);
    
    g_mutex_lock(&manager->mutex);
    
    SessionData *data = g_hash_table_lookup(manager->sessions, session_token);
    if (data) {
        LOG_INFO("Session invalidated (%.8s... / %s)\n",
                session_token, data->username);
    }
    
    g_hash_table_remove(manager->sessions, session_token);
    
    g_mutex_unlock(&manager->mutex);
}

guint session_manager_cleanup_expired(SessionManager *manager) {
    g_return_val_if_fail(manager != NULL, 0);
    
    g_mutex_lock(&manager->mutex);
    
    GDateTime *now = g_date_time_new_now_local();
    GList *expired = NULL;
    
    /* Sammle expired Sessions */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, manager->sessions);
    
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *token = (const gchar *)key;
        SessionData *data = (SessionData *)value;
        
        gboolean is_expired = FALSE;
        
        /* Prüfe Lifetime */
        if (manager->session_lifetime_hours > 0) {
            GTimeSpan diff = g_date_time_difference(now, data->created);
            gint64 hours = diff / G_TIME_SPAN_HOUR;
            
            if (hours >= manager->session_lifetime_hours) {
                is_expired = TRUE;
            }
        }
        
        /* Prüfe Inactivity */
        if (!is_expired && manager->session_inactivity_minutes > 0) {
            GTimeSpan diff = g_date_time_difference(now, data->last_activity);
            gint64 minutes = diff / G_TIME_SPAN_MINUTE;
            
            if (minutes >= manager->session_inactivity_minutes) {
                is_expired = TRUE;
            }
        }
        
        if (is_expired) {
            expired = g_list_prepend(expired, g_strdup(token));
        }
    }
    
    g_date_time_unref(now);
    
    /* Entferne expired Sessions */
    guint count = 0;
    for (GList *l = expired; l != NULL; l = l->next) {
        const gchar *token = (const gchar *)l->data;
        g_hash_table_remove(manager->sessions, token);
        count++;
    }
    
    g_list_free_full(expired, g_free);
    
    g_mutex_unlock(&manager->mutex);
    
    if (count > 0) {
        LOG_INFO("Cleaned up %u expired session(s)\n", count);
    }
    
    return count;
}
