<?php
declare(strict_types=1);

// Updater del cliente TheOne 15.x
// El cliente (modules/updater) hace POST con {version, build, os, platform,
// things, hd} y espera {url, files:{ruta:checksum}, binary:{file,checksum}}.
// El checksum debe ser identico al de g_crypt.crc32(datos, false):
// dec_to_hex en minusculas SIN ceros a la izquierda. En PHP eso es
// dechex(crc32($d)); hash(crc32b) NO sirve porque rellena a 8 digitos.
// Las rutas llevan barra inicial y la url base no.

header("Content-Type: application/json; charset=utf-8");
header("X-Content-Type-Options: nosniff");

$CLIENT_DIR = "/var/www/canary1530/client";
$BASE_URL   = "http://162.35.181.86:8080/client";
$BINARY     = "/otclient.exe";
$CACHE_FILE = sys_get_temp_dir() . "/canary1530-updater-crc.json";

// data/things solo se manda si el cliente lo pide:
//  - things=1: sprites SD (data/things/1525). El updater viejo sacaba sus
//    checksums de filesChecksums(), que se salta data/things, y si le llegaran
//    los bajaria todos en cada arranque; el nuevo (desde 2026-09-13) compara
//    leyendo solo la lista que recibe.
//  - hd=1: pack HD (data/things/1525_hd, enlace a /opt/client-hd-stage). Lo
//    pide el cliente que tiene HD activado o ya instalado: la primera vez baja
//    los ~5.100 ficheros (673 MB) y despues solo lo que cambie. Desde
//    2026-09-15 es la unica via del HD (antes iba en un ZIP aparte).
$peticion = json_decode((string)file_get_contents("php://input"), true);
$conThings = is_array($peticion) && !empty($peticion["things"]);
$conHd     = is_array($peticion) && !empty($peticion["hd"]);

function excluded(string $rel, bool $conThings, bool $conHd): bool {
    if (str_starts_with($rel, "/data/things/")) {
        $sd = $conThings && str_starts_with($rel, "/data/things/1525/");
        $hd = $conHd && str_starts_with($rel, "/data/things/1525_hd/");
        if (!$sd && !$hd) return true;
        // marcadores que escribe el cliente en su carpeta, y restos del stage
        if (str_ends_with($rel, "/.hd-version") || str_ends_with($rel, "/HD_COMPLETE")) return true;
    }
    foreach ([".log", ".pdb", ".ilk", ".tmp"] as $s) {
        if (str_ends_with($rel, $s)) return true;
    }
    if (str_contains($rel, ".bak")) return true;
    if (str_contains($rel, "/.git/")) return true;
    if (str_contains($rel, "/cache/")) return true;
    if (str_ends_with($rel, "/.client-assets-complete")) return true;
    return false;
}

if (!is_dir($CLIENT_DIR)) {
    echo json_encode(["error" => "El servidor de actualizaciones no esta listo."]);
    exit;
}

// CRC por ruta, tamano y fecha de modificacion: sin esto cada arranque de cada
// jugador releia del disco del VPS cientos de MB para calcular lo mismo.
$cache = [];
if (is_file($CACHE_FILE)) {
    $leida = json_decode((string)@file_get_contents($CACHE_FILE), true);
    if (is_array($leida)) $cache = $leida;
}
$cacheCambiada = false;

$files = [];
$hdFiles = [];
$binaryChecksum = null;
// FOLLOW_SYMLINKS: data/things/1525_hd es un enlace al stage del HD.
$it = new RecursiveIteratorIterator(
    new RecursiveDirectoryIterator($CLIENT_DIR, FilesystemIterator::SKIP_DOTS | FilesystemIterator::FOLLOW_SYMLINKS),
    RecursiveIteratorIterator::SELF_FIRST
);

foreach ($it as $item) {
    if (!$item->isFile()) continue;
    $rel = substr($item->getPathname(), strlen($CLIENT_DIR));
    if ($rel === "") continue;
    if (excluded($rel, $conThings, $conHd)) continue;

    $size = $item->getSize();
    $mtime = $item->getMTime();
    $guardado = $cache[$rel] ?? null;
    if (is_array($guardado) && $guardado[0] === $size && $guardado[1] === $mtime) {
        $sum = $guardado[2];
    } else {
        $data = @file_get_contents($item->getPathname());
        if ($data === false) continue;
        $sum = dechex(crc32($data));
        $cache[$rel] = [$size, $mtime, $sum];
        $cacheCambiada = true;
    }

    if ($rel === $BINARY) { $binaryChecksum = $sum; continue; }
    $files[$rel] = $sum;
    if ($conHd && str_starts_with($rel, "/data/things/1525_hd/")) $hdFiles[$rel] = $sum;
}

if ($cacheCambiada) {
    $tmp = $CACHE_FILE . "." . getmypid();
    if (@file_put_contents($tmp, json_encode($cache, JSON_UNESCAPED_SLASHES)) !== false) {
        @rename($tmp, $CACHE_FILE);
    }
}

$response = [
    "url"       => $BASE_URL,
    "files"     => $files,
    "keepFiles" => false,
];

if ($binaryChecksum !== null) {
    $response["binary"] = ["file" => $BINARY, "checksum" => $binaryChecksum];
}

// Resumen del pack HD: un CRC de todos sus CRC. El cliente lo guarda y, mientras
// no cambie, se salta la comprobacion de los ~5.100 ficheros (673 MB de lectura
// en cada arranque, casi un minuto en un disco mecanico).
if ($conHd && $hdFiles) {
    ksort($hdFiles);
    $acc = "";
    foreach ($hdFiles as $r => $s) { $acc .= $r . ":" . $s . "
"; }
    $response["hdSum"] = dechex(crc32($acc));
}

echo json_encode($response, JSON_UNESCAPED_SLASHES);
