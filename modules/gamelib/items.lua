-- to-do
-- change to ItemsDatabase.setTier(UIitem) to UIitem:setTier()
ItemsDatabase = {}

ItemsDatabase.rarityColors = {
    ["yellow"] = TextColors.yellow,
    ["purple"] = TextColors.purple,
    ["blue"] = TextColors.blue,
    ["green"] = TextColors.green,
    ["grey"] = TextColors.grey,
}

-- Umbrales de RL, para que la tabla de abajo se lea sola.
local GREY, GREEN, BLUE, PURPLE, YELLOW = 1, 1000, 10000, 100000, 1000000

-- getMeanPrice() es la media del precio de los NPCs y devuelve 0 si ningun NPC
-- vende el item (thingtype.cpp:1146). Todo el equipo de gama alta cae ahi y se
-- quedaba sin marco: Sanguine, Moonsilver, etc. RL usa el valor de la Cyclopedia,
-- que no viene en los assets del cliente, asi que hay que aportarlo aqui.
--
-- OJO: los valores de abajo son la CATEGORIA que les he asignado yo, no el valor
-- real de Cyclopedia. Comprueba en el cliente oficial de que color salen y cambia
-- la constante si no coincide; con eso basta, el numero exacto da igual.
ItemsDatabase.priceOverrides = {
    -- Sanguine
    [43864] = YELLOW, -- sanguine blade
    [43865] = YELLOW, -- grand sanguine blade
    [43866] = YELLOW, -- sanguine cudgel
    [43867] = YELLOW, -- grand sanguine cudgel
    [43868] = YELLOW, -- sanguine hatchet
    [43869] = YELLOW, -- grand sanguine hatchet
    [43870] = YELLOW, -- sanguine razor
    [43871] = YELLOW, -- grand sanguine razor
    [43872] = YELLOW, -- sanguine bludgeon
    [43873] = YELLOW, -- grand sanguine bludgeon
    [43874] = YELLOW, -- sanguine battleaxe
    [43875] = YELLOW, -- grand sanguine battleaxe
    [43876] = YELLOW, -- sanguine legs
    [43877] = YELLOW, -- sanguine bow
    [43878] = YELLOW, -- grand sanguine bow
    [43879] = YELLOW, -- sanguine crossbow
    [43880] = YELLOW, -- grand sanguine crossbow
    [43881] = YELLOW, -- sanguine greaves
    [43882] = YELLOW, -- sanguine coil
    [43883] = YELLOW, -- grand sanguine coil
    [43884] = YELLOW, -- sanguine boots
    [43885] = YELLOW, -- sanguine rod
    [43886] = YELLOW, -- grand sanguine rod
    [43887] = YELLOW, -- sanguine galoshes
    [50146] = YELLOW, -- sanguine trousers
    [50157] = YELLOW, -- sanguine claws
    [50158] = YELLOW, -- grand sanguine claws
    -- Moonsilver
    [53207] = YELLOW, -- moonsilver epee
    [53208] = YELLOW, -- stellar moonsilver epee
    [53209] = YELLOW, -- moonsilver claymore
    [53210] = YELLOW, -- stellar moonsilver claymore
    [53211] = YELLOW, -- moonsilver axe
    [53212] = YELLOW, -- stellar moonsilver axe
    [53213] = YELLOW, -- moonsilver chopper
    [53214] = YELLOW, -- stellar moonsilver chopper
    [53215] = YELLOW, -- moonsilver crusher
    [53216] = YELLOW, -- stellar moonsilver crusher
    [53217] = YELLOW, -- moonsilver mace
    [53218] = YELLOW, -- stellar moonsilver mace
    [53219] = YELLOW, -- moonsilver channeler
    [53220] = YELLOW, -- stellar moonsilver channeler
    [53221] = YELLOW, -- moonsilver sceptre
    [53222] = YELLOW, -- stellar moonsilver sceptre
    [53223] = YELLOW, -- moonsilver katar
    [53224] = YELLOW, -- stellar moonsilver katar
    [53225] = YELLOW, -- moonsilver bow
    [53226] = YELLOW, -- stellar moonsilver bow
    [53227] = YELLOW, -- moonsilver crossbow
    [53228] = YELLOW, -- stellar moonsilver crossbow
    [53229] = YELLOW, -- moonsilver battle visor
    [53230] = YELLOW, -- moonsilver trail hood
    [53231] = YELLOW, -- moonsilver nimbus hat
    [53232] = YELLOW, -- moonsilver spirit mask
    [53233] = YELLOW, -- moonsilver strike helm
    [54267] = PURPLE, -- moonsilver crystals
}

-- Respaldo para sets que salgan en el futuro y no esten en la tabla. Un item con
-- clasificacion (el sistema de tiers) siempre es equipo de gama alta, asi que al
-- menos recibe marco en vez de quedarse pelado.
ItemsDatabase.classificationFallback = { [1] = GREEN, [2] = BLUE, [3] = PURPLE, [4] = PURPLE }

-- Punto unico de consulta del valor: lo usan tanto los marcos como el color del
-- mensaje de loot, para que no se separen.
function ItemsDatabase.getItemValue(thingType)
    if not thingType then
        return 0
    end

    local price = thingType:getMeanPrice() or 0
    if price > 0 then
        return price
    end

    local override = ItemsDatabase.priceOverrides[thingType:getId()]
    if override then
        return override
    end

    -- Esto corre en cada dibujado de casilla: aqui llega tanto un ThingType como
    -- un Item, y si getClassification no estuviera expuesto reventaria el
    -- inventario entero en vez de perderse un marco.
    if not thingType.getClassification then
        return 0
    end

    return ItemsDatabase.classificationFallback[thingType:getClassification()] or 0
end

local function getColorForValue(value)
    if value >= 1000000 then
        return "yellow"
    elseif value >= 100000 then
        return "purple"
    elseif value >= 10000 then
        return "blue"
    elseif value >= 1000 then
        return "green"
    elseif value >= 1 then
        -- RL pone marco gris desde 1 gold, no desde 50: sin marco solo cuando el
        -- valor es 0 o no esta definido. Con el umbral en 50 habia un monton de
        -- items que en el cliente oficial salen grises y aqui salian pelados.
        return "grey"
    else
        return "white"
    end
end

local function clipfunction(value)
    if value >= 1000000 then
        return "128 0 32 32"
    elseif value >= 100000 then
        return "96 0 32 32"
    elseif value >= 10000 then
        return "64 0 32 32"
    elseif value >= 1000 then
        return "32 0 32 32"
    elseif value >= 1 then
        return "0 0 32 32"
    end
    return ""
end

function ItemsDatabase.getClipAndImagePath(item)
    if not item then
        return nil, nil, nil
    end

    local frameOption = modules.client_options.getOption('framesRarity')
    if frameOption == "none" then
        return nil, nil, nil
    end
    local imagePath = '/images/ui/item'
    local clip = nil

    if type(item) == "number" then
        item = g_things.getThingType(item, ThingCategoryItem)
    end

    if not item then
        return nil, nil, nil
    end

    if item then
        local price = ItemsDatabase.getItemValue(item)
        local itemRarity = getColorForValue(price)
        if itemRarity then
            clip = clipfunction(price)
            if clip ~= "" then
                if frameOption == "frames" then
                    imagePath = "/images/ui/rarity_frames"
                elseif frameOption == "corners" then
                    imagePath = "/images/ui/containerslot-coloredges"
                end
            else
                clip = nil
            end
        end
    end

    local clipObject = nil
    if clip then
        local x, y, w, h = clip:match("(%d+) (%d+) (%d+) (%d+)")
        clipObject = { x = tonumber(x), y = tonumber(y), width = tonumber(w), height = tonumber(h) }
    end

    return clip, imagePath, clipObject
end

function ItemsDatabase.setRarityItem(widget, item, style)
    if not g_game.getFeature(GameColorizedLootValue) or not widget then
        return
    end

    local clip, imagePath = ItemsDatabase.getClipAndImagePath(item)

    if not imagePath then
        -- no frame applies (empty slot or frames disabled): clear a frame left by a previous item,
        -- but only if this widget is currently showing one, to avoid clobbering custom backgrounds
        local currentSource = widget:getImageSource()
        if currentSource == "/images/ui/rarity_frames" or currentSource == "/images/ui/containerslot-coloredges" then
            widget:setImageClip(nil)
            widget:setImageSource('/images/ui/item')
        end
        return
    end

    widget:setImageClip(clip)
    widget:setImageSource(imagePath)
    if style then
        widget:setStyle(style)
    end
end

function ItemsDatabase.getColorForRarity(rarity)
    return ItemsDatabase.rarityColors[rarity] or TextColors.white
end

function ItemsDatabase.setColorLootMessage(text)
    local function coloringLootName(match)
        local id, itemName = match:match("(%d+)|(.+)")
        if not id or not itemName then
            -- If pattern doesn't match itemId|itemName format, return the original match with braces
            return "{" .. match .. "}"
        end

        local itemId = tonumber(id)
        if not itemId then
            return itemName or match
        end

        local thingType = g_things.getThingType(itemId, ThingCategoryItem)
        if not thingType then
            return itemName
        end

        local itemInfo = ItemsDatabase.getItemValue(thingType)
        if itemInfo then
            local color = ItemsDatabase.getColorForRarity(getColorForValue(itemInfo))
            return "{" .. itemName .. ", " .. color .. "}"
        else
            return itemName
        end
    end
    return text:gsub("{(.-)}", coloringLootName)
end

function ItemsDatabase.getTierClip(tier)
    local xOffset = (math.min(math.max(tier, 1), 10) - 1) * 9
    return {
        x = xOffset,
        y = 0,
        width = 10,
        height = 9
    }
end

function ItemsDatabase.setTier(widget, item, isSmall)
    if not g_game.getFeature(GameThingUpgradeClassification) or not widget or not widget.tier then
        return
    end
    if isSmall == nil then
        isSmall = true
    end
    local tier = type(item) == "number" and item or (item and item:getTier()) or 0
    if tier <= 0 then
        widget.tier:setVisible(false)
        return
    end
    local config
    if isSmall then
        local normalizedTier = math.min(math.max(tier, 1), 10)
        config = {
            xOffset = (normalizedTier - 1) * 9,
            width = 10,
            height = 9,
            size = "10 9",
            source = '/images/inventory/tiers-strip'
        }
    else
        local normalizedTier = math.min(math.max(tier, 1), 18)
        local xOffset = (normalizedTier - 1) * 18 + 1
        config = {
            xOffset = xOffset,
            width = 18,
            height = 16,
            size = "18 16",
            source = '/images/inventory/tiers-strip-big'
        }
    end

    widget.tier:setImageClip({
        x = config.xOffset,
        y = 0,
        width = config.width,
        height = config.height
    })
    widget.tier:setSize(config.size)
    widget.tier:setImageSource(config.source)
    widget.tier:setImageSize(config.size)
    widget.tier:setVisible(true)
end


