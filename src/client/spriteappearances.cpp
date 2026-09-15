/*
 * Copyright (c) 2022 Nekiro <https://github.com/nekiro>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "spriteappearances.h"
#include <framework/util/stats.h>

#include <nlohmann/json_fwd.hpp>
#include "lzma.h"
#include "gameconfig.h"
#include "framework/core/filestream.h"
#include "framework/core/resourcemanager.h"
#include "framework/graphics/image.h"
#include "framework/stdext/time.h"

 // warnings related to protobuf
    // https://android.googlesource.com/platform/external/protobuf/+/brillo-m9-dev/vsprojects/readme.txt

using json = nlohmann::json;

SpriteAppearances g_spriteAppearances;

void SpriteAppearances::init()
{
    // in tibia 12.81 there is currently 3482 sheets
    m_sheets.reserve(24000);   // 5.084 hojas en SD, 20.315 en HD
}

void SpriteAppearances::terminate()
{
    unload();
}

Size SpriteSheet::getSpriteSize() const
{    
    // this array includes all possible combinations within 384x384 sheet
    // if you intend to change that, you will also have to modify the assets editor
    // CHANGING THIS MAY BREAK READING EXISTING SPRITESHEETS

    // tile sizes in spritesheets, see SpriteLayout for array key definitions
    static const std::array<Size, 36> sizes = {
        Size(32,32),  // 0
        Size(32,64),  // 1
        Size(64,32),  // 2
        Size(64,64),  // 3
        Size(32,96),  // 4
        Size(32,128), // 5
        Size(32,192), // 6
        Size(32,384), // 7
        Size(64,96),  // 8
        Size(64,128), // 9
        Size(64,192), // 10
        Size(64,384), // 11
        Size(96,32),  // 12
        Size(96,64),  // 13
        Size(96,96),  // 14
        Size(96,128), // 15
        Size(96,192), // 16
        Size(96,384), // 17
        Size(128,32),  // 18
        Size(128,64),  // 19
        Size(128,96),  // 20
        Size(128,128), // 21
        Size(128,192), // 22
        Size(128,384), // 23
        Size(192,32),  // 24
        Size(192,64),  // 25
        Size(192,96),  // 26
        Size(192,128), // 27
        Size(192,192), // 28
        Size(192,384), // 29
        Size(384,32),  // 30
        Size(384,64),  // 31
        Size(384,96),  // 32
        Size(384,128), // 33
        Size(384,192), // 34
        Size(384,384)  // 35
    };

    const size_t idx = static_cast<size_t>(spriteLayout);
    if (idx < sizes.size())
        return sizes[idx];

    return sizes[0];
}

int SpriteSheet::getSpritesPerSheet() const
{
    const Size& size = getSpriteSize();
    const int spritesPerColumn = SpriteSheet::SIZE / size.height();

    return getColumns() * spritesPerColumn;
}

// Bytes de hojas decodificadas que hay ahora mismo en RAM (todas las hojas).
static std::atomic<int64_t> s_bytesDecodificados{ 0 };

int64_t SpriteAppearances::bytesDecodificados() { return s_bytesDecodificados.load(std::memory_order_relaxed); }

// UNA sola lectura de hoja del disco a la vez en todo el cliente, lea quien lea:
// el hilo del mapa (suelos), el lector de precarga o los hilos del pool que
// componen outfits (hasta 11 a la vez). Con todos leyendo a la vez de un disco
// mecanico cada lectura pasaba de 10-20 ms a 350-550 ms (medido), y el hilo del
// mapa se quedaba detras de todos: fotograma de 3 s al cambiar de zona. El hilo
// del mapa tiene prioridad: mientras este esperando, los demas le ceden el turno.
static std::mutex s_discoHojas;
static std::atomic_int s_prioritariosEsperando{ 0 };
static thread_local bool t_lecturaPrioritaria = false;

void SpriteAppearances::setLecturaPrioritaria(const bool prioritaria) { t_lecturaPrioritaria = prioritaria; }

static void cogerTurnoDeDisco()
{
    if (t_lecturaPrioritaria) {
        s_prioritariosEsperando.fetch_add(1, std::memory_order_acq_rel);
        s_discoHojas.lock();
        s_prioritariosEsperando.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    while (true) {
        while (s_prioritariosEsperando.load(std::memory_order_acquire) > 0)
            stdext::millisleep(1);
        s_discoHojas.lock();
        if (s_prioritariosEsperando.load(std::memory_order_acquire) == 0)
            return;
        // llego un prioritario mientras cogiamos el candado: se le cede
        s_discoHojas.unlock();
        stdext::millisleep(1);
    }
}

bool SpriteAppearances::leerComprimido(const SpriteSheetPtr& sheet) const
{
    std::lock_guard<std::mutex> lock(sheet->m_candadoFichero);
    if (!sheet->comprimido.empty())
        return true;

    // Orden de candados: primero el de la hoja, luego el del disco. Dentro del
    // tramo de disco no se coge ningun candado de hoja, asi que no hay abrazo.
    cogerTurnoDeDisco();
    std::lock_guard<std::mutex> lockDisco(s_discoHojas, std::adopt_lock);

    // Separado de DescomprimirHoja en el trazador: esto es DISCO (en uno mecanico,
    // 10-70 ms por fichero), lo otro es CPU (~8 ms por hoja HD).
    AutoStat medida(STATS_GENERAL, "LeerHoja", sheet->file);
    const auto& path = fmt::format("{}{}", g_spriteAppearances.getPath(), sheet->file);
    if (!g_resources.fileExists(path))
        return false;

    const auto& fin = g_resources.openFile(path);
    fin->cache(true); // deja el fichero entero en fin->m_data
    if (fin->m_data.empty())
        return false;

    sheet->comprimido = fin->m_data;
    return true;
}

bool SpriteAppearances::loadSpriteSheet(const SpriteSheetPtr& sheet) const
{
    const auto estado = sheet->m_loadingState.load(std::memory_order_acquire);
    if (estado == SpriteLoadState::LOADING)
        return false;
    if (estado == SpriteLoadState::LOADED)
        return true;

    const auto previo = sheet->m_loadingState.exchange(SpriteLoadState::LOADING, std::memory_order_acq_rel);
    if (previo == SpriteLoadState::LOADING)
        return false;
    if (previo == SpriteLoadState::LOADED) {
        // otro hilo la termino entre la lectura de arriba y el exchange
        sheet->m_loadingState.store(SpriteLoadState::LOADED, std::memory_order_release);
        return true;
    }

    try {
        if (!leerComprimido(sheet)) {
            sheet->m_loadingState.store(SpriteLoadState::NONE, std::memory_order_release);
            return false;
        }

        // Medida para el trazador de fotogramas lentos (g_stats.getSlow): una hoja
        // HD de 768 son ~8 ms de LZMA, y si toca en el hilo del mapa es un tiron.
        AutoStat medida(STATS_GENERAL, "DescomprimirHoja", sheet->file);

        // Se lee de los bytes en RAM. Una vez rellenos no cambian, asi que no
        // hace falta candado para leerlos.
        const std::vector<uint8_t>& buf = sheet->comprimido;
        size_t pos = 0;
        const auto u8 = [&]() -> uint8_t {
            if (pos >= buf.size())
                throw stdext::exception("cabecera de la hoja truncada");
            return buf[pos++];
        };

        // Dimensionado al maximo (768) porque es thread_local y no se puede
        // redimensionar por pack. Lo que SI depende del pack es cuanto se le
        // ofrece al descompresor, mas abajo.
        thread_local static std::array<uint8_t, LZMA_UNCOMPRESSED_SIZE_MAX> decompressBuffer;

        // Tamano real de este pack. LZMA1 crudo no lleva marca de fin: el
        // descompresor solo devuelve LZMA_STREAM_END si el flujo llena
        // exactamente lo que se le ofrece. Si aqui se pasara el tamano del
        // buffer (768) en vez del de la hoja, un pack SD nunca terminaria.
        const uint32_t uncompressedSize = SpriteSheet::bytesInSheet() + 122;

        /*
           CIP's header, always 32 (0x20) bytes.
           Header format:
           [0x00, X):          A variable number of NULL (0x00) bytes. The amount of pad-bytes can vary depending on how many
                               bytes the "7-bit integer encoded LZMA file size" take.
           [X, X + 0x05):      The constant byte sequence [0x70 0x0A 0xFA 0x80 0x24]
           [X + 0x05, 0x20]:   LZMA file size (Note: excluding the 32 bytes of this header) encoded as a 7-bit integer
       */

        while (u8() == 0x00);
        pos += 4;
        while ((u8() & 0x80) == 0x80);

        const uint8_t lclppb = u8();

        lzma_options_lzma options{};
        options.lc = lclppb % 9;

        const int remainder = lclppb / 9;
        options.lp = remainder % 5;
        options.pb = remainder / 5;

        uint32_t dictionarySize = 0;
        for (uint8_t i = 0; i < 4; ++i) {
            dictionarySize += u8() << (i * 8);
        }

        options.dict_size = dictionarySize;

        pos += 8; // cip compressed size
        if (pos >= buf.size())
            throw stdext::exception("hoja sin datos tras la cabecera");

        lzma_stream stream = LZMA_STREAM_INIT;

        const lzma_filter filters[2] = {
            lzma_filter{LZMA_FILTER_LZMA1, &options},
            lzma_filter{LZMA_VLI_UNKNOWN, nullptr}
        };

        lzma_ret ret = lzma_raw_decoder(&stream, filters);
        if (ret != LZMA_OK) {
            throw stdext::exception(fmt::format("failed to initialize lzma raw decoder result: {}", ret));
        }

        stream.next_in = buf.data() + pos;
        stream.avail_in = buf.size() - pos;
        stream.next_out = decompressBuffer.data();
        stream.avail_out = uncompressedSize;

        const auto result = lzma_code(&stream, LZMA_RUN);
        lzma_end(&stream);

        if (result != LZMA_STREAM_END)
            throw stdext::exception("LZMA decompression failed");

        // pixel offset
        const uint8_t* bmpOffsetPtr = decompressBuffer.data() + 10;
        const uint32_t bmpDataOffset =
            bmpOffsetPtr[0] |
            (bmpOffsetPtr[1] << 8) |
            (bmpOffsetPtr[2] << 16) |
            (bmpOffsetPtr[3] << 24);

        const uint32_t bytesInSheet = SpriteSheet::bytesInSheet();
        const uint32_t widthBytes = SpriteSheet::widthBytes();

        // validate offset
        if (bmpDataOffset + bytesInSheet > decompressBuffer.size())
            throw stdext::exception("sprite sheet image offset out of bounds");

        uint8_t* bufferStart = decompressBuffer.data() + bmpDataOffset;

        // swap BGR ? RGB and fix magenta
        for (uint32_t i = 0; i < bytesInSheet; i += 4) {
            std::swap(bufferStart[i], bufferStart[i + 2]); // B <-> R

            const uint32_t rgb = bufferStart[i] | (bufferStart[i + 1] << 8) | (bufferStart[i + 2] << 16);
            if (rgb == 0xFF00FF) {
                bufferStart[i + 0] = 0x00;
                bufferStart[i + 1] = 0x00;
                bufferStart[i + 2] = 0x00;
                bufferStart[i + 3] = 0x00;
            }
        }

        // vertical flip
        const int halfHeight = SpriteSheet::SIZE / 2;
        uint8_t tempLine[SPRITE_SHEET_MAX_WIDTH_BYTES];
        for (int y = 0; y < halfHeight; ++y) {
            uint8_t* top = bufferStart + y * widthBytes;
            uint8_t* bottom = bufferStart + (SpriteSheet::SIZE - 1 - y) * widthBytes;

            std::memcpy(tempLine, top, widthBytes);
            std::memcpy(top, bottom, widthBytes);
            std::memcpy(bottom, tempLine, widthBytes);
        }

        auto datos = std::make_unique<uint8_t[]>(bytesInSheet);
        std::memcpy(datos.get(), bufferStart, bytesInSheet);
        {
            std::unique_lock<std::shared_mutex> lock(sheet->m_candado);
            sheet->data = std::move(datos);
        }
        s_bytesDecodificados.fetch_add(bytesInSheet, std::memory_order_relaxed);
        sheet->ultimoUso.store(stdext::millis(), std::memory_order_relaxed);

        sheet->m_loadingState.store(SpriteLoadState::LOADED, std::memory_order_release);
        return true;
    } catch (const std::exception& e) {
        sheet->m_loadingState.store(SpriteLoadState::NONE, std::memory_order_release);
        g_logger.error("Failed to load single sprite sheet '{}': {}", sheet->file, e.what());
        return false;
    }
}

void SpriteAppearances::unload()
{
    m_spritesCount = 0;
    m_sheets.clear();
    s_bytesDecodificados.store(0, std::memory_order_relaxed);
}

// Tope de hojas decodificadas en RAM. Una hoja HD son 2,36 MB y antes no se
// liberaba ninguna: medido, 3,5 GB privados a los 6 minutos de pasear por zonas
// nuevas. Con el tope, las que llevan mas tiempo sin usarse se sueltan y, si
// vuelven a hacer falta, se decodifican otra vez desde los bytes comprimidos que
// siguen en RAM (~8 ms en un hilo del pool, sin tocar el disco).
static constexpr int64_t TOPE_DECODIFICADAS = int64_t(512) << 20;
static constexpr int64_t MARGEN_LIBERACION = int64_t(64) << 20;  // se baja hasta 64 MB por debajo del tope
static constexpr int64_t GRACIA_MS = 5000;                        // lo usado hace menos de 5 s no se toca

void SpriteAppearances::liberarDecodificadas()
{
    if (s_bytesDecodificados.load(std::memory_order_relaxed) <= TOPE_DECODIFICADAS)
        return;

    const int64_t ahora = stdext::millis();
    std::vector<std::pair<int64_t, SpriteSheet*>> candidatas;
    for (const auto& hoja : m_sheets) {
        if (!hoja->estaDecodificada())
            continue;
        const int64_t uso = hoja->ultimoUso.load(std::memory_order_relaxed);
        if (ahora - uso > GRACIA_MS)
            candidatas.emplace_back(uso, hoja.get());
    }
    std::ranges::sort(candidatas, {}, &std::pair<int64_t, SpriteSheet*>::first);

    const int64_t objetivo = TOPE_DECODIFICADAS - MARGEN_LIBERACION;
    for (auto& [uso, hoja] : candidatas) {
        if (s_bytesDecodificados.load(std::memory_order_relaxed) <= objetivo)
            break;
        // Si alguien la esta leyendo ahora mismo se deja para la siguiente pasada.
        std::unique_lock<std::shared_mutex> lock(hoja->m_candado, std::try_to_lock);
        if (!lock.owns_lock())
            continue;
        if (hoja->m_loadingState.load(std::memory_order_acquire) != SpriteLoadState::LOADED || !hoja->data)
            continue;
        // NONE antes de soltar: quien la pida la vuelve a cargar. Con el candado
        // exclusivo cogido nadie puede estar leyendo el puntero.
        hoja->m_loadingState.store(SpriteLoadState::NONE, std::memory_order_release);
        hoja->data.reset();
        s_bytesDecodificados.fetch_sub(SpriteSheet::bytesInSheet(), std::memory_order_relaxed);
    }
}

SpriteSheetPtr SpriteAppearances::getSheetBySpriteId(const int id, bool& isLoading, const bool load /* = true */)
{
    if (id == 0) {
        return nullptr;
    }

    // Busqueda por biseccion sobre las hojas ordenadas por firstId.
    //
    // Antes recorria TODAS las hojas en cada consulta de sprite. En SD son 5.084;
    // en HD son 20.315, y esto se llama una vez por cada sprite de cada textura:
    // para un outfit de 120 fotogramas salian millones de comparaciones.
    const auto sheetIt = std::ranges::upper_bound(m_sheets, id, {},
        [](const SpriteSheetPtr& s) { return s->firstId; });

    if (sheetIt == m_sheets.begin())
        return nullptr;

    const auto& sheet = *std::prev(sheetIt);
    if (id < sheet->firstId || id > sheet->lastId)
        return nullptr;

    if (load && !loadSpriteSheet(sheet)) {
        isLoading = sheet->m_loadingState == SpriteLoadState::LOADING;
        return nullptr;
    }

    return sheet;
}

ImagePtr SpriteAppearances::getSpriteImage(const int id, bool& isLoading)
{
    try {
        const auto& sheet = getSheetBySpriteId(id, isLoading, true);
        if (!sheet) {
            return nullptr;
        }

        // Candado compartido mientras se copian los pixeles: el recolector solo
        // libera la hoja con el candado exclusivo.
        std::shared_lock<std::shared_mutex> candado(sheet->m_candado);
        if (!sheet->data) {
            // liberada entre la comprobacion de estado y el candado: se tratara
            // como "cargando" y quien la pida la volvera a pedir
            isLoading = true;
            return nullptr;
        }
        sheet->ultimoUso.store(stdext::millis(), std::memory_order_relaxed);
        const uint8_t* datos = sheet->data.get();

        const Size& size = sheet->getSpriteSize();

        const auto& image = std::make_shared<Image>(size);
        uint8_t* pixelData = image->getPixelData();

        const int spriteOffset = id - sheet->firstId;
        const int allColumns = sheet->getColumns();
        const int spritesPerSheet = sheet->getSpritesPerSheet();

        if (spriteOffset < 0 || spriteOffset >= spritesPerSheet) {
            g_logger.error("Sprite id {} is out of bounds for sheet {} (offset {}, max {})", id, sheet->file, spriteOffset, spritesPerSheet);
            return nullptr;
        }
        const int spriteRow = std::floor(static_cast<float>(spriteOffset) / static_cast<float>(allColumns));
        const int spriteColumn = spriteOffset % allColumns;

        const int spriteWidthBytes = size.width() * 4;

        for (int height = size.height() * spriteRow, offset = 0; height < size.height() + (spriteRow * size.height()); height++, offset++) {
            std::memcpy(&pixelData[offset * spriteWidthBytes], &datos[(height * SpriteSheet::widthBytes()) + (spriteColumn * spriteWidthBytes)], spriteWidthBytes);
        }

        if (!image->hasTransparentPixel()) {
            // The image must be more than 4 pixels transparent to be considered transparent.
            uint8_t cntTrans = 0;
            const auto& buf = image->getPixels();
            for (size_t i = 3, n = buf.size(); i < n; i += 4) {
                if (buf[i] == 0x00 && ++cntTrans > 4) {
                    image->setTransparentPixel(true);
                    break;
                }
            }
        }

        return image;
    } catch (const stdext::exception& e) {
        g_logger.error("Failed to get sprite id {}: {}", id, e.what());
        return nullptr;
    }
}

void SpriteAppearances::saveSpriteToFile(const int id, const std::string& file)
{
    if (const auto& sprite = getSpriteImage(id)) {
        sprite->savePNG(file);
    }
}

void SpriteAppearances::saveSheetToFileBySprite(const int id, const std::string& file)
{
    if (const auto& sheet = getSheetBySpriteId(id)) {
        saveSheetToFile(sheet, file);
    }
}

void SpriteAppearances::saveSheetToFile(const SpriteSheetPtr& sheet, const std::string& file)
{
    std::shared_lock<std::shared_mutex> candado(sheet->m_candado);
    if (!sheet->data)
        return;
    Image image({ SpriteSheet::SIZE }, 4, sheet->data.get());
    image.savePNG(file);
}