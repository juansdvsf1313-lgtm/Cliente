# -*- coding: utf-8 -*-
"""
Extrae iconos de weapon mastery del graphics_resources.rcc del cliente oficial.

El .rcc es un Qt Resource Collection (formato 'qres', version 3). Se parsea el
arbol de nombres y se sacan solo los ficheros pedidos, por nombre.

Hace falta porque el modulo game_proficiency del OTClient pinta tres perks con
offsets (1216, 1280, 1344) que se salen de icons-0.png, que solo mide 1216px.
El cliente oficial no usa esa hoja: guarda un PNG suelto por perk, que es el
mismo patron que el modulo ya usa para icons-weaponmastery-elementalPiercing.

usage:
    _extraer_iconos_rcc.py --listar        ver que hay
    _extraer_iconos_rcc.py                 extraer los weaponmastery
"""
import re
import sys
import zlib
from pathlib import Path

RCC = Path(r"D:\Organizado\Juegos\Tibia\packages\Tibia\bin\graphics_resources.rcc")
DESTINO = Path(r"D:\Canary_OTClient_src\modules\game_proficiency\images")

FLAG_COMPRIMIDO = 0x01
FLAG_DIRECTORIO = 0x02
FLAG_ZSTD = 0x04


def u16(b, o):
    return int.from_bytes(b[o:o + 2], "big")


def u32(b, o):
    return int.from_bytes(b[o:o + 4], "big")


def parsear(datos):
    """Devuelve {ruta: bytes} de todo el .rcc."""
    if datos[:4] != b"qres":
        raise SystemExit("no es un .rcc")
    version = u32(datos, 4)
    off_arbol = u32(datos, 8)
    off_datos = u32(datos, 12)
    off_nombres = u32(datos, 16)

    # v1: 14 bytes por nodo; v2 y v3 anaden la fecha (8 bytes)
    tam_nodo = 14 if version < 2 else 22

    def leer_nombre(off):
        largo = u16(datos, off_nombres + off)
        inicio = off_nombres + off + 6  # 2 de largo + 4 de hash
        return datos[inicio:inicio + largo * 2].decode("utf-16-be", "replace")

    salida = {}

    def recorrer(indice, ruta):
        base = off_arbol + indice * tam_nodo
        off_nombre = u32(datos, base)
        flags = u16(datos, base + 4)
        nombre = leer_nombre(off_nombre)
        ruta_actual = f"{ruta}/{nombre}" if ruta else nombre

        if flags & FLAG_DIRECTORIO:
            n_hijos = u32(datos, base + 6)
            primero = u32(datos, base + 10)
            for i in range(n_hijos):
                recorrer(primero + i, ruta_actual)
            return

        off_blob = u32(datos, base + 10)
        pos = off_datos + off_blob
        largo = u32(datos, pos)
        crudo = datos[pos + 4:pos + 4 + largo]

        if flags & FLAG_ZSTD:
            return  # no se usa aqui; se salta en vez de adivinar
        if flags & FLAG_COMPRIMIDO:
            try:
                crudo = zlib.decompress(crudo[4:])  # los 4 primeros = tamano final
            except zlib.error:
                return
        salida[ruta_actual] = crudo

    # El nodo 0 es la raiz; sus hijos cuelgan de ahi.
    n_hijos = u32(datos, off_arbol + 6)
    primero = u32(datos, off_arbol + 10)
    for i in range(n_hijos):
        recorrer(primero + i, "")
    return salida


def main():
    todo = parsear(RCC.read_bytes())
    pngs = {k: v for k, v in todo.items() if v[:8] == b"\x89PNG\r\n\x1a\n"}
    print(f"ficheros en el rcc: {len(todo)}  (PNG: {len(pngs)})")

    interesa = {k: v for k, v in pngs.items()
                if re.search(r"weaponmastery|masterylevel|proficiency", k, re.I)}

    if "--listar" in sys.argv:
        for k in sorted(interesa):
            print(f"  {len(interesa[k]):>8}  {k}")
        return 0

    DESTINO.mkdir(parents=True, exist_ok=True)
    escritos = 0
    for ruta, blob in sorted(interesa.items()):
        nombre = ruta.rsplit("/", 1)[-1]
        destino = DESTINO / nombre
        if destino.exists() and destino.read_bytes() == blob:
            continue
        destino.write_bytes(blob)
        print(f"  escrito  {nombre}  ({len(blob)} bytes)")
        escritos += 1
    print(f"\n{escritos} iconos nuevos o actualizados en {DESTINO}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
