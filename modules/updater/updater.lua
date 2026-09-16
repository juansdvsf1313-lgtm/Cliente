Updater = {}

Updater.maxRetries = 5

local updaterWindow
local loadModulesFunction
local scheduledEvent
local httpOperationId = 0

-- Descargas en vuelo (id -> true), para cancelarlas todas en abort().
local operaciones = {}

-- Resumen (CRC de todos los CRC) del pack HD que sirve el servidor. Comprobar
-- los ~5.100 ficheros del HD uno a uno son 673 MB leidos en cada arranque: en un
-- disco mecanico, casi un minuto. Se guarda el resumen tras cada comprobacion
-- buena y mientras el servidor mande el mismo no se vuelven a mirar.
local MARCA_HD = 'data/things/.hd-sum'
local hdSumServidor = nil

local function esFicheroHd(file)
  return string.find(file, '/data/things/', 1, true) ~= nil
     and string.find(file, '_hd/', 1, true) ~= nil
end

local function hayHdInstalado()
  local ok, dirs = pcall(g_resources.listDirectoryFiles, '/data/things/')
  if not ok or type(dirs) ~= 'table' then return false end
  for _, d in ipairs(dirs) do
    if tostring(d):match('_hd$') and g_resources.fileExists('/data/things/' .. d .. '/catalog-content.json') then
      return true
    end
  end
  return false
end

local function leerMarcaHd()
  if not g_resources.fileExists('/' .. MARCA_HD) then return nil end
  local ok, v = pcall(g_resources.readFileContents, '/' .. MARCA_HD)
  if not ok or type(v) ~= 'string' then return nil end
  return v:match('^%s*(.-)%s*$')
end

local function escribirMarcaHd()
  if not hdSumServidor or hdSumServidor == '' then return end
  pcall(function() g_resources.writeFileContentsToWorkDir(MARCA_HD, hdSumServidor) end)
end
-- Descargas simultaneas. Una a una, el pack HD (5.100 ficheros) pagaba una ida
-- y vuelta HTTP por fichero y tardaba el doble que el propio volumen de datos.
local CONCURRENTES = 4

-- Modo "solo HD": lo lanza game_things cuando el jugador activa HD y no lo
-- tiene. Solo se comparan y bajan los ficheros de data/things/<ver>_hd, no se
-- toca el binario ni se reinicia, y al terminar se avisa por alTerminarHd.
local soloHd = false
local alTerminarHd = nil
local hdCompletado = false

--- HD por el updater: se pide al servidor si el jugador tiene HD activado o
-- ya instalado (asi se mantiene al dia sin volver a bajar 673 MB).
function Updater.quiereHd()
  return g_settings.getBoolean('hdMode', false) or hayHdInstalado()
end

local function onLog(level, message, time)
  if level == LogError then
    Updater.error(message)
    g_logger.setOnLog(nil)
  end
end

local function loadModules()
  if loadModulesFunction then
    local tmpLoadFunc = loadModulesFunction
    loadModulesFunction = nil
    tmpLoadFunc()
  end
end

-- Descarga la lista con hasta CONCURRENTES peticiones a la vez. Cada fichero
-- reintenta por su cuenta hasta Updater.maxRetries; el primero que agota los
-- reintentos para todo con Updater.error.
local function downloadFiles(url, files, doneCallback)
  if not updaterWindow then return end
  local total = #files
  if total == 0 then return doneCallback() end

  local siguiente, completados, enVuelo, fallado = 1, 0, 0, false

  local function terminado()
    if updaterWindow and not fallado then doneCallback() end
  end

  local lanzar
  local function intentar(entry, retries)
    if not updaterWindow or fallado then return end
    local file, esperado = entry[1], entry[2]
    if retries > 0 then
      updaterWindow.downloadStatus:setText(tr("Downloading (%i retry):\n%s", retries, file))
    else
      updaterWindow.downloadStatus:setText(tr("Downloading:\n%s", file))
    end
    local id
    id = HTTP.download(url .. file, file,
      function(_, checksum, err)
        if id then operaciones[id] = nil end
        if not updaterWindow or fallado then return end
        if not err and checksum ~= esperado then
          err = "Invalid checksum of: " .. file .. ".\nShould be " .. esperado .. ", is: " .. checksum
        end
        if err then
          if retries >= Updater.maxRetries then
            fallado = true
            return Updater.error("Can't download file: " .. file .. ".\nError: " .. err)
          end
          scheduleEvent(function() intentar(entry, retries + 1) end, 250)
          return
        end
        completados = completados + 1
        enVuelo = enVuelo - 1
        updaterWindow.mainProgress:setPercent(math.floor(100 * completados / total))
        updaterWindow.status:setText(tr("Updating %i files", total) .. string.format('  (%d/%d)', completados, total))
        if completados >= total then return terminado() end
        lanzar()
      end,
      function(progress, speed)
        if updaterWindow then
          updaterWindow.downloadProgress:setPercent(progress)
          updaterWindow.downloadProgress:setText(speed .. " kbps")
        end
      end)
    if id then operaciones[id] = true end
  end

  lanzar = function()
    while updaterWindow and not fallado and enVuelo < CONCURRENTES and siguiente <= total do
      local entry = files[siguiente]
      siguiente = siguiente + 1
      enVuelo = enVuelo + 1
      intentar(entry, 0)
    end
  end

  updaterWindow.downloadProgress:setPercent(0)
  lanzar()
end

-- Solo se comprueban los archivos que manda el servidor (data.files), no todo
-- el disco. g_resources.filesChecksums() listaba tambien los ~12.000 sprites
-- de data/things (SD y HD), preguntando uno a uno si eran carpeta, y aunque
-- luego los descartaba, esa enumeracion era la que dejaba la ventana congelada
-- en 'Comprobando archivos locales...' sin barra y sin poder cancelar.
-- Se hace por tandas de TANDA_MS entre frames: la barra avanza y Cancel responde.
-- Mismo CRC32 y mismo formato que filesChecksums (g_crypt.crc32 sin mayusculas).
local TANDA_MS = 30

local function compararArchivos(data, alTerminar)
  local lista = {}
  for file, checksum in pairs(data.files) do
    lista[#lista + 1] = { file, checksum }
  end

  local total = #lista
  local toUpdate, toUpdateFiles = {}, {}
  local indice = 1
  local inicio = g_clock.millis()

  local function tanda()
    if not updaterWindow then return end

    local limite = g_clock.millis() + TANDA_MS
    while indice <= total and g_clock.millis() < limite do
      local file, checksum = lista[indice][1], lista[indice][2]
      local igual = false
      if g_resources.fileExists(file) then
        -- fileChecksum lee los bytes TAL CUAL (en C++ y con cache). Con
        -- readFileContents no valia: ese descifra, y en el cliente cifrado su
        -- CRC no coincide nunca con el del servidor, asi que el updater se
        -- habria vuelto a descargar el cliente entero en cada arranque.
        local ok, suma = pcall(g_resources.fileChecksum, file)
        igual = ok and type(suma) == 'string' and suma == checksum
      end
      if not igual then
        table.insert(toUpdate, { file, checksum })
        table.insert(toUpdateFiles, file)
      end
      indice = indice + 1
    end

    local porcentaje = total > 0 and math.floor((indice - 1) * 100 / total) or 100
    updaterWindow.mainProgress:setPercent(porcentaje)
    updaterWindow.status:setText(tr('Comprobando archivos locales... %d%%', porcentaje))

    if indice <= total then
      scheduledEvent = scheduleEvent(tanda, 1)
      return
    end

    g_logger.info(string.format('[updater] %d archivos comprobados en %.1f s; %d por actualizar',
      total, (g_clock.millis() - inicio) / 1000, #toUpdate))
    alTerminar(toUpdate, toUpdateFiles)
  end

  tanda()
end

local function updateFilesPaso2(data, toUpdate, toUpdateFiles)
  local newFiles = #toUpdateFiles > 0

  -- update binary
  local binary = nil
  if type(data.binary) == "table" and data.binary.file:len() > 1 then
    local selfChecksum = g_resources.selfChecksum()
    if selfChecksum:len() > 0 and selfChecksum ~= data.binary.checksum then
      binary = data.binary.file
      table.insert(toUpdate, { binary, data.binary.checksum })
    end
  end

  if #toUpdate == 0 then -- nothing to update
    -- Ya estaba todo bien: cuenta como descarga de HD terminada (si no, quien
    -- pidio el HD recibiria un "no termino" con los ficheros ya en su sitio).
    hdCompletado = true
    escribirMarcaHd()
    updaterWindow.mainProgress:setPercent(100)
    scheduledEvent = scheduleEvent(Updater.abort, 20)
    return
  end

  -- update of some files require full client restart
  local forceRestart = false
  local reloadModules = false
  local forceRestartPattern = { "init.lua", "corelib", "updater", "otmod" }
  for _, file in ipairs(toUpdate) do
    for __, pattern in ipairs(forceRestartPattern) do
      if string.find(file[1], pattern) then
        forceRestart = true
      end
      if not string.find(file[1], "data/things") then
        reloadModules = true
      end
    end
  end

  updaterWindow.status:setText(tr("Updating %i files", #toUpdate))
  updaterWindow.mainProgress:setPercent(0)
  updaterWindow.downloadProgress:setPercent(0)
  updaterWindow.downloadProgress:show()
  updaterWindow.downloadStatus:show()
  updaterWindow.changeUrlButton:hide()

  downloadFiles(data["url"], toUpdate, function()
    hdCompletado = true
    updaterWindow.status:setText(tr("Updating client (may take few seconds)"))
    updaterWindow.mainProgress:setPercent(100)
    updaterWindow.downloadProgress:hide()
    updaterWindow.downloadStatus:hide()
    scheduledEvent = scheduleEvent(function()
      local restart = binary or (not loadModulesFunction and reloadModules) or forceRestart
      if newFiles then
        g_resources.updateFiles(toUpdateFiles, not restart)
      end

      -- Todo lo de la lista quedo al dia: se apunta el resumen del pack HD para
      -- poder saltarse su comprobacion en los siguientes arranques.
      escribirMarcaHd()

      if binary then
        g_resources.updateExecutable(binary)
      end

      if restart then
        g_app.restart()
      else
        if reloadModules then
          g_textures.clearCache()
          g_modules.reloadModules()
        end
        Updater.abort()
      end
    end, 100)
  end)
end

local function updateFiles(data, keepCurrentFiles)
  if not updaterWindow then return end

  if type(data) ~= "table" then
    return Updater.error("Invalid data from updater api (not table)")
  end

  if type(data.error) == 'string' and data.error:len() > 0 then
    return Updater.error(data.error)
  end

  if not data.files or type(data.url) ~= 'string' or data.url:len() < 4 then
    return Updater.error("Invalid data from updater api: " .. json.encode(data, 2))
  end

  if data.keepFiles then
    keepCurrentFiles = true
  end

  -- Pack HD al dia: el servidor manda un resumen de los CRC de todo el pack. Si
  -- es el mismo que la ultima vez y los assets siguen estando, no se comprueban
  -- uno a uno (serian 673 MB de lectura en cada arranque).
  hdSumServidor = (type(data.hdSum) == 'string' and data.hdSum ~= '') and data.hdSum or nil
  if hdSumServidor and hdSumServidor == leerMarcaHd() and hayHdInstalado() then
    local quitados = 0
    for file in pairs(data.files) do
      if esFicheroHd(file) then
        data.files[file] = nil
        quitados = quitados + 1
      end
    end
    if quitados > 0 then
      g_logger.info(string.format('[updater] pack HD al dia (%s): %d ficheros sin comprobar', hdSumServidor, quitados))
    end
  end

  -- Modo solo HD: quedarse con los ficheros del pack HD y no tocar el binario
  -- (nada de reiniciar en mitad de la partida).
  if soloHd then
    local hd = {}
    for file, checksum in pairs(data.files) do
      if string.find(file, "/data/things/") and string.find(file, "_hd/") then
        hd[file] = checksum
      end
    end
    data.files = hd
    data.binary = nil
  end

  -- Se avisa y se cede un frame para que el mensaje llegue a pintarse antes
  -- de bloquear. No hay forma de forzar un repintado sincrono desde Lua.
  updaterWindow.status:setText(tr('Comprobando archivos locales...'))
  updaterWindow.mainProgress:setPercent(0)
  scheduledEvent = scheduleEvent(function()
    if not updaterWindow then return end
    compararArchivos(data, function(toUpdate, toUpdateFiles)
      updateFilesPaso2(data, toUpdate, toUpdateFiles)
    end)
  end, 50)
end

-- public functions
function Updater.init(loadModulesFunc)
  g_logger.setOnLog(onLog)
  loadModulesFunction = loadModulesFunc
  Updater.check()
end

function Updater.terminate()
  loadModulesFunction = nil
  Updater.abort(true)
end

function Updater.abort(terminate)
  HTTP.cancel(httpOperationId)
  for id in pairs(operaciones) do
    HTTP.cancel(id)
  end
  operaciones = {}
  removeEvent(scheduledEvent)
  if updaterWindow then
    updaterWindow:destroy()
    updaterWindow = nil
  end
  loadModules()
  if soloHd then
    -- Descarga de HD lanzada desde el juego: se avisa a quien la pidio en vez de
    -- disparar onUpdateFinished (eso es solo para el arranque).
    local cb, ok = alTerminarHd, hdCompletado
    soloHd, alTerminarHd, hdCompletado = false, nil, false
    if cb then pcall(cb, ok, ok and nil or 'La descarga de los graficos HD no termino.') end
    return
  end
  if not terminate then
    signalcall(g_app.onUpdateFinished, g_app)
  end
end

--- Descarga (o pone al dia) el pack HD desde el juego, sin reiniciar.
-- callback(ok, mensajeDeError)
function Updater.descargarHd(callback)
  if updaterWindow then
    return callback(false, 'El actualizador ya esta ocupado.')
  end
  if not Services or not Services.updater or Services.updater == '' then
    return callback(false, 'Este cliente no tiene servidor de actualizaciones configurado.')
  end
  soloHd = true
  alTerminarHd = callback
  hdCompletado = false
  Updater.check({ soloHd = true })
end

function Updater.check(args)
  if updaterWindow then return end

  updaterWindow = g_ui.displayUI('updater')
  updaterWindow:show()
  updaterWindow:focus()
  updaterWindow:raise()

  local updateData = nil
  local function progressUpdater(value)
    removeEvent(scheduledEvent)
    if value == 100 then
      return Updater.error(tr("Timeout"))
    end
    if updateData and (value > 60 or (not g_platform.isMobile() or not ALLOW_CUSTOM_SERVERS or not loadModulesFunc)) then -- gives 3s to set custom updater for mobile version
      return updateFiles(updateData)
    end
    scheduledEvent = scheduleEvent(function() progressUpdater(value + 1) end, 100)
    updaterWindow.mainProgress:setPercent(value)
  end
  progressUpdater(0)

  httpOperationId = HTTP.postJSON(Services.updater, {
    version = g_app.getBuildRevision(),
    build = g_app.getVersion(),
    os = g_app.getOs(),
    platform = g_window.getPlatformType(),
    args = args or {},
    -- Este updater compara leyendo solo los archivos de la lista, asi que puede
    -- recibir tambien los sprites SD de data/things. El viejo no: sacaba los
    -- checksums de filesChecksums(), que se salta data/things. updater.php solo
    -- los incluye si se lo pedimos.
    things = 1,
    -- Pack HD (data/things/<ver>_hd) por el mismo camino: solo si el jugador lo
    -- usa o lo tiene instalado (o si nos han pedido bajarlo ahora).
    hd = (soloHd or Updater.quiereHd()) and 1 or 0
  }, function(data, err)
    if err then
      return Updater.error(err)
    end
    updateData = data
  end)
end

function Updater.error(message)
  removeEvent(scheduledEvent)
  if not updaterWindow then return end
  displayErrorBox(tr("Updater Error"), message).onOk = function()
    Updater.abort()
  end
end
