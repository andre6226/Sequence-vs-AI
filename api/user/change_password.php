<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');


function handlePost($conn, $userId) {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['old_password', 'new_password'], $_POST);
    $oldPass = $_POST['old_password'];
    $newPass = $_POST['new_password'];
    ApiHelper::validatePassword(password: $newPass);

    // Limita i tentativi di indovinare la vecchia password
    ApiHelper::throttle('change_password:' . $userId, 10, 900);

    $stmt = $conn->prepare("SELECT password FROM users WHERE id = ?");
    $stmt->bind_param("i", $userId);
    $stmt->execute();
    $result = $stmt->get_result()->fetch_assoc();

    if (!$result) ApiHelper::abort("Utente non trovato", 404);

    if (!password_verify($oldPass, $result['password'])) {
        ApiHelper::abort("La vecchia password non e' corretta", 401);
    }

    ApiHelper::throttleClear('change_password:' . $userId);

    $newHash = password_hash($newPass, PASSWORD_BCRYPT);

    $updateStmt = $conn->prepare("UPDATE users SET password = ? WHERE id = ?");
    $updateStmt->bind_param("si", $newHash, $userId);

    if (!$updateStmt->execute()) ApiHelper::serverError("Errore cambio password: " . $updateStmt->error);

    echo json_encode(["success" => true, "message" => "Password aggiornata con successo"]);
}


// Senza un token valido non si arriva alla query
$userId = ApiHelper::requireAuth();
$conn = Database::getInstance()->getConnection();

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    handlePost($conn, $userId);
} else {
    http_response_code(405);
}
