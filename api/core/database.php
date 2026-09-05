<?php
require_once __DIR__ . '/env.php';

class Database {
    private static $instance = null;
    private $connection;

    private function __construct() {
        // Credenziali dal file .env / variabili d'ambiente, mai hardcoded
        $host     = Env::require_key('DB_HOST');
        $db_name  = Env::require_key('DB_NAME');
        $username = Env::require_key('DB_USER');
        $password = Env::require_key('DB_PASS');

        // Silenzia il warning nativo: l'errore lo gestiamo noi senza esporlo
        $this->connection = @new mysqli($host, $username, $password, $db_name);

        if ($this->connection->connect_error) {
            // Il dettaglio finisce nei log del server, al client solo un messaggio generico
            error_log("Errore di connessione al Database: " . $this->connection->connect_error);
            http_response_code(500);
            echo json_encode(["error" => "Servizio momentaneamente non disponibile"]);
            exit;
        }
        $this->connection->set_charset("utf8mb4");
    }

    public static function getInstance() {
        if (self::$instance === null) {
            self::$instance = new Database();
        }
        return self::$instance;
    }

    public function getConnection() {
        return $this->connection;
    }

}
?>
