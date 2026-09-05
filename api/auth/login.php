<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

function findUser($conn, $email) {
    $stmt = $conn->prepare("SELECT id, username, password, livello FROM users WHERE email = ?");
    $stmt->bind_param("s", $email);
    $stmt->execute();
    return $stmt->get_result()->fetch_assoc();
}


function handleLogin() {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['email', 'password'], $_POST);
    ApiHelper::validateEmail($_POST['email']);
    $input = $_POST;

    // Massimo 10 tentativi ogni 15 minuti sulla stessa coppia email/IP
    ApiHelper::throttle('login:' . strtolower($input['email']), 10, 900);

    $conn = Database::getInstance()->getConnection();
    $user = findUser($conn, $input['email']);

    if (!$user) {
        // Confronto comunque una password fittizia: senza questo, i tempi di
        // risposta rivelerebbero quali email sono registrate.
        password_verify($input['password'], '$2y$10$usesomesillystringfore7hnbRJHxXVLeakoG8K30M1MlVkd.dK');
        ApiHelper::abort("Credenziali non valide.", 401);
    }

    if (!password_verify($input['password'], $user['password']))
        ApiHelper::abort("Credenziali non valide.", 401);

    // Login riuscito: azzero il contatore dei tentativi
    ApiHelper::throttleClear('login:' . strtolower($input['email']));

    $payload = [
        'sub' => $user['id'],
        'username' => $user['username']
    ];

    $token = JwtHelper::generate($payload);
    JwtHelper::setAuthCookie($token);

    echo json_encode([
        "success" => true,
        "user" => [
            "id" => $user['id'],
            "username" => $user['username'],
            "livello" => $user['livello']
        ]
    ]);
}

// Routing
if ($_SERVER['REQUEST_METHOD'] === 'POST') handleLogin();
else http_response_code(405);
?>
