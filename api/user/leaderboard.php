<?php
require_once __DIR__ . '/../core/database.php';
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');


function handleGet($conn) {
    $query = "SELECT 
    u.username, 
    u.livello,
    COUNT(*) as partite_totali,
    SUM(p.outcome = 'vittoria') as vittorie,
    ROUND(AVG(p.outcome = 'vittoria') * 100, 1) as win_rate
    FROM users u
    JOIN partite p ON u.id = p.user_id
    GROUP BY u.id, u.username, u.livello
    HAVING partite_totali >= 5 
    ORDER BY win_rate DESC, vittorie DESC, partite_totali DESC
    LIMIT 10";

    $result = $conn->query($query);
    if (!$result) ApiHelper::serverError("Errore leaderboard: " . $conn->error);

    $data = $result->fetch_all(MYSQLI_ASSOC);
    
    echo json_encode([
        "success" => true, 
        "data" => ApiHelper::sanitize($data)
    ]);
}

$conn = Database::getInstance()->getConnection();

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    handleGet($conn);
} else {
    http_response_code(405);
}