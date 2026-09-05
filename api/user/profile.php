<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

const LIVELLI_AMMESSI = ['Base', 'Intermedio', 'Esperto'];

function fetchUser($conn, $id) {
    $stmt = $conn->prepare("SELECT id, username, email, nome, cognome, citta, bio, livello FROM users WHERE id = ?");
    $stmt->bind_param("i", $id);
    $stmt->execute();
    return $stmt->get_result()->fetch_assoc() ?: ApiHelper::abort("Utente non trovato", 404);
}

function handleGet($conn, $userId) {
    $userdata = fetchUser($conn, $userId);
    echo json_encode(["success" => true, "data" => ApiHelper::sanitize($userdata)]);
}

function handlePut($conn, $userId, $PUT_DATA) {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['nome', 'cognome', 'citta', 'bio', 'livello'], $PUT_DATA);

    // Il livello e' una ENUM sul database: accetto solo i valori previsti
    if (!in_array($PUT_DATA['livello'], LIVELLI_AMMESSI, true)) {
        ApiHelper::abort("Livello non valido");
    }

    // Lunghezze coerenti con le colonne della tabella users
    ApiHelper::maxLength($PUT_DATA['nome'], 50, 'nome');
    ApiHelper::maxLength($PUT_DATA['cognome'], 50, 'cognome');
    ApiHelper::maxLength($PUT_DATA['citta'], 100, 'citta');
    ApiHelper::maxLength($PUT_DATA['bio'], 1000, 'bio');

    $stmt = $conn->prepare("UPDATE users SET nome=?, cognome=?, citta=?, bio=?, livello=? WHERE id=?");

    $stmt->bind_param("sssssi", $PUT_DATA['nome'], $PUT_DATA['cognome'], $PUT_DATA['citta'], $PUT_DATA['bio'], $PUT_DATA['livello'], $userId);
    // Il dettaglio dell'errore SQL resta nei log, non torna al client
    if (!$stmt->execute()) ApiHelper::serverError("Errore aggiornamento profilo: " . $stmt->error);

    $userdata = fetchUser($conn, $userId);
    echo json_encode(["success" => true, "message" => "Profilo aggiornato", "data" => ApiHelper::sanitize($userdata)]);
}


$userId = ApiHelper::requireAuth();

$conn = Database::getInstance()->getConnection();
$method = $_SERVER['REQUEST_METHOD'];

if ($method === 'GET') {
    handleGet($conn, $userId);
}
else if ($method === 'POST') {
    parse_str(file_get_contents("php://input"), $PUT_DATA);
    handlePut($conn, $userId, $PUT_DATA);
}
else {
    http_response_code(405);
}
