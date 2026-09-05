<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

function createUser($conn, $data) {
    $hash = password_hash($data['password'], PASSWORD_BCRYPT);
    $stmt = $conn->prepare("INSERT INTO users (username, email, password, nome, cognome, livello) VALUES (?, ?, ?, ?, ?, 'Base')");
    $stmt->bind_param("sssss", $data['username'], $data['email'], $hash, $data['nome'], $data['cognome']);

    // mysqli lancia un'eccezione in caso di errore (comportamento PHP 8.1+)
    try {
        $stmt->execute();
    } catch (mysqli_sql_exception $e) {
        // 1062 = chiave duplicata: username o email gia' presenti
        if ($e->getCode() === 1062) {
            ApiHelper::abort("Username o email gia' in uso", 409);
        }
        throw $e;
    }
    return $stmt->insert_id;
}


function handleRegisterPost() {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['email', 'password', 'confirm_password', 'nome', 'cognome', 'username'], $_POST);
    $input = $_POST;

    // Limite alle registrazioni per IP: 20 ogni ora
    ApiHelper::throttle('register', 20, 3600);

    ApiHelper::validateEmail($input['email']);
    ApiHelper::validatePassword(password: $input['password']);

    if ($input['password'] !== $input['confirm_password']) {
        ApiHelper::abort("Le password non coincidono");
    }

    // Lunghezze coerenti con le colonne della tabella users
    ApiHelper::maxLength($input['username'], 50, 'username');
    ApiHelper::maxLength($input['nome'], 50, 'nome');
    ApiHelper::maxLength($input['cognome'], 50, 'cognome');

    if (!preg_match('/^[A-Za-z0-9_.-]{3,50}$/', $input['username'])) {
        ApiHelper::abort("Username non valido: da 3 a 50 caratteri fra lettere, numeri, punto, trattino e underscore");
    }

    $conn = Database::getInstance()->getConnection();

    try{
        $newId = createUser($conn, $input);
        $payload = ['sub' => $newId, 'username' => $input['username']];
        $token = JwtHelper::generate($payload);

        JwtHelper::setAuthCookie($token);

        echo json_encode(["success" => true, "user" => ["id" => $newId, "username" => $input['username'], "livello" => "Base"]]);
    } catch (Exception $e) {
        error_log("Errore nella creazione utente: " . $e->getMessage());
        ApiHelper::abort("Errore nella creazione dell'account", 500);
    }
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') handleRegisterPost();
else http_response_code(405);
