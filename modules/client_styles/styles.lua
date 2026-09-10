local resourceLoaders = {
    ["otui"] = g_ui.importStyle,
    ["otfont"] = g_fonts.importFont,
    ["ttf"] = g_fonts.importFont,
    ["otf"] = g_fonts.importFont,
    ["otps"] = g_particles.importParticle,
}

function init()
    local device = g_platform.getDevice()
    importResources("styles", "otui", device)
    importResources("fonts/otfont", "otfont", device)
    importResources("fonts//ttf", "ttf", device)
    importResources("fonts/ttf", "otf", device)
    importResources("particles", "otps", device)

    -- Verdana real al tamano que usa el cliente oficial (medido: 9 px de alto
    -- en texto normal, 10 px con digitos). La carga automatica solo registra
    -- las TTF a tamano 12, asi que esta se pide explicitamente.
    local okNormal = g_fonts.importFontWithSize('/fonts/ttf/Verdana.ttf', 11)
    -- Verdana negrita a 11 px: es la forma del texto del cliente oficial
    -- (medido: 38.9 px encendidos por fila frente a 28.4 sin negrita).
    -- Se registra SIEMPRE con este nombre porque los estilos la referencian asi.
    local okNegrita = g_fonts.importFontWithSize('/fonts/ttf/verdana-bold.ttf', 11)
    -- Los botones del cliente oficial usan una letra mas pequena: medido
    -- 'Improve to 65%' = 75 px de ancho y 8 de alto, que corresponde a
    -- Verdana negrita 8 px (da 76x8).
    g_fonts.importFontWithSize('/fonts/ttf/verdana-bold.ttf', 8)

    g_mouse.loadCursors('/cursors/cursors')
    g_gameConfig.loadFonts()
end

function terminate()
end

function importResources(dir, type, device)
    local path = '/' .. dir .. '/'
    local files = g_resources.listDirectoryFiles(path, true, false, true)
    for _, file in pairs(files) do
        if g_resources.isFileType(file, type) then
            resourceLoaders[type](file)
        end
    end

    -- try load device specific resources
    if device then
        local devicePath = g_platform.getDeviceShortName(device.type)
        if devicePath ~= "" then
            table.insertall(files, importResources(dir .. '/' .. devicePath, type))
        end
        local osPath = g_platform.getOsShortName(device.os)
        if osPath ~= "" then
            table.insertall(files, importResources(dir .. '/' .. osPath, type))
        end
        return
    end
    return files
end

function reloadParticles()
    g_particles.terminate()
    local device = g_platform.getDevice()
    importResources("particles", "otps", device)
end
