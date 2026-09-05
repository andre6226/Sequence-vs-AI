<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');
function handlePost($conn, $userId) {
    ApiHelper::assertSameOrigin();
    ApiHelper::ensureFields(['outcome'], $_POST);

    $outcome = $_POST['outcome'];
    if (!in_array($outcome, ['vittoria', 'sconfitta'])) {
        ApiHelper::abort("Esito non valido. Valori ammessi: 'vittoria', 'sconfitta'");
    }
    $stmt = $conn->prepare("INSERT INTO partite (user_id, outcome) VALUES (?, ?)");
    $stmt->bind_param("is", $userId, $outcome);
    
    // Il dettaglio dell'errore SQL resta nei log, non torna al client
    if (!$stmt->execute()) ApiHelper::serverError("Errore salvataggio partita: " . $stmt->error);

    echo json_encode(["success" => true, "message" => "Partita registrata"]);
}

$userId = ApiHelper::requireAuth();
$conn = Database::getInstance()->getConnection();

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    handlePost($conn, $userId);
} else {
    http_response_code(405);
}
?>