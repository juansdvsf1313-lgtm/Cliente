local HOTKEY = 'Ctrl+Y'
local MAP_SHADERS = { {
    name = 'Map - Default',
    frag = nil
}, {
    -- Escalado dirigido por bordes: suaviza el pixelado reconstruyendo las
    -- diagonales, sin desenfocar. Lo mas parecido a HD sin arte nuevo.
    name = 'Map - HD Edge',
    frag = 'shaders/fragment/hd_edge.frag',
    useFramebuffer = true
}, {
    -- Super-resolucion estilo waifu2x ("Single-Image Super-Resolution for
    -- Anime-Style Art using Deep Convolutional Neural Networks"): analiza la
    -- estructura local, reconstruye siguiendola y devuelve el detalle como
    -- residuo. Mas caro que HD Edge (16 lecturas por pixel) pero es el que
    -- realmente parece HD: diagonales continuas y contornos que no se pierden.
    name = 'Map - HD Anime SR',
    frag = 'shaders/fragment/hd_anime_sr.frag',
    useFramebuffer = true
}, {
    -- Mismo shader, menos desenfoque en las zonas sin borde (suelo, piedra):
    -- SIGMA_FLAT es lo que decide si esas zonas se ven definidas o lavadas.
    name = 'Map - HD Anime SR (Nitido)',
    frag = 'shaders/fragment/hd_anime_sr.frag',
    useFramebuffer = true,
    defines = {
        SR_SIGMA_FLAT = '0.48',
        SR_SIGMA_ALONG = '0.82',
        SR_SIGMA_ACROSS = '0.22',
        SR_SHARPNESS = '0.95',
        SR_LINE_DARK = '0.30'
    }
}, {
    -- El mas HD de los tres: en zona plana casi no promedia, asi que la textura
    -- queda tan definida como sin shader, y el suavizado se reserva solo para
    -- las diagonales. Si algun suelo con dithering te pica demasiado, sube
    -- SR_SIGMA_FLAT a 0.44 o baja SR_SHARPNESS.
    name = 'Map - HD Anime SR (Cristalino)',
    frag = 'shaders/fragment/hd_anime_sr.frag',
    useFramebuffer = true,
    defines = {
        SR_SIGMA_FLAT = '0.32',
        SR_SIGMA_ALONG = '0.80',
        SR_SIGMA_ACROSS = '0.18',
        SR_SHARPNESS = '1.20',
        SR_LINE_DARK = '0.35'
    }
}, {
    name = 'Map - Fog',
    frag = 'shaders/fragment/fog.frag',
    tex1 = 'images/clouds'
}, {
    name = 'Map - Rain',
    frag = 'shaders/fragment/rain.frag'
}, {
    name = 'Map - Snow',
    frag = 'shaders/fragment/snow.frag',
    tex1 = 'images/snow'
}, {
    name = 'Map - Gray Scale',
    frag = 'shaders/fragment/grayscale.frag'
}, {
    name = 'Map - Bloom',
    frag = 'shaders/fragment/bloom.frag'
}, {
    name = 'Map - Sepia',
    frag = 'shaders/fragment/sepia.frag'
}, {
    name = 'Map - Pulse',
    frag = 'shaders/fragment/pulse.frag',
    drawViewportEdge = true
}, {
    name = 'Map - Old Tv',
    frag = 'shaders/fragment/oldtv.frag'
}, {
    name = 'Map - Party',
    frag = 'shaders/fragment/party.frag'
}, {
    name = 'Map - Radial Blur',
    frag = 'shaders/fragment/radialblur.frag',
    drawViewportEdge = true
}, {
    name = 'Map - Zomg',
    frag = 'shaders/fragment/zomg.frag',
    drawViewportEdge = true
}, {
    name = 'Map - Heat',
    frag = 'shaders/fragment/heat.frag',
    drawViewportEdge = true
}, {
    name = 'Map - Noise',
    frag = 'shaders/fragment/noise.frag'
} }

local OUTFIT_SHADERS = { {
    name = 'Outfit - Default',
    frag = nil
}, {
    name = 'Outfit - Rainbow',
    frag = 'shaders/fragment/party.frag'
}, {
    name = 'Outfit - Ghost',
    frag = 'shaders/fragment/radialblur.frag',
    drawColor = false
}, {
    name = 'Outfit - Jelly',
    frag = 'shaders/fragment/heat.frag'
}, {
    name = 'Outfit - Fragmented',
    frag = 'shaders/fragment/noise.frag'
}, {
    name = 'Outfit - cyclopedia-black',
    frag = 'shaders/fragment/cyclopedia.frag'
}, {
    name = 'Outfit - Outline',
    useFramebuffer = true,
    frag = 'shaders/fragment/outline.frag'
}, {
    name = 'Outfit - ForgeDonor',
    useFramebuffer = true,
    frag = 'shaders/fragment/forge_donor.frag'
}, {
    name = 'Outfit - ForgeSuccess',
    useFramebuffer = true,
    frag = 'shaders/fragment/forge_success.frag'
}, {
    name = 'Outfit - ForgeFailed',
    useFramebuffer = true,
    frag = 'shaders/fragment/forge_failed.frag'
}, }

local MOUNT_SHADERS = { {
    name = 'Mount - Default',
    frag = nil
}, {
    name = 'Mount - Rainbow',
    frag = 'shaders/fragment/party.frag'
} }

-- Text shaders for improved readability and visual effects
-- All shaders use multi-sample circular sampling for smooth outlines
local TEXT_SHADERS = { {
    name = 'Text - Default',
    frag = nil -- No shader, standard text rendering
}, {
    name = 'Text - Gold Outline',
    frag = 'shaders/fragment/text_golden_shadow_bold_fragment.frag' -- Smooth gold (#ee8413) outline
}, {
    name = 'Text - Black Outline',
    frag = 'shaders/fragment/text_black_outline.frag' -- Classic black outline, max readability
}, {
    name = 'Text - Glow',
    frag = 'shaders/fragment/text_glow.frag' -- Soft glow effect (higher GPU cost)
} }

local function attachShaders()
    local map = modules.game_interface.getMapPanel()
    map:setShader('Default')

    local player = g_game.getLocalPlayer()
    if player then
        player:setShader('Default')
        player:setMountShader('Default')
    end
end

-- g_shaders compila en un evento diferido y se queda con el puntero crudo de la
-- cadena, no con una copia. Las fuentes generadas se guardan aqui para que el
-- recolector de Lua no las libere antes de que se compilen.
local generatedSources = {}

-- Un preset es el mismo .frag con otras constantes: se le anteponen los #define
-- y se compila desde codigo, asi no hay tres copias del algoritmo que mantener
-- en paralelo. Los valores van como texto a proposito: string.format('%f') usa
-- el separador decimal del sistema y en un Windows en espanol escribiria "0,48",
-- que GLSL no entiende.
local buildSource = function(opts, path)
    local prefix = ''
    for key, value in pairs(opts.defines) do
        prefix = prefix .. string.format('#define %s %s\n', key, value)
    end
    return prefix .. g_resources.readFileContents(path)
end

local registerShader = function(opts, method)
    local fragmentShaderPath = resolvepath(opts.frag)

    if fragmentShaderPath ~= nil then
        if opts.defines then
            generatedSources[opts.name] = buildSource(opts, fragmentShaderPath)
            g_shaders.createFragmentShaderFromCode(opts.name, generatedSources[opts.name],
                opts.useFramebuffer or false)
        else
            g_shaders.createFragmentShader(opts.name, opts.frag, opts.useFramebuffer or false)
        end

        if opts.tex1 then
            g_shaders.addMultiTexture(opts.name, opts.tex1)
        end
        if opts.tex2 then
            g_shaders.addMultiTexture(opts.name, opts.tex2)
        end

        -- Setup proper uniforms
        g_shaders[method](opts.name)
    end
end

-- Un shader que no compile NO debe abortar onInit: si eso ocurre, el Keybind de
-- Ctrl+Y nunca llega a registrarse (esta al final) y la ventana de shaders queda
-- inaccesible sin ninguna pista de por que.
local registerShaderSafe = function(opts, method)
    local ok, err = pcall(registerShader, opts, method)
    if not ok then
        g_logger.error(string.format("[game_shaders] no se pudo registrar '%s': %s",
            tostring(opts.name), tostring(err)))
    end
end

ShaderController = Controller:new()

function ShaderController:onInit()
    for _, opts in pairs(MAP_SHADERS) do
        registerShaderSafe(opts, 'setupMapShader')
    end

    for _, opts in pairs(OUTFIT_SHADERS) do
        registerShaderSafe(opts, 'setupOutfitShader')
    end

    for _, opts in pairs(MOUNT_SHADERS) do
        registerShaderSafe(opts, 'setupMountShader')
    end

    for _, opts in pairs(TEXT_SHADERS) do
        registerShaderSafe(opts, 'setupTextShader')
    end

    g_logger.info(string.format("[game_shaders] shaders de mapa registrados: %d", #MAP_SHADERS))
    Keybind.new('Windows', 'show/hide Shader Windows', HOTKEY, '')
    Keybind.bind('Windows', 'show/hide Shader Windows', {
        {
            type = KEY_DOWN,
            callback = function()
                g_logger.info("[game_shaders] atajo pulsado")
                if ShaderController.ui then
                    ShaderController:unloadHtml()
                else
                    ShaderController:open()
                end
            end,
        }
    })
end

function ShaderController:onTerminate()
    g_shaders.clear()
    Keybind.delete('Windows', 'show/hide Shader Windows')
end

function ShaderController:onGameStart()
    attachShaders()
end

function ShaderController:onMapComboBoxChange(event)
    local map = modules.game_interface.getMapPanel()
    map:setShader(event.text)

    local data = event.target:getCurrentOption().data
    map:setDrawViewportEdge(data.drawViewportEdge == true)
end

function ShaderController:onOutfitComboBoxChange(event)
    local player = g_game.getLocalPlayer()
    if player then
        player:setShader(event.text)
        local data = event.target:getCurrentOption().data
        player:setDrawOutfitColor(data.drawColor ~= false)
    end
end

function ShaderController:onMountComboBoxChange(event)
    local player = g_game.getLocalPlayer()
    if player then
        player:setMountShader(event.text)
    end
end

function ShaderController:open()
    self:loadHtml('shaders.html', modules.game_interface.getMapPanel())

    for _, opts in pairs(MAP_SHADERS) do
        self.ui.mapComboBox:addOption(opts.name, opts)
    end

    for _, opts in pairs(OUTFIT_SHADERS) do
        self.ui.outfitComboBox:addOption(opts.name, opts)
    end

    for _, opts in pairs(MOUNT_SHADERS) do
        self.ui.mountComboBox:addOption(opts.name, opts)
    end
    for _, opts in pairs(TEXT_SHADERS) do
        self.ui.textComboBox:addOption(opts.name, opts)
    end
end

function ShaderController:onTextComboBoxChange(event)
    -- Apply text shader to local player's name
    -- This affects how the player's name is rendered above their character
    local player = g_game.getLocalPlayer()
    if player then
        -- If using widget-based name rendering
        local infoWidget = player:getWidgetInformation()
        if infoWidget then
            infoWidget:setShader(event.text)
        end
        -- Apply to engine-level name rendering (requires C++ support)
        if player.setNameShader then
            player:setNameShader(event.text)
        end
    end
end
