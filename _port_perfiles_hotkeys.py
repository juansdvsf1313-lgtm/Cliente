# -*- coding: utf-8 -*-
"""
Trae al cliente de Canary la capa de perfiles de hotkeys del cliente 8.60.

Lo que se trae es la MECANICA, no el aspecto: un perfil cubre a la vez las
teclas F1-F12 y la barra de acciones, porque los dos leen del mismo sitio
(clientoptions.json -> hotkeyOptions.hotkeySets, el formato de CipSoft). La
interfaz se monta con los estilos que ya usa este cliente.

La maquinaria de perfiles YA estaba en game_actionbar/logics/ApiJson.lua
(createHotkeySet, renameHotkeySet, removeHotkeySet, setCurrentHotkeySetName);
lo unico que se le anadio fueron tres getters. Aqui solo se enchufa el panel de
hotkeys a eso.

Almacenamiento: los perfiles son GLOBALES. Cuando hay un perfil activo las
teclas se guardan en game_hotkeys.profiles[nombre]; si no hay ninguno se
mantiene el camino de siempre (por servidor y personaje), asi que lo que ya
estuviera guardado sigue funcionando.

CharacterHotkeys (el auto-selector por vocacion del 8.60) no existe en este
cliente. Las llamadas van protegidas y simplemente no hacen nada.
"""
import sys, time, shutil
from pathlib import Path

RAIZ = Path(r"D:\Canary_OTClient_src\modules\game_hotkeys")
LUA = RAIZ / "hotkeys_manager.lua"
OTUI = RAIZ / "hotkeys_manager.otui"
SELLO = time.strftime("%Y%m%d_%H%M%S")

fallos = []


def sub(txt, viejo, nuevo, etiqueta):
    n = txt.count(viejo)
    if n != 1:
        fallos.append(f"{etiqueta}: esperaba 1 coincidencia, encontre {n}")
        return txt
    print(f"  OK  {etiqueta}")
    return txt.replace(viejo, nuevo, 1)


t = LUA.read_text(encoding="utf-8")

# --- 1. variables del modulo ------------------------------------------------
t = sub(t, """boundCombosCallback = {}
hotkeysList = {}""", """boundCombosCallback = {}
hotkeysList = {}
hotkeyProfileCombo = nil
profileComboUpdating = false""", "1. variables del combo")

# --- 2. enganchar el combo en init() ----------------------------------------
t = sub(t, """    currentHotkeys = hotkeysWindow:getChildById('currentHotkeys')""",
        """    currentHotkeys = hotkeysWindow:getChildById('currentHotkeys')
    hotkeyProfileCombo = hotkeysWindow:getChildById('hotkeyProfileCombo')""",
        "2. init(): coger el combo")

# --- 3. soltarlo en terminate() ---------------------------------------------
t = sub(t, """function terminate()""", """function terminate()
    hotkeyProfileCombo = nil""", "3. terminate(): soltar el combo")

# --- 4. refrescar el combo al entrar ----------------------------------------
t = sub(t, """function online()
    reload()
    hide()
end""", """function online()
    reload()
    refreshProfileCombo()
    hide()
end""", "4. online(): refrescar el combo")

# --- 5. el bloque de perfiles, justo antes de load() -------------------------
BLOQUE = '''-- ===========================================================================
--  Perfiles de hotkeys
-- ===========================================================================
--  La lista de perfiles vive en los ajustes de la barra de acciones
--  (clientoptions.json -> hotkeyOptions.hotkeySets), el mismo sitio del que la
--  barra lee su propia distribucion. Compartirlo es lo que hace que UN perfil
--  cubra a la vez las teclas F1-F12 y la barra, como el cliente real.
-- ===========================================================================

local function apiJson()
    local ab = modules.game_actionbar
    return ab and ab.ApiJson or nil
end

local function profileKey()
    local api = apiJson()
    if api and api.getCurrentHotkeySetName then
        local name = api.getCurrentHotkeySetName()
        if name and name ~= '' then
            return name
        end
    end
    return nil
end

-- Las cinco bases de vocacion son el material de partida para quien no tenga
-- nada guardado, asi que se pueden copiar pero nunca renombrar ni borrar.
local PROTECTED_PROFILES = {
    Knight = true, Paladin = true, Sorcerer = true, Druid = true, Monk = true,
}

local USETYPE_FROM_JSON = {
    UseOnYourself = HOTKEY_MANAGER_USEONSELF,
    UseOnTarget = HOTKEY_MANAGER_USEONTARGET,
    UseWith = HOTKEY_MANAGER_USEWITH,
}

local function trimName(name)
    return name and string.gsub(name, '^%s*(.-)%s*$', '%1') or ''
end

-- Mueve o borra la lista de teclas de un perfil junto con el perfil, para que
-- renombrar conserve las teclas y borrar no deje una rama huerfana.
local function migrateStoredKeys(oldName, newName)
    local node = g_settings.getNode('game_hotkeys') or {}
    local byProfile = node['profiles']
    if not byProfile then
        return
    end
    if newName then
        byProfile[newName] = byProfile[oldName]
    end
    byProfile[oldName] = nil
    g_settings.setNode('game_hotkeys', node)
    g_settings.save()
end

function refreshProfileCombo()
    if not hotkeyProfileCombo then
        return
    end
    local api = apiJson()
    if not api or not api.getHotkeySetNames then
        hotkeyProfileCombo:setVisible(false)
        return
    end

    profileComboUpdating = true
    hotkeyProfileCombo:clearOptions()
    local names = api.getHotkeySetNames() or {}
    for _, name in ipairs(names) do
        hotkeyProfileCombo:addOption(name)
    end
    local current = api.getCurrentHotkeySetName and api.getCurrentHotkeySetName()
    if current and current ~= '' then
        hotkeyProfileCombo:setCurrentOption(current, true)
    end
    hotkeyProfileCombo:setVisible(#names > 0)
    profileComboUpdating = false
end

-- Construye una lista de teclas F a partir de las asignaciones de la barra del
-- perfil (barra 1, botones 1..12). Los hechizos pasan a hotkey de texto; los
-- objetos usables (pociones) conservan item y tipo de uso. Devuelve nil si el
-- perfil no tiene nada que copiar.
function seedFromProfile(profile)
    local api = apiJson()
    if not api or not api.getHotkeySet then
        return nil
    end
    local set = api.getHotkeySet(profile)
    local mappings = set and set.actionBarOptions and set.actionBarOptions.mappings
    if not mappings then
        return nil
    end
    local out = {}
    local n = 0
    for _, m in pairs(mappings) do
        local a = m.actionsetting
        if a and (m.actionBar == 1 or m.actionBar == nil)
            and m.actionButton and m.actionButton >= 1 and m.actionButton <= 12 then
            local combo = 'F' .. m.actionButton
            if a.chatText and a.chatText ~= '' then
                out[combo] = { value = a.chatText,
                               autoSend = a.sendAutomatically and true or false }
                n = n + 1
            elseif a.useObject then
                out[combo] = { itemId = a.useObject,
                               useType = USETYPE_FROM_JSON[a.useType] }
                n = n + 1
            end
        end
    end
    if n == 0 then
        return nil
    end
    return out
end

function newProfile()
    local api = apiJson()
    if not api or not api.createHotkeySet then
        return
    end
    local box = UIInputBox.create(tr('New Profile'), function(name)
        name = trimName(name)
        if name == '' then
            return
        end
        if api.getHotkeySet and api.getHotkeySet(name) then
            displayErrorBox(tr('Hotkeys'), tr('A profile with that name already exists.'))
            return
        end
        local from = profileKey()
        save()
        api.createHotkeySet(name, from)
        if from then
            local node = g_settings.getNode('game_hotkeys') or {}
            local byProfile = node['profiles']
            if byProfile and byProfile[from] then
                byProfile[name] = byProfile[from]
                g_settings.setNode('game_hotkeys', node)
                g_settings.save()
            end
        end
        api.setCurrentHotkeySetName(name)
        reload()
        refreshProfileCombo()
    end)
    box:addLineEdit(tr('Name'), '', 30)
    box:display(tr('Create'), tr('Cancel'))
end

function renameProfile()
    local api = apiJson()
    local old = profileKey()
    if not api or not api.renameHotkeySet or not old then
        return
    end
    if PROTECTED_PROFILES[old] then
        displayErrorBox(tr('Hotkeys'), tr('Vocation base profiles cannot be renamed.'))
        return
    end
    local box = UIInputBox.create(tr('Rename Profile'), function(name)
        name = trimName(name)
        if name == '' or name == old then
            return
        end
        local ok, err = api.renameHotkeySet(old, name)
        if not ok then
            displayErrorBox(tr('Hotkeys'), tr('Could not rename: %s', err or '?'))
            return
        end
        migrateStoredKeys(old, name)
        refreshProfileCombo()
    end)
    box:addLineEdit(tr('Name'), old, 30)
    box:display(tr('Rename'), tr('Cancel'))
end

function deleteProfile()
    local api = apiJson()
    local name = profileKey()
    if not api or not api.removeHotkeySet or not name then
        return
    end
    if PROTECTED_PROFILES[name] then
        displayErrorBox(tr('Hotkeys'), tr('Vocation base profiles cannot be deleted.'))
        return
    end
    local msgBox
    msgBox = displayGeneralBox(tr('Delete Profile'),
        tr("Delete profile '%s' and its hotkeys?", name), {
        { text = tr('Delete'), callback = function()
            local ok, err = api.removeHotkeySet(name)
            if ok then
                migrateStoredKeys(name, nil)
                reload()
                refreshProfileCombo()
            else
                displayErrorBox(tr('Hotkeys'), tr('Could not delete: %s', err or '?'))
            end
            msgBox:destroy()
        end },
        { text = tr('Cancel'), callback = function()
            msgBox:destroy()
        end },
    })
end

-- Cambiar de perfil tiene que guardar antes lo que hay en pantalla; si no, las
-- ediciones hechas desde que se abrio la ventana se irian al perfil que se deja.
function onProfileChange(name)
    if profileComboUpdating or not name or name == '' then
        return
    end
    local api = apiJson()
    if not api or not api.setCurrentHotkeySetName then
        return
    end
    if api.getCurrentHotkeySetName and api.getCurrentHotkeySetName() == name then
        return
    end
    save()
    api.setCurrentHotkeySetName(name)
    reload()
end

'''

t = sub(t, "function load(forceDefaults)", BLOQUE + "function load(forceDefaults)",
        "5. bloque de perfiles")

# --- 6. load(): rama por perfil ---------------------------------------------
t = sub(t, """    if perServer and not table.empty(hotkeys) then
        if G.host ~= nil then
            serverHost = string.gsub(G.host, "^https?://", "")
            hotkeys = hotkeys[serverHost]
        end
    end
    if perCharacter and not table.empty(hotkeys) then
        hotkeys = hotkeys[g_game.getCharacterName()]
    end""",
        """    -- Con un perfil activo las teclas viven bajo ese perfil y son globales.
    -- Sin perfil se mantiene el camino de siempre (servidor + personaje), asi
    -- que lo que ya hubiera guardado sigue cargando igual.
    local profile = profileKey()
    if profile then
        local byProfile = hotkeys['profiles']
        local stored = byProfile and byProfile[profile]
        if table.empty(stored) then
            -- Perfil sin teclas propias: se siembran desde la barra de acciones
            -- de ese mismo perfil, que es lo que conecta las dos cosas.
            stored = seedFromProfile(profile)
        end
        hotkeys = stored or {}
    else
        if perServer and not table.empty(hotkeys) then
            if G.host ~= nil then
                serverHost = string.gsub(G.host, "^https?://", "")
                hotkeys = hotkeys[serverHost]
            end
        end
        if perCharacter and not table.empty(hotkeys) then
            hotkeys = hotkeys[g_game.getCharacterName()]
        end
    end""", "6. load(): rama por perfil")

# --- 7. save(): rama por perfil ---------------------------------------------
t = sub(t, """function save()
    local serverHost = string.gsub(G.host, "^https?://", "")
    local hotkeySettings = g_settings.getNode('game_hotkeys') or {}
    local hotkeys = hotkeySettings

    if perServer then""",
        """function save()
    local serverHost = string.gsub(G.host, "^https?://", "")
    local hotkeySettings = g_settings.getNode('game_hotkeys') or {}
    local hotkeys = hotkeySettings

    local profile = profileKey()
    if profile then
        hotkeySettings['profiles'] = hotkeySettings['profiles'] or {}
        hotkeySettings['profiles'][profile] = hotkeySettings['profiles'][profile] or {}
        hotkeys = hotkeySettings['profiles'][profile]
    elseif perServer then""", "7. save(): rama por perfil")

t = sub(t, """    if perCharacter then
        local char = g_game.getCharacterName()
        if not hotkeys[char] then
            hotkeys[char] = {}
        end
        hotkeys = hotkeys[char]
    end""",
        """    if not profile and perCharacter then
        local char = g_game.getCharacterName()
        if not hotkeys[char] then
            hotkeys[char] = {}
        end
        hotkeys = hotkeys[char]
    end""", "7b. save(): saltar el por-personaje si hay perfil")

if fallos:
    print("\\n!! NO SE ESCRIBIO NADA:")
    for f in fallos:
        print("  -", f)
    sys.exit(1)

shutil.copy2(LUA, str(LUA) + f".bak_perfiles_{SELLO}")
LUA.write_bytes(t.encode("utf-8"))
print(f"\\nEscrito hotkeys_manager.lua. Respaldo con sello {SELLO}")
