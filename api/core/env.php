<?php
// api/core/env.php
// Caricatore minimale per il file .env: nessuna dipendenza esterna.
// Le variabili d'ambiente reali (es. quelle iniettate dal container) hanno
// sempre la precedenza sul file, cosi' in produzione il .env puo' non esistere.

class Env {
    private static $vars = null;

    private static function load() {
        if (self::$vars !== null) return;
        self::$vars = [];

        // Posizioni possibili del .env, in ordine di preferenza:
        //  1. fuori dalla document root (hosting tipo public_html): il file
        //     non e' raggiungibile dal web nemmeno se .htaccess non funziona
        //  2. nella document root del progetto (setup Docker di sviluppo)
        $candidati = [
            __DIR__ . '/../../../.env',
            __DIR__ . '/../../.env',
        ];
        $path = null;
        foreach ($candidati as $c) {
            if (is_readable($c)) { $path = $c; break; }
        }
        if ($path === null) return;

        foreach (file($path, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $line) {
            $line = trim($line);
            if ($line === '' || $line[0] === '#') continue;

            $pos = strpos($line, '=');
            if ($pos === false) continue;

            $key = trim(substr($line, 0, $pos));
            $val = trim(substr($line, $pos + 1));

            // Rimuove eventuali apici attorno al valore
            $len = strlen($val);
            if ($len >= 2 && ($val[0] === '"' || $val[0] === "'") && $val[$len - 1] === $val[0]) {
                $val = substr($val, 1, -1);
            }
            self::$vars[$key] = $val;
        }
    }

    public static function get($key, $default = null) {
        self::load();

        $fromEnv = getenv($key);
        if ($fromEnv !== false && $fromEnv !== '') return $fromEnv;

        if (isset(self::$vars[$key]) && self::$vars[$key] !== '') return self::$vars[$key];

        return $default;
    }

    // Per i valori senza i quali l'applicazione non puo' funzionare in sicurezza.
    public static function require_key($key) {
        $val = self::get($key);
        if ($val === null) {
            error_log("Configurazione mancante: $key (controlla il file .env)");
            http_response_code(500);
            echo json_encode(["error" => "Errore di configurazione del server"]);
            exit;
        }
        return $val;
    }

    public static function isProduction() {
        return strtolower(self::get('APP_ENV', 'production')) === 'production';
    }
}
