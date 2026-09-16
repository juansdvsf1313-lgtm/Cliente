# -*- coding: utf-8 -*-
r"""Cifra (o descifra / verifica) los ficheros de un cliente ya EMPAQUETADO, con
el mismo algoritmo que ResourceManager::encrypt/decrypt del exe (desplazamiento
por bytes con la contrasena, mas una cabecera en claro que marca el fichero
como cifrado). El exe lee igual ficheros cifrados y en claro, asi que el arbol
de desarrollo NO se cifra nunca: solo la copia que se distribuye.

  python _cifrar_cliente.py --raiz D:\TheOne_cliente_pruebas --desde-config
  python _cifrar_cliente.py --raiz /opt/client-stage --clave ... --cabecera ...
  python _cifrar_cliente.py --raiz ... --verificar          # cuenta cifrados/claros
  python _cifrar_cliente.py --raiz ... --descifrar          # vuelta atras
  python _cifrar_cliente.py --autotest                      # ida y vuelta en memoria

Solo se cifran las extensiones de la lista INCLUIDAS (todas se leen por rutas
que descifran). Fuera quedan: .ogg (se transmiten a pelo por OpenAL), .txt/.md/
LICENSE (los lee el jugador), otclient.exe, y cualquier fichero que ya lleve la
cabecera (no se cifra dos veces).
"""
import argparse, os, re, sys

INCLUIDAS = {'.lua', '.otui', '.otmod', '.otml', '.otfont', '.otps', '.frag', '.vert',
             '.html', '.css', '.json', '.dat', '.lzma', '.png', '.jpg', '.ttf', '.ini'}
CARPETAS_FUERA = {'.git', 'build', 'src', 'cmake', 'browser_client', 'android', 'ios'}

try:
    import numpy as np
except ImportError:  # el bucle en Python puro vale para ficheros pequenos
    np = None


def desplazar(datos: bytes, clave: bytes, cifrar: bool, usar_numpy: bool = True) -> bytes:
    """Equivale byte a byte a ResourceManager::encrypt (cifrar=True) / decrypt.
    encrypt: i par -> c + p[j] - i ; i impar -> c - p[j] + i   (mod 256)
    decrypt: i par -> c - p[j] + i ; i impar -> c + p[j] - i"""
    n = len(datos)
    if n == 0:
        return datos
    plen = len(clave)
    if usar_numpy and np is not None and n > 4096:
        c = np.frombuffer(datos, dtype=np.uint8).astype(np.int64)
        i = np.arange(n, dtype=np.int64)
        p = np.frombuffer(clave, dtype=np.uint8).astype(np.int64)[i % plen]
        par = (i % 2) == 0
        if cifrar:
            r = np.where(par, c + p - i, c - p + i)
        else:
            r = np.where(par, c - p + i, c + p - i)
        return (r & 0xFF).astype(np.uint8).tobytes()
    salida = bytearray(n)
    j = 0
    for i in range(n):
        c = datos[i]
        p = clave[j]
        if cifrar:
            r = c + p - i if i % 2 == 0 else c - p + i
        else:
            r = c - p + i if i % 2 == 0 else c + p - i
        salida[i] = r & 0xFF
        j += 1
        if j >= plen:
            j = 0
    return bytes(salida)


def leer_config(ruta_config: str):
    s = open(ruta_config, encoding='utf-8', errors='replace').read()
    # Anclado al principio de linea: config.h lleva un comentario de EJEMPLO con
    # AY_OBFUSCATE("21UsO5...") antes del #define real, y sin el ancla se cogia ese.
    clave = re.search(r'^#define ENCRYPTION_PASSWORD AY_OBFUSCATE\("([^"]+)"\)', s, re.M)
    cab = re.search(r'^#define ENCRYPTION_HEADER AY_OBFUSCATE\("([^"]+)"\)', s, re.M)
    act = re.search(r'^#define ENABLE_ENCRYPTION (\d)', s, re.M)
    if not clave or not cab:
        sys.exit("no encuentro ENCRYPTION_PASSWORD / ENCRYPTION_HEADER en " + ruta_config)
    if act and act.group(1) != '1':
        print("AVISO: ENABLE_ENCRYPTION no es 1 en config.h: el exe compilado asi NO descifra")
    return clave.group(1), cab.group(1)


def recorrer(raiz: str):
    for carpeta, subdirs, ficheros in os.walk(raiz):
        subdirs[:] = [d for d in subdirs if d not in CARPETAS_FUERA]
        for f in ficheros:
            yield os.path.join(carpeta, f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--raiz', help='carpeta del cliente empaquetado (se modifica EN SITIO)')
    ap.add_argument('--clave')
    ap.add_argument('--cabecera')
    ap.add_argument('--desde-config', metavar='RUTA', nargs='?', const='src/framework/config.h',
                    help='leer clave y cabecera de config.h (por defecto src/framework/config.h)')
    ap.add_argument('--descifrar', action='store_true')
    ap.add_argument('--verificar', action='store_true', help='solo contar, sin tocar nada')
    ap.add_argument('--simular', action='store_true', help='listar lo que se haria')
    ap.add_argument('--autotest', action='store_true')
    a = ap.parse_args()

    if a.autotest:
        import os as _os
        clave = b'Ab9' * 40
        for tam in (0, 1, 2, 3, 100, 4095, 4097, 70000):
            datos = _os.urandom(tam)
            assert desplazar(desplazar(datos, clave, True), clave, False) == datos, tam
            # el vectorizado y el bucle puro tienen que coincidir
            if np is not None and tam > 4096:
                assert desplazar(datos, clave, True) == desplazar(datos, clave, True, usar_numpy=False), "numpy y bucle puro difieren"
        print("autotest OK")
        return

    if a.desde_config:
        clave, cabecera = leer_config(a.desde_config)
    else:
        clave, cabecera = a.clave, a.cabecera
    if not a.raiz or not clave or not cabecera:
        sys.exit("faltan --raiz y (--clave y --cabecera, o --desde-config)")
    if not re.fullmatch(r'[A-Za-z0-9]+', cabecera):
        sys.exit("la cabecera solo admite letras y numeros")

    kb = clave.encode('ascii')
    hb = cabecera.encode('ascii')
    raiz = os.path.abspath(a.raiz)
    if os.path.isfile(os.path.join(raiz, 'src', 'framework', 'config.h')) and not a.verificar:
        sys.exit("ESO ES EL ARBOL DE DESARROLLO (tiene src/framework/config.h): no se cifra nunca. Usa una copia empaquetada.")

    cifrados = claros = tocados = saltados = 0
    bytes_tocados = 0
    for ruta in recorrer(raiz):
        ext = os.path.splitext(ruta)[1].lower()
        if ext not in INCLUIDAS:
            saltados += 1
            continue
        with open(ruta, 'rb') as fh:
            datos = fh.read()
        ya = datos.startswith(hb)
        if ya:
            cifrados += 1
        else:
            claros += 1
        if a.verificar:
            continue
        if a.descifrar:
            if not ya:
                continue
            nuevo = desplazar(datos[len(hb):], kb, False)
        else:
            if ya:
                continue
            nuevo = hb + desplazar(datos, kb, True)
        if a.simular:
            print(("descifrar " if a.descifrar else "cifrar    ") + os.path.relpath(ruta, raiz))
        else:
            tmp = ruta + '.tmp_cifrado'
            with open(tmp, 'wb') as fh:
                fh.write(nuevo)
            os.replace(tmp, ruta)
        tocados += 1
        bytes_tocados += len(datos)

    print(f"raiz: {raiz}")
    print(f"ficheros con extension incluida: {cifrados + claros}  (ya cifrados {cifrados}, en claro {claros}); fuera de la lista: {saltados}")
    if not a.verificar:
        print(f"{'descifrados' if a.descifrar else 'cifrados'} ahora: {tocados}  ({bytes_tocados / 1048576:.1f} MB){'  [simulado]' if a.simular else ''}")


if __name__ == '__main__':
    main()
