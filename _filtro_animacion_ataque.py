# -*- coding: utf-8 -*-
"""Opcion de cliente para apagar la animacion de arma de los ataques normales.

En Tibia real la opcion es del cliente (gameWindowShowAttackAnimation): el
servidor manda la animacion siempre y el cliente decide si la dibuja. Se hace
igual aqui, asi cada jugador manda sobre lo suyo sin tocar el protocolo.

Los efectos 304-309 son los seis sprites de golpe (espada, club, hacha, vara,
cruzadas, puno). Si la opcion esta apagada, se descartan al recibirlos; el resto
de efectos no se toca.
"""
import sys, time, shutil
from pathlib import Path

RAIZ = Path(r"D:\Canary_OTClient_src")
GAMEH = RAIZ / "src/client/game.h"
LUAF = RAIZ / "src/framework/luafunctions.cpp"
PARSE = RAIZ / "src/client/protocolgameparse.cpp"
DATAOPT = RAIZ / "modules/client_options/data_options.lua"
OTUI = RAIZ / "modules/client_options/styles/graphics/effects.otui"
SELLO = time.strftime("%Y%m%d_%H%M%S")

fallos = []


def sub(txt, viejo, nuevo, etiqueta):
    n = txt.count(viejo)
    if n != 1:
        fallos.append(f"{etiqueta}: esperaba 1, encontre {n}")
        return txt
    print(f"  OK  {etiqueta}")
    return txt.replace(viejo, nuevo, 1)


g = GAMEH.read_text(encoding="utf-8")
lf = LUAF.read_text(encoding="utf-8")
pa = PARSE.read_text(encoding="utf-8")
do = DATAOPT.read_text(encoding="utf-8")
ot = OTUI.read_text(encoding="utf-8")

print("== game.h ==")
# Se cuelga de Game, que es donde vive lo que depende de la partida.
ancla = "    bool canPerformGameAction() const;"
g = sub(g, ancla,
        """    // Animacion de arma de los ataques normales (efectos 304-309).
    // En el cliente oficial es gameWindowShowAttackAnimation.
    void setShowAttackAnimation(const bool show) { m_showAttackAnimation = show; }
    bool isShowingAttackAnimation() const { return m_showAttackAnimation; }

""" + ancla, "setter y getter en Game")

# El miembro, junto a los demas booleanos de estado.
g = sub(g, "    bool m_online{ false };",
        "    bool m_online{ false };\n    bool m_showAttackAnimation{ true };",
        "miembro m_showAttackAnimation")

print("== luafunctions.cpp ==")
lf = sub(lf, '    g_lua.bindSingletonFunction("g_app", "setDrawEffectOnTop", &GraphicalApplication::setDrawEffectOnTop, &g_app);',
         '    g_lua.bindSingletonFunction("g_app", "setDrawEffectOnTop", &GraphicalApplication::setDrawEffectOnTop, &g_app);\n'
         '    g_lua.bindSingletonFunction("g_game", "setShowAttackAnimation", &Game::setShowAttackAnimation, &g_game);\n'
         '    g_lua.bindSingletonFunction("g_game", "isShowingAttackAnimation", &Game::isShowingAttackAnimation, &g_game);',
         "bindings de Lua")

print("== protocolgameparse.cpp ==")
pa = sub(pa, """                    if (!g_things.isValidDatId(effectId, ThingCategoryEffect)) {
                        g_logger.traceError("invalid effect id {}", effectId);
                        continue;
                    }""",
         """                    if (!g_things.isValidDatId(effectId, ThingCategoryEffect)) {
                        g_logger.traceError("invalid effect id {}", effectId);
                        continue;
                    }

                    // 304-309: los seis sprites de golpe por tipo de arma. Se
                    // descartan si el jugador apago la animacion de ataque.
                    if (effectId >= 304 && effectId <= 309 && !g_game.isShowingAttackAnimation()) {
                        break;
                    }""",
         "filtro de los efectos 304-309")

print("== data_options.lua ==")
do = sub(do, """    drawEffectOnTop                   = {
        value = false,
        action = function(value, options, controller, panels, extraWidgets)
            g_app.setDrawEffectOnTop(value)
        end
    },""",
         """    drawEffectOnTop                   = {
        value = false,
        action = function(value, options, controller, panels, extraWidgets)
            g_app.setDrawEffectOnTop(value)
        end
    },
    showAttackAnimation               = {
        value = true,
        action = function(value, options, controller, panels, extraWidgets)
            g_game.setShowAttackAnimation(value)
        end
    },""", "opcion showAttackAnimation")

print("== effects.otui ==")
ot = sub(ot, """    OptionCheckBoxMarked
      id: drawEffectOnTop
      !text: tr('Draw Effect On Top')
      !tooltip: tr('Draw effect after drawing the entire floor.')""",
         """    OptionCheckBoxMarked
      id: drawEffectOnTop
      !text: tr('Draw Effect On Top')
      !tooltip: tr('Draw effect after drawing the entire floor.')

  SmallReversedQtPanel
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: prev.bottom
    margin-top: 7
    height: 22

    OptionCheckBoxMarked
      id: showAttackAnimation
      !text: tr('Show Attack Animation')
      !tooltip: tr('Draw the weapon animation on melee auto-attacks.')""",
         "casilla en el panel de efectos")

if fallos:
    print("\n!! NO SE ESCRIBIO NADA:")
    for f in fallos:
        print("  -", f)
    sys.exit(1)

for ruta, texto in ((GAMEH, g), (LUAF, lf), (PARSE, pa), (DATAOPT, do), (OTUI, ot)):
    shutil.copy2(ruta, str(ruta) + f".bak_anim_{SELLO}")
    ruta.write_bytes(texto.encode("utf-8"))
print(f"\nEscrito. Respaldos con sello {SELLO}")
