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

#pragma once

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <framework/graphics/declarations.h>
#include <framework/luaengine/luaobject.h>

enum class SpriteLayout
{
    // default sheet sizes
    SIZE_32_32 = 0,
    SIZE_32_64 = 1,
    SIZE_64_32 = 2,
    SIZE_64_64 = 3,

    // extended sheet sizes (all possible combinations within 384x384 spritesheet)
    SIZE_32_96  = 4,
    SIZE_32_128 = 5,
    SIZE_32_192 = 6,
    SIZE_32_384 = 7,
    SIZE_64_96  = 8,
    SIZE_64_128 = 9,
    SIZE_64_192 = 10,
    SIZE_64_384 = 11,
    SIZE_96_32  = 12,
    SIZE_96_64  = 13,
    SIZE_96_96  = 14,
    SIZE_96_128 = 15,
    SIZE_96_192 = 16,
    SIZE_96_384 = 17,
    SIZE_128_32  = 18,
    SIZE_128_64  = 19,
    SIZE_128_96  = 20,
    SIZE_128_128 = 21,
    SIZE_128_192 = 22,
    SIZE_128_384 = 23,
    SIZE_192_32  = 24,
    SIZE_192_64  = 25,
    SIZE_192_96  = 26,
    SIZE_192_128 = 27,
    SIZE_192_192 = 28,
    SIZE_192_384 = 29,
    SIZE_384_32  = 30,
    SIZE_384_64  = 31,
    SIZE_384_96  = 32,
    SIZE_384_128 = 33,
    SIZE_384_192 = 34,
    SIZE_384_384 = 35
};

enum class SpriteLoadState
{
    NONE,
    LOADING,
    LOADED
};

class SpriteSheet
{
public:
    // Lado de la hoja del pack cargado. NO es constante: 384 en SD y 768 en el
    // pack HD reempaquetado. Lo fija loadAppearances() leyendo el catalogo,
    // antes de construir ninguna hoja, y no vuelve a cambiar mientras ese pack
    // este cargado. Por eso puede ser un estatico suelto y todas las cuentas de
    // abajo siguen saliendo solas.
    static inline uint16_t SIZE = 384;

    static void setSheetSize(const uint16_t size) { SIZE = size; }

    // Derivados del lado. Antes eran macros de config.h con el 384 dentro.
    static uint32_t widthBytes() { return static_cast<uint32_t>(SIZE) * 4; }
    static uint32_t bytesInSheet() { return static_cast<uint32_t>(SIZE) * SIZE * 4; }

    SpriteSheet(const int firstId, const int lastId, const SpriteLayout spriteLayout, std::string file) : firstId(firstId), lastId(lastId), spriteLayout(spriteLayout), file(std::move(
        file))
    {
    }

    Size getSpriteSize() const;

    int getSpritesPerSheet() const;

    // 64 pixel width == 6 columns each 64x or 32 pixels, 12 columns
    int getColumns() const { return SIZE / getSpriteSize().width(); }

    int firstId = 0;
    int lastId = 0;

    SpriteLayout spriteLayout = SpriteLayout::SIZE_32_32;
    std::atomic<SpriteLoadState> m_loadingState = SpriteLoadState::NONE;

    // Hoja decodificada (BGRA ya volteada). Leer o soltar SOLO con m_candado:
    // el recolector la libera cuando lleva tiempo sin usarse (ver
    // SpriteAppearances::liberarDecodificadas), asi que un puntero suelto puede
    // morir debajo de quien lo lea. LOADED implica que data no es nula.
    std::unique_ptr<uint8_t[]> data;
    mutable std::shared_mutex m_candado;

    // El fichero .lzma tal cual, leido del disco UNA sola vez. Es pequeno (unos
    // 130 KB por hoja HD) y evita volver al disco cuando la decodificada se ha
    // liberado. Se rellena bajo m_candadoFichero y ya no cambia.
    std::vector<uint8_t> comprimido;
    std::mutex m_candadoFichero;

    std::atomic<int64_t> ultimoUso{ 0 }; // stdext::millis() de la ultima lectura de sprites
    int indice{ -1 };                     // posicion en m_sheets tras sortSheets(): para leer vecinas
    std::string file;

    bool estaDecodificada() const { return m_loadingState.load(std::memory_order_acquire) == SpriteLoadState::LOADED; }
};

//@bindsingleton g_spriteAppearances
class SpriteAppearances
{
public:
    void init();
    void terminate();

    void unload();

    void setSpritesCount(const int count) { m_spritesCount = count; }
    int getSpritesCount() const { return m_spritesCount; }

    void setPath(const std::string& path) { m_path = path; }
    std::string getPath() const { return m_path; }

    bool loadSpriteSheet(const SpriteSheetPtr& sheet) const;

    // Solo lee el fichero a memoria (sin decodificar). Lo usa el cargador de
    // precarga para las hojas vecinas en sus ratos libres.
    bool leerComprimido(const SpriteSheetPtr& sheet) const;

    // El hilo que lo active pasa por delante de los demas al leer hojas del
    // disco (lo usa el camino sincrono de los suelos en el hilo del mapa).
    static void setLecturaPrioritaria(bool prioritaria);

    // Libera las hojas decodificadas menos usadas cuando se pasa del tope.
    // Lo llama GarbageCollection cada pocos segundos.
    void liberarDecodificadas();
    static int64_t bytesDecodificados();

    SpriteSheetPtr getSheetByIndex(const int i) const {
        return (i >= 0 && i < static_cast<int>(m_sheets.size())) ? m_sheets[i] : nullptr;
    }
    void saveSheetToFileBySprite(int id, const std::string& file);
    void saveSheetToFile(const SpriteSheetPtr& sheet, const std::string& file);
    SpriteSheetPtr getSheetBySpriteId(int id, bool load = true) {
        bool isLoading = false;
        return getSheetBySpriteId(id, isLoading, load);
    }
    SpriteSheetPtr getSheetBySpriteId(int id, bool& isLoading, bool load = true);

    void addSpriteSheet(const SpriteSheetPtr& sheet) { m_sheets.emplace_back(sheet); }

    // Deja las hojas ordenadas por su primer id para poder buscarlas por
    // biseccion en getSheetBySpriteId, que se llama una vez por cada sprite.
    void sortSheets() {
        std::ranges::sort(m_sheets, [](const SpriteSheetPtr& a, const SpriteSheetPtr& b) {
            return a->firstId < b->firstId;
        });
        for (int i = 0; i < static_cast<int>(m_sheets.size()); ++i)
            m_sheets[i]->indice = i;
    }

    ImagePtr getSpriteImage(int id) {
        bool isLoading = false;
        return getSpriteImage(id, isLoading);
    }
    ImagePtr getSpriteImage(int id, bool& isLoading);
    void saveSpriteToFile(int id, const std::string& file);

private:
    uint32_t m_spritesCount{ 0 };
    std::vector<SpriteSheetPtr> m_sheets;
    std::string m_path;
};

extern SpriteAppearances g_spriteAppearances;
