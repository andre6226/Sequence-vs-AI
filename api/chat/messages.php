<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

// 1. Auth: Se non sei loggato, blocco subito tutto.
$userId = ApiHelper::requireAuth();

$conn = Database::getInstance()->getConnection();

// --- FUNZIONI DI GESTIONE ---

function handleGet($conn, $userId) {
    // se manca partner_id, ritorna array vuoto
    if (!isset($_GET['partner_id'])) {
        echo json_encode(["success" => true, "data" => []]);
        return;
    }

    $partnerId = (int)$_GET['partner_id'];
    $query = "SELECT m.messaggio, m.data_invio, m.mittente_id, u.username as mittente_nome
              FROM messaggi m
              JOIN users u ON m.mittente_id = u.id
              WHERE (m.mittente_id = ? AND m.destinatario_id = ?) 
                 OR (m.mittente_id = ? AND m.destinatario_id = ?)
              ORDER BY m.data_invio ASC";

    $stmt = $conn->prepare($query);
    $stmt->bind_param("iiii", $userId, $partnerId, $partnerId, $userId);
    $stmt->execute();
    
    $raw_data = $stmt->get_result()->fetch_all(MYSQLI_ASSOC);
    $data = ApiHelper::sanitize($raw_data);

    //aggiungo il flag 'is_me' DOPO la sanitizzazione 
    foreach ($data as &$row) {
        $row['is_me'] = ($row['mittente_id'] == $userId);
    }

    echo json_encode(["success" => true, "data" => $data]);
}

function destinatarioEsiste($conn, $destId) {
    $stmt = $conn->prepare("SELECT id FROM users WHERE id = ?");
    $stmt->bind_param("i", $destId);
    $stmt->execute();
    return (bool) $stmt->get_result()->fetch_assoc();
}

function handlePost($conn, $userId) {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['destinatario_id', 'messaggio'], $_POST);
    $destId = (int)$_POST['destinatario_id'];
    $msg = $_POST['messaggio'];

    if ($destId === $userId) ApiHelper::abort("Non puoi scriverti da solo");

    // Tetto alla lunghezza: evita di riempire la tabella con un solo invio
    ApiHelper::maxLength($msg, 2000, 'messaggio');

    // Senza questo controllo un id inesistente arriverebbe fino alla foreign key
    if (!destinatarioEsiste($conn, $destId)) ApiHelper::abort("Destinatario non trovato", 404);

    $stmt = $conn->prepare("INSERT INTO messaggi (mittente_id, destinatario_id, messaggio) VALUES (?, ?, ?)");
    $stmt->bind_param("iis", $userId, $destId, $msg);
    
    if ($stmt->execute()) {
        echo json_encode(["success" => true]);
    } else {
        ApiHelper::serverError("Errore invio messaggio: " . $stmt->error);
    }
}

$method = $_SERVER['REQUEST_METHOD'];

if ($method === 'GET') {
    handleGet($conn, $userId);
} elseif ($method === 'POST') {
    handlePost($conn, $userId);
} else {
    ApiHelper::abort("Metodo non consentito", 405);
}