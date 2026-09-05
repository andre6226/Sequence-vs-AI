<?php
// api/core/api_helper.php

require_once __DIR__ . '/env.php';
require_once __DIR__ . '/jwt_helper.php';

// --- Configurazione di sicurezza applicata a ogni risposta delle API ---

// In produzione gli errori PHP finiscono nei log, non nel corpo della risposta
ini_set('display_errors', Env::isProduction() ? '0' : '1');
ini_set('log_errors', '1');

// Da PHP 8.1 mysqli segnala gli errori con un'eccezione: senza questa rete di
// sicurezza un errore SQL diventerebbe un fatal error, con tanto di query e
// percorsi dei file nella risposta.
set_exception_handler(function ($e) {
    error_log('Eccezione non gestita: ' . $e->getMessage() . ' in ' . $e->getFile() . ':' . $e->getLine());
    if (!headers_sent()) {
        header('Content-Type: application/json');
        http_response_code(500);
    }
    echo json_encode(["error" => "Errore interno del server"]);
});

if (!headers_sent()) {
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: no-referrer');
    // Le risposte contengono dati personali: niente cache condivisa
    header('Cache-Control: no-store');
}

class ApiHelper {

    public static function abort($msg, $code = 400) {
        if (!headers_sent()) {
            header('Content-Type: application/json');
        }
        http_response_code($code);
        echo json_encode(["error" => $msg]);
        exit;
    }

    // Errore interno: il dettaglio nei log, al client solo un messaggio generico
    public static function serverError($logMessage) {
        error_log($logMessage);
        self::abort("Errore interno del server", 500);
    }

    // Pulisce i dati (versione essenziale)
    public static function sanitize($data) {
        if (is_null($data)) return null;
        if (is_array($data)) {
            foreach ($data as $k => $v) {
                $data[$k] = self::sanitize($v);
            }
            return $data;
        }
        return htmlspecialchars($data ?? '', ENT_QUOTES, 'UTF-8');
    }

    public static function ensureFields($fields, $source) {
        foreach ($fields as $f) {
            if (!isset($source[$f])) {
                self::abort("Campo mancante o vuoto: $f");
            }
            // Un campo inviato come array (es. nome[]=x) romperebbe le funzioni
            // che si aspettano una stringa: lo rifiutiamo subito.
            if (!is_scalar($source[$f])) {
                self::abort("Formato non valido per il campo: $f");
            }
            if (trim((string) $source[$f]) === '') {
                self::abort("Campo mancante o vuoto: $f");
            }
        }
    }

    public static function maxLength($value, $max, $label) {
        if (mb_strlen((string) $value) > $max) {
            self::abort("Il campo $label supera i $max caratteri");
        }
    }

    public static function validatePassword($password) {
        if (strlen($password) < 8) {
            self::abort("La password deve essere lunga almeno 8 caratteri");
        }
        // password_hash con BCRYPT tronca oltre i 72 byte: meglio rifiutare
        if (strlen($password) > 72) {
            self::abort("La password non puo' superare i 72 caratteri");
        }
        if (!preg_match('/[A-Za-z]/', $password) ||
            !preg_match('/[0-9]/', $password) ||
            !preg_match('/[^A-Za-z0-9]/', $password)) {
            self::abort("La password deve contenere almeno una lettera, un numero e un carattere speciale");
        }
    }

    public static function validateEmail($email) {
        if (!filter_var($email, FILTER_VALIDATE_EMAIL) || strlen($email) > 100) {
            self::abort("Email non valida");
        }
    }

    // Verifica il token e restituisce l'id utente, oppure blocca con 401.
    public static function requireAuth() {
        $userId = JwtHelper::getUserId();
        if (!$userId) self::abort("Non autorizzato", 401);
        return (int) $userId;
    }

    // Difesa CSRF: il browser invia sempre l'header Origin sulle richieste
    // POST, anche same-origin. Se arriva da un altro sito, rifiutiamo.
    public static function assertSameOrigin() {
        $origin = $_SERVER['HTTP_ORIGIN'] ?? null;
        if ($origin === null || $origin === '') return; // niente Origin: nessun confronto possibile

        $host = $_SERVER['HTTP_HOST'] ?? '';
        $parsed = parse_url($origin);
        if (!isset($parsed['host'])) self::abort("Origine non valida", 403);

        $originHost = $parsed['host'];
        if (isset($parsed['port'])) $originHost .= ':' . $parsed['port'];

        if (!hash_equals($host, $originHost)) {
            self::abort("Richiesta cross-origin non consentita", 403);
        }
    }

    // Limitatore di tentativi su file, per rallentare gli attacchi a forza bruta.
    // In caso di problemi sul filesystem non blocca l'utente legittimo.
    public static function throttle($key, $maxAttempts = 10, $windowSeconds = 900) {
        $file = self::throttleFile($key);
        $now = time();
        $state = ['count' => 0, 'start' => $now];

        $raw = @file_get_contents($file);
        if ($raw !== false) {
            $decoded = json_decode($raw, true);
            if (is_array($decoded) && isset($decoded['count'], $decoded['start'])) {
                $state = $decoded;
            }
        }

        // Finestra scaduta: si riparte da zero
        if ($now - $state['start'] > $windowSeconds) {
            $state = ['count' => 0, 'start' => $now];
        }

        if ($state['count'] >= $maxAttempts) {
            self::abort("Troppi tentativi. Riprova tra qualche minuto.", 429);
        }

        $state['count']++;
        @file_put_contents($file, json_encode($state), LOCK_EX);
    }

    public static function throttleClear($key) {
        @unlink(self::throttleFile($key));
    }

    private static function throttleFile($key) {
        return sys_get_temp_dir() . '/sequence_throttle_' . sha1($key . '|' . ($_SERVER['REMOTE_ADDR'] ?? ''));
    }
}
