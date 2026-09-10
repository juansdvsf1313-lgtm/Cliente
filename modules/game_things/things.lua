ThingsLoaderController = Controller:new()

local filename = nil
local loaded = false

function setFileName(name)
    filename = name
end

function isLoaded()
    return loaded
end

local function tryLoadDatWithFallbacks(datPath)
    if g_things.loadDat(datPath) then
        return true
    end

    local featureFlags = {
        GameSpritesU32,
        GameEnhancedAnimations,
        GameIdleAnimations
    }

    local combinations = {
        { 1 }, { 2 }, { 3 },
        { 1, 2 }, { 1, 3 }, { 2, 3 },
        { 1, 2, 3 }
    }

    for _, combo in ipairs(combinations) do
        for _, idx in ipairs(combo) do
            g_game.enableFeature(featureFlags[idx])
        end

        if g_things.loadDat(datPath) then
            return true
        end
    end

    return false
end

-- Modo HD: assets de doble resolucion en /data/things/<version>_hd. El tamano de
-- sprite tiene que cambiar A LA VEZ, porque ThingType calcula cuantos tiles ocupa
-- un objeto dividiendo los pixeles del sprite entre g_gameConfig.getSpriteSize():
-- con sprites de 64 y tamano 32 todo saldria del doble de grande, y al reves
-- daria 0 tiles y no se veria nada.
local function resolveAssets(version)
    if g_settings.getBoolean('hdMode', false) then
        -- Ojo: resolvepath() con '/' inicial devuelve la ruta tal cual, nunca nil,
        -- asi que comprobarla contra nil no verificaba nada. Hay que preguntarle al
        -- gestor de recursos si la carpeta esta montada de verdad.
        local hdPath = string.format('/data/things/%d_hd/', version)
        if g_resources.directoryExists(hdPath) and
           g_resources.fileExists(hdPath .. 'catalog-content.json') then
            g_gameConfig.setSpriteSize(64)
            g_logger.info(string.format('[things] modo HD: %s (sprite-size 64)', hdPath))
            return hdPath
        end
        g_logger.error(string.format(
            '[things] modo HD activado pero no se encuentra %s - se usan los assets normales', hdPath))
    end

    g_gameConfig.setSpriteSize(32)
    return resolvepath(string.format('/data/things/%d/', version))
end

-- Cambio de HD/SD en caliente. Es posible porque loadAppearances() ya hace
-- things.clear() y g_spriteAppearances.unload() por dentro: reconstruye todos los
-- ThingType con el tamano de sprite vigente y suelta las hojas cacheadas. Lo unico
-- que queda por nuestra cuenta es forzar el recalculo de la geometria del mapa,
-- porque el tile cambia de tamano y setVisibleDimension ignora el mismo valor.
function reloadThingsAssets()
    local version = g_game.getClientVersion()
    g_logger.info(string.format('[things] reloadThingsAssets: version=%s hdMode=%s',
        tostring(version), tostring(g_settings.getBoolean('hdMode', false))))
    if version == nil or version < 1281 or g_game.getFeature(GameLoadSprInsteadProtobuf) then
        -- Todavia no hay assets cargados: bastara con que load() lea la opcion.
        return true
    end

    local filePath = resolveAssets(version)
    if filePath == nil then
        g_logger.error('[things] no se encontro la carpeta de assets')
        return false
    end

    if not g_things.loadAppearances(filePath) then
        g_logger.error('[things] fallo al recargar appearances')
        return false
    end
    if not g_things.loadStaticData(filePath) then
        g_logger.error('[things] fallo al recargar staticdata')
        return false
    end

    local map = modules.game_interface and modules.game_interface.getMapPanel()
    if map then
        map:zoomIn()
        map:zoomOut()
    end

    g_logger.info(string.format('[things] assets recargados en caliente (sprite-size %s)',
        tostring(g_gameConfig.getSpriteSize())))
    return true
end

local function load(version)
    local errorList = {}

    if version >= 1281 and not g_game.getFeature(GameLoadSprInsteadProtobuf) then
        local filePath = resolveAssets(version)
        if not g_things.loadAppearances(filePath) then
            errorList[#errorList + 1] = "Couldn't load assets"
        end
        if not g_things.loadStaticData(filePath) then
            errorList[#errorList + 1] = "Couldn't load staticdata"
        end
        if g_game.getFeature(GameProficiency) and not g_things.resolveProficienciesFile(filePath) then
            g_logger.warning(string.format(
                    "[game_things.load()] Couldn't load /data/things/%d/proficiencies-<hash>.json", version))
        end
    else
        local datPath, sprPath
        if filename then
            datPath = resolvepath('/data/things/' .. filename)
            sprPath = resolvepath('/data/things/' .. filename)
        else
            datPath = resolvepath('/data/things/' .. version .. '/Tibia')
            sprPath = resolvepath('/data/things/' .. version .. '/Tibia')
        end

        g_logger.setLevel(5)
        if not tryLoadDatWithFallbacks(datPath) then
            errorList[#errorList + 1] = tr('Unable to load dat file, please place a valid dat in \'%s.dat\'', datPath)
        end
        g_logger.setLevel(1)

        if not g_sprites.loadSpr(sprPath) then
            errorList[#errorList + 1] = tr('Unable to load spr file, please place a valid spr in \'%s.spr\'', sprPath)
        end
        if g_game.getFeature(GameLoadSprInsteadProtobuf) and version >= 1281 then
            local staticPath = resolvepath(string.format('/data/things/%d/appearances', version))
            if not g_things.loadAppearances(staticPath) then
                g_logger.warning(string.format(
                    "[game_things.load()] Couldn't load /data/things/%d/appearances.dat, possible packets error.", version))
            end
        end
    end

    loaded = #errorList == 0
    if loaded then
        -- loading client files was successful, try to load sounds now
        -- sound files are optional, this means that failing to load them
        -- will not block logging into game
        g_sounds.loadClientFiles(resolvepath(string.format('/data/sounds/%d/', version)))
        return
    end

    local messageBox = displayErrorBox(tr('Error'), table.concat(errorList, "\n"))
    addEvent(function()
        messageBox:raise()
        messageBox:focus()
    end)

    g_game.setClientVersion(0)
    g_game.setProtocolVersion(0)
end

function ThingsLoaderController:onInit()
    self:registerEvents(g_game, {
        onClientVersionChange = load
    })
end
