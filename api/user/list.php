<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

// L'elenco degli utenti serve solo alla chat: niente token, niente elenco
$userId = ApiHelper::requireAuth();
$conn = Database::getInstance()->getConnection();

$stmt = $conn->prepare("SELECT id, username FROM users WHERE id != ? ORDER BY username ASC");
$stmt->bind_param("i", $userId);
$stmt->execute();

$result = $stmt->get_result();
$data = $result->fetch_all(MYSQLI_ASSOC);
echo json_encode([
    "success" => true,
    "data" => ApiHelper::sanitize($data)
]);
?>
