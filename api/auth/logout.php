<?php
require_once __DIR__ . '/../core/jwt_helper.php';
require_once __DIR__ . '/../core/api_helper.php';

header('Content-Type: application/json');

JwtHelper::clearAuthCookie();

echo json_encode(["success" => true, "message" => "Logout effettuato con successo"]);
