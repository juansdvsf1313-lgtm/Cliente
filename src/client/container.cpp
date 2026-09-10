/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
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

#include "container.h"

#include <algorithm>

#include "item.h"

ItemPtr Container::getItem(const int slot)
{
    if (slot < 0 || slot >= static_cast<int>(m_items.size()))
        return nullptr;
    return m_items[slot];
}

void Container::onOpen(const ContainerPtr& previousContainer)
{
    callLuaField("onOpen", previousContainer);
}

void Container::onClose()
{
    m_closed = true;
    callLuaField("onClose");
}

void Container::onAddItem(const ItemPtr& item, int slot)
{
    slot -= m_firstIndex;

    // indicates that there is a new item on next page
    if (m_hasPages && slot > m_capacity) {
        callLuaField("onSizeChange", ++m_size);
        return;
    }

    if (m_items.size() == m_capacity) {
        onRemoveItem(m_firstIndex + m_capacity - 1, nullptr);
        ++m_size;
    }

    // El slot que manda el servidor puede caer fuera de rango (p.ej. al
    // intercambiar equipado<->contenedor, o con paginacion desincronizada).
    // Sin acotarlo, insert() con un iterador invalido es comportamiento
    // indefinido y el item acababa en una posicion incorrecta.
    const auto insertPos = std::clamp<int>(slot, 0, static_cast<int>(m_items.size()));
    if (insertPos != slot) {
        g_logger.warning("Container::onAddItem - contenedor {}: slot {} fuera de rango (items: {}, firstIndex: {}, capacity: {}), se inserta en {}",
                         m_id, slot, m_items.size(), m_firstIndex, m_capacity, insertPos);
    }

    m_items.insert(m_items.begin() + insertPos, item);
    ++m_size;

    updateItemsPositions();

    callLuaField("onSizeChange", m_size);
    callLuaField("onAddItem", slot, item);
}

ItemPtr Container::findItemById(const uint32_t itemId, const int subType, const uint8_t tier) const
{
    for (const auto& item : m_items)
        if (item->getId() == itemId && (subType == -1 || item->getSubType() == subType) && item->getTier() == tier)
            return item;
    return nullptr;
}

void Container::onAddItems(const std::vector<ItemPtr>& items)
{
    for (const auto& item : items)
        m_items.push_back(item);
    updateItemsPositions();
}

void Container::onUpdateItem(int slot, const ItemPtr& item)
{
    slot -= m_firstIndex;
    if (slot < 0 || slot >= static_cast<int>(m_items.size())) {
        // Salir aqui sin borrar deja el item viejo en la lista mientras el 'add'
        // que viene detras inserta el nuevo: el item se ve DUPLICADO aunque el
        // servidor tenga uno solo. El traceError original no se escribe en
        // compilacion release, asi que el fallo era invisible.
        // OJO: no sirve pedir refreshContainer -- el opcode 202 que envia el
        // cliente lo interpreta Canary como parseExivaRestrictions en 15.25.
        g_logger.warning("Container::onUpdateItem - contenedor {}: slot {} fuera de rango (items: {}, firstIndex: {}, capacity: {}, size: {}) -> update DESCARTADO, el widget se queda con el item viejo",
                         m_id, slot, m_items.size(), m_firstIndex, m_capacity, m_size);
        return;
    }

    const auto oldItem = m_items[slot];
    m_items[slot] = item;
    item->setPosition(getSlotPosition(slot));

    callLuaField("onUpdateItem", slot, item, oldItem);
}

void Container::onRemoveItem(int slot, const ItemPtr& lastItem)
{
    slot -= m_firstIndex;

    // indicates that there has been deleted an item on next page
    if (m_hasPages && slot >= static_cast<int>(m_items.size())) {
        callLuaField("onSizeChange", --m_size);
        return;
    }

    if (slot < 0 || slot >= static_cast<int>(m_items.size())) {
        // Salir aqui sin borrar deja el item viejo en la lista mientras el 'add'
        // que viene detras inserta el nuevo: se ve DUPLICADO aunque el servidor
        // tenga uno solo. traceError no se escribe en release -> era invisible.
        g_logger.warning("Container::onRemoveItem - contenedor {}: slot {} fuera de rango (items: {}, firstIndex: {}, capacity: {}, size: {}) -> remove DESCARTADO",
                         m_id, slot, m_items.size(), m_firstIndex, m_capacity, m_size);
        return;
    }

    const auto item = m_items[slot];
    m_items.erase(m_items.begin() + slot);

    if (lastItem) {
        onAddItem(lastItem, m_firstIndex + m_capacity - 1);
        --m_size;
    }

    --m_size;

    updateItemsPositions();

    callLuaField("onSizeChange", m_size);
    callLuaField("onRemoveItem", slot, item);
}

void Container::updateItemsPositions()
{
    for (int slot = 0; slot < static_cast<int>(m_items.size()); ++slot)
        m_items[slot]->setPosition(getSlotPosition(slot));
}