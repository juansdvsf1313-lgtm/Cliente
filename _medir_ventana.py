# -*- coding: utf-8 -*-
"""
Mide una ventana del cliente y la compara con la del cliente oficial.

Uso:
    python _medir_ventana.py oficial.png nuestra.png
    python _medir_ventana.py --ultima            (coge las 2 capturas mas nuevas de Pictures\\Screenshots)

Recorta la ventana por su marco en las dos imagenes y saca: tamano, bandas
horizontales (titulo, pestanas, secciones), lineas verticales (columnas,
separadores) y donde empieza el texto. Comparar esas listas numero a numero es
lo que permite ajustar sin estimar a ojo.

Requiere pillow y numpy (ya estan en el entorno de la sesion).
"""
import sys
import os
import glob
import numpy as np
from PIL import Image


def recortar_ventana(ruta, zona=(960, 1660, 400, 1000)):
    """Recorta la ventana del juego buscando su marco claro dentro de 'zona'."""
    im = Image.open(ruta).convert('RGB')
    a = np.asarray(im).astype(int)
    x0z, x1z, y0z, y1z = zona
    x1z = min(x1z, a.shape[1])
    y1z = min(y1z, a.shape[0])
    sub = a[y0z:y1z, x0z:x1z]
    gris = (abs(sub[:, :, 0] - sub[:, :, 1]) < 6) & (abs(sub[:, :, 1] - sub[:, :, 2]) < 6)
    claro = gris & (sub[:, :, 0] > 105) & (sub[:, :, 0] < 140)
    fy = np.where(claro.sum(axis=1) > 380)[0]
    fx = np.where(claro.sum(axis=0) > 300)[0]
    if len(fy) < 2 or len(fx) < 2:
        return None
    y0, y1, x0, x1 = fy.min(), fy.max(), fx.min(), fx.max()
    return im.crop((x0z + x0, y0z + y0, x0z + x1 + 1, y0z + y1 + 1))


def bandas_horizontales(a):
    """Lineas horizontales fuertes: separan titulo, pestanas, secciones, pie."""
    h, w, _ = a.shape
    gris = (abs(a[:, :, 0] - a[:, :, 1]) < 8) & (abs(a[:, :, 1] - a[:, :, 2]) < 8)
    claro = gris & (a[:, :, 0] > 95)
    filas = claro[:, 10:w - 10].sum(axis=1)
    lim = (w - 20) * 0.55
    return _agrupar([y for y in range(h) if filas[y] > lim])


def lineas_verticales(a, y0, y1, umbral=0.55):
    """Lineas verticales fuertes entre y0 e y1: bordes de columna y separadores."""
    h, w, _ = a.shape
    y1 = min(y1, h)
    fr = a[y0:y1, :, :]
    gris = (abs(fr[:, :, 0] - fr[:, :, 1]) < 8) & (abs(fr[:, :, 1] - fr[:, :, 2]) < 8)
    claro = gris & (fr[:, :, 0] > 95)
    cols = claro.sum(axis=0)
    lim = (y1 - y0) * umbral
    return _agrupar([x for x in range(w) if cols[x] > lim])


def inicio_texto(a, y0, y1, x0, x1, brillo=110):
    """Primera columna con pixeles de texto, para medir margenes."""
    fr = a[y0:y1, x0:x1, :].mean(axis=2)
    xs = [i for i in range(x1 - x0) if (fr[:, i] > brillo).any()]
    return x0 + min(xs) if xs else None


def _agrupar(valores, hueco=3):
    grupos, ini, prev = [], None, None
    for v in valores:
        if ini is None:
            ini = v
        elif v - prev > hueco:
            grupos.append(ini)
            ini = v
        prev = v
    if ini is not None:
        grupos.append(ini)
    return grupos


def informe(nombre, im):
    a = np.asarray(im.convert('RGB')).astype(int)
    h, w, _ = a.shape
    print('=== %s === %dx%d' % (nombre, w, h))
    print('   bandas horizontales: %s' % bandas_horizontales(a))
    print('   lineas verticales (mitad superior): %s' % lineas_verticales(a, 70, min(h, 350)))
    return a


def main():
    args = sys.argv[1:]
    if args and args[0] == '--ultima':
        d = os.path.expanduser('~') + '/Pictures/Screenshots'
        fs = sorted(glob.glob(d + '/*.png'), key=os.path.getmtime, reverse=True)[:2]
        if len(fs) < 2:
            print('Hacen falta al menos 2 capturas en', d)
            return
        args = [fs[1], fs[0]]
        print('oficial =', os.path.basename(args[0]))
        print('nuestra =', os.path.basename(args[1]))
    if len(args) != 2:
        print(__doc__)
        return

    for etiqueta, ruta in [('OFICIAL', args[0]), ('NUESTRA', args[1])]:
        im = recortar_ventana(ruta)
        if im is None:
            print('%s: no encuentro el marco de la ventana en %s' % (etiqueta, ruta))
            continue
        salida = '_medida_%s.png' % etiqueta.lower()
        im.save(salida)
        informe(etiqueta, im)
        print('   recorte guardado en %s' % salida)
        print()


if __name__ == '__main__':
    main()
