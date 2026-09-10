--[[
    register(id, name, thingId, thingType, config)
    config = {
        speed, disableWalkAnimation, shader, drawOnUI, opacity
        duration, loop, transform, hideOwner, followOwner, size{width, height}
        offset{x, y, onTop}, dirOffset[dir]{x, y, onTop},
        light { color, intensity}, drawOrder(only for tiles),
        bounce{minHeight, height, speed},
        pulse{minHeight, height, speed},
        fade{start, end, speed}

        onAttach, onDetach
    }
]]
--
AttachedEffectManager.register(1, 'Spoke Lighting', 12, ThingCategoryEffect, {
    speed = 0.5,
    onAttach = function(effect, owner)
        print('onAttach: ', effect:getId(), owner:getName())
    end,
    onDetach = function(effect, oldOwner)
        print('onDetach: ', effect:getId(), oldOwner:getName())
    end
})

-- Use the paperdoll system instead of attachedEffect for this kind of attachment, it’s more consistent.
AttachedEffectManager.register(2, 'Bat Wings', 307, ThingCategoryCreature, {
    speed = 5,
    disableWalkAnimation = true,
    shader = 'Outfit - Rainbow',
    followOwner = true,
    dirOffset = {
        [North] = { 0, -10, true },
        [East] = { 5, -5 },
        [South] = { -5, 0 },
        [West] = { -10, -5, true }
    },
    onAttach = function(effect, owner)
        owner:setBounce(0, 10, 5000)
    end,
    onDetach = function(effect, oldOwner)
        oldOwner:setBounce(0, 0)
    end
})

AttachedEffectManager.register(3, 'Angel Light', 50, ThingCategoryEffect, {
    opacity = 0.5,
    drawOnUI = false
})

AttachedEffectManager.register(4, 'Four Angel Light', 0, 0, {
    onAttach = function(effect, owner)
        local angelLight = g_attachedEffects.getById(3)
        local angelLight1 = angelLight:clone()
        local angelLight2 = angelLight:clone()
        local angelLight3 = angelLight:clone()
        local angelLight4 = angelLight:clone()

        angelLight1:setOffset(-50, 50, true)
        angelLight2:setOffset(50, 50, true)
        angelLight3:setOffset(50, -50, true)
        angelLight4:setOffset(-50, -50, true)

        effect:attachEffect(angelLight1)
        effect:attachEffect(angelLight2)
        effect:attachEffect(angelLight3)
        effect:attachEffect(angelLight4)
    end
})

AttachedEffectManager.register(5, 'Transform', 40, ThingCategoryCreature, {
    transform = true,
    duration = 5000,
    onAttach = function(effect, owner)
        local e = Effect.create()
        e:setId(7)
        owner:getTile():addThing(e)
    end,
    onDetach = function(effect, oldOwner)
        local e = Effect.create()
        e:setId(50)
        oldOwner:getTile():addThing(e)
    end
})

AttachedEffectManager.register(6, 'Lake Monster', 34, ThingCategoryEffect, {
    hideOwner = true,
    duration = 1500,
    -- loop = 1,
    onDetach = function(effect, oldOwner)
        local e = Effect.create()
        e:setId(54)
        oldOwner:getTile():addThing(e)
    end
})

AttachedEffectManager.register(7, 'Pentagram Aura', '/images/game/effects/pentagram', ThingExternalTexture, {
    size = { 128, 128 },
    offset = { 50, 45 }
})

-- Ki venia a 140x110, mas de cuatro casillas de ancho: es el aura enorme que
-- se veia en juego. Se baja a 64x64, dos casillas, que ya rodea al personaje
-- sin taparlo. El offset se escalo con el mismo factor (0.457) que el tamano,
-- para conservar el encuadre que tenia. Al ser textura externa SI respeta
-- `size` (attachedeffect.cpp:142); los efectos del juego no.
AttachedEffectManager.register(8, 'Ki', '/images/game/effects/ki', ThingExternalTexture, {
    size = { 64, 64 },
    offset = { 27, 34, true },
    pulse = { 0, 20, 3000 },
    --fade = { 0, 100, 1000 },
})

AttachedEffectManager.register(9, 'Thunder', '/images/game/effects/thunder', ThingExternalTexture, {
    loop = 1,
    offset = { 215, 230 }
})

AttachedEffectManager.register(10, 'Dynamic Effect', 0, 0, {
    duration = 500,
    onAttach = function(effect, owner)
        local spriteSize = g_gameConfig.getSpriteSize()
        local length = 3

        local missile = AttachedEffect.create(38, ThingCategoryMissile)
        missile:setDuration(effect:getDuration())
        missile:setDirection(5)
        missile:setOffset(spriteSize * length, 0)
        missile:setBounce(0, 15, 1000)
        missile:move(Position.translated(owner:getPosition(), -length, 0), owner:getPosition())
        effect:attachEffect(missile)

        missile = AttachedEffect.create(38, ThingCategoryMissile)
        missile:setDuration(effect:getDuration())
        missile:setDirection(3)
        missile:setOffset(-(spriteSize * length), 0)
        missile:setBounce(0, 15, 1000)
        missile:move(Position.translated(owner:getPosition(), length, 0), owner:getPosition())

        effect:attachEffect(missile)
    end,
    onDetach = function(effect, oldOwner)
        local e = Effect.create()
        e:setId(50)
        oldOwner:getTile():addThing(e)
    end
})

AttachedEffectManager.register(11, 'Bat', 307, ThingCategoryCreature, {
    speed = 0.5,
    offset = { 0, 0 },
    bounce = { 20, 20, 2000 }
})

--[[
    Auras -- seleccion final, 2026-09-08.

    Las 16 salen de mirar los 116 efectos del cliente que ocupan UNA casilla
    (celda 32x32 y patron 1x1) y tienen 6 o mas fases. Se descartaron los que
    son proyectil o salpicadura (flechas, cortes, gotas) y se dejaron los que
    envuelven al personaje: anillos, circulos runicos y orbes.

    onTop = true (el tercer valor del offset): con `false` el aura se dibuja
    POR DEBAJO del personaje, y un efecto de una casilla bajo un outfit de
    una casilla queda tapado del todo -- invisible en juego y en la vista
    previa de la ventana de outfit. lib.lua:94 aplica el flag aunque el
    offset sea 0,0. La opacidad baja compensa que ahora va encima.

    Por que una casilla: `size` no sirve para encoger un efecto del juego
    (attachedeffect.cpp:142 solo aplica m_size a texturas externas), asi que el
    unico control de tamano es elegir el efecto adecuado. offset {0,0} porque
    con 32x32 ya cae justo encima de la criatura.

    Cada id debe estar declarado tambien en
    D:\Canary\data\XMLttachedeffects.xml o el servidor rechaza la eleccion.
]]
local AURAS = {
    -- id  nombre                  efecto  velocidad  opacidad
    { 12, 'Aura Dorada',            245, 0.7, 0.60 },
    { 13, 'Aura de Fuego',          329, 0.7, 0.60 },
    { 14, 'Aura de Hielo',          225, 0.6, 0.60 },
    { 15, 'Aura de Naturaleza',     280, 0.7, 0.60 },
    { 16, 'Aura de Energia',        274, 0.7, 0.60 },
    { 17, 'Aura de Sombra',         292, 0.5, 0.60 },
    { 18, 'Aura de Tormenta',       181, 0.9, 0.60 },
    { 19, 'Aura Arcana',            222, 0.7, 0.60 },
    { 20, 'Anillo Azul',            2,   0.6, 0.60 },
    { 21, 'Anillo Espectral',       343, 0.6, 0.60 },
    { 22, 'Circulo Runico Verde',   272, 0.6, 0.60 },
    { 23, 'Circulo Runico Rojo',    273, 0.6, 0.60 },
    { 24, 'Circulo Runico Naranja', 275, 0.6, 0.60 },
    { 25, 'Orbe Carmesi',           223, 0.7, 0.60 },
    { 26, 'Orbe Plateado',          226, 0.7, 0.60 },
    { 27, 'Runa Oscura',            10,  0.6, 0.60 },
}

for _, aura in ipairs(AURAS) do
    local id, nombre, efecto, velocidad, opacidad = aura[1], aura[2], aura[3], aura[4], aura[5]
    AttachedEffectManager.register(id, nombre, efecto, ThingCategoryEffect, {
        speed = velocidad,
        opacity = opacidad,
        followOwner = true,
        offset = { 0, 0, true },
    })
end
