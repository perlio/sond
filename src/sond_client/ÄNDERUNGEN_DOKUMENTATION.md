# ÄNDERUNGEN FÜR LAZY AUTHENTICATION UND AUTO RE-LOGIN

## Übersicht

Die Änderungen implementieren:
1. **Lazy Authentication**: Login erfolgt erst beim ersten Server-Request
2. **Auto Re-Authentication**: Bei abgelaufenem Token (401/403/500) automatisch neu einloggen

## Geänderte Dateien

### 1. sond_client.h (→ sond_client_MODIFIED.h)

**Neue Funktion hinzugefügt:**
```c
void sond_client_set_login_callback(SondClient *client,
                                    gboolean (*callback)(SondClient*, gpointer),
                                    gpointer user_data);
```

Diese Funktion setzt einen Callback, der für Login/Re-Login aufgerufen wird.
Der Callback muss:
- TRUE zurückgeben bei erfolgreichem Login
- FALSE zurückgeben bei Abbruch
- Nach erfolgreichem Login `sond_client_set_auth()` aufrufen

**Dokumentation angepasst:**
- Alle Server-Request-Funktionen führen jetzt automatisch Login durch
- Bei abgelaufenem Token wird automatisch Re-Login durchgeführt

### 2. sond_client.c (→ sond_client_MODIFIED.c)

**Neue Struct-Member:**
```c
struct _SondClient {
    // ... bestehende Members ...
    
    /* Lazy-Login Callback */
    gboolean (*login_callback)(SondClient *client, gpointer user_data);
    gpointer login_callback_user_data;
};
```

**Neue interne Funktionen:**

#### `ensure_authenticated()`
- Prüft ob Token vorhanden ist
- Falls nein: Ruft login_callback auf (Lazy Login)
- Wird am Anfang jeder Server-Request-Funktion aufgerufen

#### `handle_auth_error()`
- Wird bei 401/403/500-Fehler aufgerufen
- Löscht altes Token
- Ruft auth_failed_callback auf (UI-Benachrichtigung)
- Ruft login_callback auf für Re-Login
- Gibt TRUE zurück bei erfolgreichem Re-Login

**Geänderte Funktionen:**

Alle Server-Request-Funktionen wurden um einen Retry-Loop erweitert:

```c
gboolean sond_client_lock_node(...) {
    // 1. Authentifizierung sicherstellen (Lazy Login)
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    // 2. Request-Body vorbereiten
    // ...
    
    // 3. Retry-Loop für Auth-Fehler
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;  // Nur ein Retry
        
        // HTTP-Request senden
        // ...
        
        guint status = soup_message_get_status(msg);
        
        // Auth-Fehler? (401, 403 oder 500 als Fallback)
        if (status == 401 || status == 403 || status == 500) {
            // Re-Login versuchen
            if (handle_auth_error(client, error)) {
                retry = TRUE;  // Mit neuem Token nochmal
                continue;
            }
            break;
        }
        
        // Normales Error-Handling
        // ...
    }
    
    return success;
}
```

**Warum 500 als Fallback?**
Sie erwähnten, dass der Server aktuell 500 zurückgibt bei abgelaufenem Token.
Der Code behandelt daher 401, 403 UND 500 als Auth-Fehler.
Später können Sie den Server anpassen, um korrekt 401 zurückzugeben.

### 3. main.c (→ main_MODIFIED.c)

**Hauptänderungen:**

```c
int main(int argc, char *argv[]) {
    // ... Client erstellen ...
    
    // 1. Login-Callback setzen (für Lazy-Login und Re-Login)
    sond_client_set_login_callback(client, on_login_needed, 
                                    "Session abgelaufen. Bitte erneut anmelden.");
    
    // 2. Auth-Failed-Callback setzen (nur für UI-Benachrichtigung)
    sond_client_set_auth_failed_callback(client, on_auth_failed, NULL);
    
    // 3. KEIN Login beim Start mehr!
    //    Login erfolgt erst bei erster Server-Anfrage
    
    // ... Rest der Initialisierung ...
}
```

**Callback-Implementierung:**

```c
static gboolean on_login_needed(SondClient *client, gpointer user_data) {
    const gchar *message = (const gchar *)user_data;
    
    // Login-Dialog zeigen
    LoginResult *login_result = sond_login_dialog_show(NULL,
                                    sond_client_get_server_url(client),
                                    message);
    
    if (!login_result || !login_result->success) {
        return FALSE;  // Abbruch
    }
    
    // Auth setzen
    sond_client_set_auth(client, login_result->username, 
                         login_result->session_token);
    
    login_result_free(login_result);
    return TRUE;  // Erfolg
}
```

**Vereinfachter Auth-Failed-Callback:**
```c
static void on_auth_failed(SondClient *client, gpointer user_data) {
    LOG_ERROR("Auth failed - session expired\n");
    // Nur Logging, kein Re-Login mehr (wird automatisch gemacht)
}
```

## Ablauf-Diagramm

### Beim ersten Server-Request (z.B. create_and_lock_node):

```
┌─────────────────────────────────────┐
│ sond_client_create_and_lock_node()  │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ ensure_authenticated()              │
│ ├─ Token vorhanden?                 │
│ │  └─ NEIN                           │
│ └─ login_callback aufrufen          │
│    └─ on_login_needed()              │
│       └─ Login-Dialog zeigen         │
│          └─ sond_client_set_auth()   │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ HTTP-Request senden                 │
│ (mit Authorization-Header)          │
└──────────────┬──────────────────────┘
               │
               ▼
         [ Erfolg ]
```

### Bei abgelaufenem Token:

```
┌─────────────────────────────────────┐
│ sond_client_lock_node()             │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ HTTP-Request senden                 │
└──────────────┬──────────────────────┘
               │
               ▼
       [ Status 401/403/500 ]
               │
               ▼
┌─────────────────────────────────────┐
│ handle_auth_error()                 │
│ ├─ Token löschen                    │
│ ├─ auth_failed_callback aufrufen    │
│ │  └─ on_auth_failed() (nur Logging)│
│ └─ login_callback aufrufen          │
│    └─ on_login_needed()              │
│       └─ Login-Dialog zeigen         │
│          └─ sond_client_set_auth()   │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ HTTP-Request WIEDERHOLEN            │
│ (mit neuem Token)                   │
└──────────────┬──────────────────────┘
               │
               ▼
         [ Erfolg ]
```

## Installation

### Variante 1: Direkt ersetzen (nach Backup!)

```bash
# Backup erstellen
cd C:\msys64\home\pkrieger\eclipse-workspace\sond\src\sond_client
cp sond_client.c sond_client.c.backup
cp sond_client.h sond_client.h.backup
cp main.c main.c.backup

# Neue Versionen kopieren
cp sond_client_MODIFIED.c sond_client.c
cp sond_client_MODIFIED.h sond_client.h
cp main_MODIFIED.c main.c
```

### Variante 2: Vergleichen und manuell ändern

Verwenden Sie einen Diff-Tool in Eclipse:
1. Rechtsklick auf `sond_client.c` → Compare With → Select from History
2. Vergleichen Sie mit `sond_client_MODIFIED.c`
3. Übernehmen Sie die Änderungen manuell

## Was bleibt gleich?

- `sond_login_dialog.c/h` - KEINE Änderungen nötig
- Die öffentliche API ist abwärtskompatibel
- Bestehender Code, der die Server-Funktionen aufruft, funktioniert weiterhin

## Server-Anpassung (optional, für später)

Der Server sollte bei abgelaufenem Token **401 Unauthorized** statt 500 zurückgeben.
Der Client-Code funktioniert aber mit beiden Varianten.

Beispiel Server-Änderung (in Ihrem Server-Code):

```python
# Alt (falsch):
if not is_token_valid(token):
    return jsonify({"error": "Invalid token"}), 500

# Neu (korrekt):
if not is_token_valid(token):
    return jsonify({"error": "Invalid or expired token"}), 401
```

## Testen

1. **Lazy Login testen:**
   - Client starten
   - Login-Dialog sollte NICHT sofort erscheinen
   - Erste Aktion ausführen (z.B. Node erstellen)
   - JETZT sollte Login-Dialog erscheinen

2. **Re-Login testen:**
   - Client starten und einloggen
   - Server-seitig Token ungültig machen (oder warten bis abgelaufen)
   - Aktion ausführen (z.B. Node sperren)
   - Login-Dialog sollte erscheinen mit Meldung "Session abgelaufen"
   - Nach Re-Login sollte Aktion erfolgreich sein

## Vorteile der Lösung

✅ **Lazy Authentication**: Offline-Nutzung möglich (Login erst bei Server-Bedarf)
✅ **Automatisches Re-Login**: Benutzer wird nicht rausgeworfen bei abgelaufenem Token
✅ **Transparent**: Bestehender Code funktioniert ohne Änderungen
✅ **Robust**: Funktioniert auch wenn Server 500 statt 401 zurückgibt
✅ **Einfach erweiterbar**: Weitere Server-Funktionen können gleich implementiert werden

## Fragen?

Falls Probleme auftreten oder Sie weitere Anpassungen brauchen, lassen Sie es mich wissen!
