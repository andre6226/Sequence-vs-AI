<?php
require_once __DIR__ . '/env.php';

class JwtHelper {
    const COOKIE_NAME = 'sequence_token';
    const ISSUER = 'gamesaw_backend';

    // La chiave di firma arriva dal .env: se manca, l'applicazione si ferma
    // invece di ripiegare su un valore prevedibile.
    private static function secret() {
        return Env::require_key('JWT_SECRET');
    }

    private static function ttl() {
        return (int) Env::get('JWT_TTL', 3600);
    }

    public static function generate($payload) {
        $header = json_encode(['typ' => 'JWT', 'alg' => 'HS256']);
        $payload['iss'] = self::ISSUER;
        $payload['iat'] = time();
        $payload['exp'] = time() + self::ttl();

        $base64UrlHeader = self::base64UrlEncode($header);
        $base64UrlPayload = self::base64UrlEncode(json_encode($payload));

        $signature = hash_hmac('sha256', $base64UrlHeader . "." . $base64UrlPayload, self::secret(), true);
        $base64UrlSignature = self::base64UrlEncode($signature);

        return $base64UrlHeader . "." . $base64UrlPayload . "." . $base64UrlSignature;
    }

    public static function validate($token) {
        if (!is_string($token) || $token === '') return false;

        $parts = explode('.', $token);
        if (count($parts) !== 3) return false;

        [$header, $payload, $signature] = $parts;

        $validSignature = hash_hmac('sha256', $header . "." . $payload, self::secret(), true);
        if (!hash_equals(self::base64UrlEncode($validSignature), $signature)) {
            return false;
        }

        $data = json_decode(self::base64UrlDecode($payload));
        // Un payload malformato non deve mai arrivare ai controlli successivi
        if (!is_object($data)) return false;

        if (!isset($data->exp) || !isset($data->iat) || !isset($data->iss)) return false;
        if ($data->iss !== self::ISSUER) return false;
        if ($data->exp < time()) return false;
        // Token datato nel futuro: firma valida ma orologio manomesso
        if ($data->iat > time() + 60) return false;

        return $data;
    }

    public static function getBearerToken() {
        if (isset($_COOKIE[self::COOKIE_NAME])) {
            return $_COOKIE[self::COOKIE_NAME];
        }
        return null;
    }

    public static function getUserId() {
        $token = self::getBearerToken();
        if (!$token) return null;
        $payload = self::validate($token);
        if ($payload === false) return null;
        return $payload->sub ?? $payload->id ?? null;
    }

    // Opzioni del cookie in un unico punto, cosi' login/register/logout
    // non possono divergere fra loro.
    private static function cookieOptions($expires) {
        return [
            'expires'  => $expires,
            'path'     => '/',
            'secure'   => true,
            'httponly' => true,
            'samesite' => 'Lax'
        ];
    }

    public static function setAuthCookie($token) {
        setcookie(self::COOKIE_NAME, $token, self::cookieOptions(time() + self::ttl()));
    }

    public static function clearAuthCookie() {
        setcookie(self::COOKIE_NAME, "", self::cookieOptions(time() - 3600));
    }

    private static function base64UrlEncode($data) {
        return rtrim(strtr(base64_encode($data), '+/', '-_'), '=');
    }

    private static function base64UrlDecode($data) {
        return base64_decode(strtr($data, '-_', '+/'));
    }
}
