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

#include "thingtype.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include "animator.h"
#include "game.h"
#include "gameconfig.h"
#include "lightview.h"
#include "spriteappearances.h"
#include "spritemanager.h"
#include "framework/core/asyncdispatcher.h"
#include "framework/core/clock.h"
#include "framework/stdext/time.h"
#include <framework/util/stats.h>
#include "framework/core/filestream.h"
#include "framework/graphics/drawpoolmanager.h"
#include "framework/graphics/image.h"
#include "framework/otml/otmlnode.h"
#include <framework/core/graphicalapplication.h>

const static TexturePtr m_textureNull;

namespace {
    std::string_view categoryName(const ThingCategory category)
    {
        switch (category) {
            case ThingCategoryItem: return "item";
            case ThingCategoryCreature: return "creature";
            case ThingCategoryEffect: return "effect";
            case ThingCategoryMissile: return "missile";
            default: return "unknown";
        }
    }
}

#ifdef FRAMEWORK_PROTOBUF
void ThingType::unserializeAppearance(const uint16_t clientId, const ThingCategory category, const appearances::Appearance& appearance)
{
    m_fromAppearances = true;
    m_null = false;
    m_id = clientId;
    m_category = category;
    m_name = appearance.name();
    m_description = appearance.description();

    applyAppearanceFlags(appearance.flags());

    if (!g_game.getFeature(Otc::GameLoadSprInsteadProtobuf)) {
        m_animationPhases = 0;
        int totalSpritesCount = 0;

        struct FrameGroupInfo {
            int startIndex, numPatternX, numPatternY, numPatternZ, layers, phases;
        };
        std::vector<FrameGroupInfo> fgInfos;

        for (const auto& framegroup : appearance.frame_group()) {
            const int frameGroupType = framegroup.fixed_frame_group();
            const auto& spriteInfo = framegroup.sprite_info();
            const auto& animation = spriteInfo.animation();
            spriteInfo.sprite_id(); // sprites
            const auto& spritesPhases = animation.sprite_phase();

            m_numPatternX = spriteInfo.pattern_width();
            m_numPatternY = spriteInfo.pattern_height();
            m_numPatternZ = spriteInfo.pattern_depth();
            m_layers = spriteInfo.layers();
            m_opaque = spriteInfo.is_opaque();

            const int groupPhases = std::max<int>(1, spritesPhases.size());
            m_animationPhases += groupPhases;

            if (const auto& sheet = g_spriteAppearances.getSheetBySpriteId(spriteInfo.sprite_id(0), false)) {
                m_size = sheet->getSpriteSize() / g_gameConfig.getSpriteSize();
            }

            // animations
            if (spritesPhases.size() > 1) {
                auto* animator = new Animator;
                animator->unserializeAppearance(animation);

                if (frameGroupType == FrameGroupMoving)
                    m_animator = animator;
                else if (frameGroupType == FrameGroupIdle || frameGroupType == FrameGroupInitial)
                    m_idleAnimator = animator;
            }

            const int totalSprites = m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * groupPhases;

            if (totalSpritesCount + totalSprites > 4096)
                throw Exception("a thing type has more than 4096 sprites");

            const int fgStartIndex = totalSpritesCount;
            m_spritesIndex.resize(totalSpritesCount + totalSprites);
            for (int j = totalSpritesCount, spriteId = 0; j < (totalSpritesCount + totalSprites); ++j, ++spriteId) {
                m_spritesIndex[j] = spriteInfo.sprite_id(spriteId);
            }

            fgInfos.push_back({fgStartIndex, (int)m_numPatternX, (int)m_numPatternY, (int)m_numPatternZ, (int)m_layers, groupPhases});
            totalSpritesCount += totalSprites;
        }

        // When frame groups have different pattern dimensions (e.g. idle patternX=1 vs moving patternX=4),
        // the sprite index formula breaks. Remap all groups to the maximum dimensions found across
        // all frame groups: smaller groups repeat their sprites, larger groups are never truncated.
        if (fgInfos.size() > 1) {
            int tgtX = 1, tgtY = 1, tgtZ = 1, tgtL = 1;
            for (const auto& fg : fgInfos) {
                tgtX = std::max(tgtX, fg.numPatternX);
                tgtY = std::max(tgtY, fg.numPatternY);
                tgtZ = std::max(tgtZ, fg.numPatternZ);
                tgtL = std::max(tgtL, fg.layers);
            }

            bool dimensionMismatch = false;
            for (const auto& fg : fgInfos) {
                if (fg.numPatternX != tgtX || fg.numPatternY != tgtY ||
                    fg.numPatternZ != tgtZ || fg.layers != tgtL) {
                    dimensionMismatch = true;
                    break;
                }
            }

            if (dimensionMismatch) {
                const auto oldIndex = std::move(m_spritesIndex);
                m_spritesIndex.clear();
                m_spritesIndex.reserve(m_animationPhases * tgtX * tgtY * tgtZ * tgtL);

                for (const auto& fg : fgInfos) {
                    for (int a = 0; a < fg.phases; ++a) {
                        for (int z = 0; z < tgtZ; ++z) {
                            for (int y = 0; y < tgtY; ++y) {
                                for (int x = 0; x < tgtX; ++x) {
                                    for (int l = 0; l < tgtL; ++l) {
                                        const int srcX = std::min(x, fg.numPatternX - 1);
                                        const int srcY = std::min(y, fg.numPatternY - 1);
                                        const int srcZ = std::min(z, fg.numPatternZ - 1);
                                        const int srcL = std::min(l, fg.layers - 1);
                                        const int srcIdx = fg.startIndex +
                                            (((a * fg.numPatternZ + srcZ) * fg.numPatternY + srcY) * fg.numPatternX + srcX) * fg.layers + srcL;
                                        m_spritesIndex.push_back(oldIndex[srcIdx]);
                                    }
                                }
                            }
                        }
                    }
                }

                // Update member dimensions to reflect the unified target grid.
                m_numPatternX = tgtX;
                m_numPatternY = tgtY;
                m_numPatternZ = tgtZ;
                m_layers      = tgtL;
            }
        }

        m_textureData.resize(m_animationPhases);
    }
}

void ThingType::applyAppearanceFlags(const appearances::AppearanceFlags& flags)
{
    if (flags.has_bank()) {
        m_groundSpeed = flags.bank().waypoints();
        m_flags |= ThingFlagAttrGround;
    }

    if (flags.has_clip() && flags.clip()) {
        m_flags |= ThingFlagAttrGroundBorder;
    }

    if (flags.has_bottom()) {
        m_flags |= ThingFlagAttrOnBottom;
    }

    if (flags.has_top()) {
        m_flags |= ThingFlagAttrOnTop;
    }

    if (flags.has_container() && flags.container()) {
        m_flags |= ThingFlagAttrContainer;
    }

    if (flags.has_cumulative() && flags.cumulative()) {
        m_flags |= ThingFlagAttrStackable;
    }

    if (flags.has_multiuse() && flags.multiuse()) {
        m_flags |= ThingFlagAttrMultiUse;
    }

    if (flags.has_forceuse() && flags.forceuse()) {
        m_flags |= ThingFlagAttrForceUse;
    }

    if (flags.has_usable() && flags.usable()) {
        m_flags |= ThingFlagAttrUsable;
    }

    if (flags.has_write()) {
        m_flags |= ThingFlagAttrWritable;
        m_maxTextLength = flags.write().max_text_length();
    }

    if (flags.has_write_once()) {
        m_flags |= ThingFlagAttrWritableOnce;
        m_maxTextLength = flags.write_once().max_text_length_once();
    }

    if (flags.has_liquidpool() && flags.liquidpool()) {
        m_flags |= ThingFlagAttrSplash;
    }

    if (flags.has_unpass() && flags.unpass()) {
        m_flags |= ThingFlagAttrNotWalkable;
    }

    if (flags.has_unmove() && flags.unmove()) {
        m_flags |= ThingFlagAttrNotMoveable;
    }

    if (flags.has_unsight() && flags.unsight()) {
        m_flags |= ThingFlagAttrBlockProjectile;
    }

    if (flags.has_avoid() && flags.avoid()) {
        m_flags |= ThingFlagAttrNotPathable;
    }

    // no_movement_animation (?)

    if (flags.has_take() && flags.take()) {
        m_flags |= ThingFlagAttrPickupable;
    }

    if (flags.has_liquidcontainer() && flags.liquidcontainer()) {
        m_flags |= ThingFlagAttrFluidContainer;
    }

    if (flags.has_hang() && flags.hang()) {
        m_flags |= ThingFlagAttrHangable;
    }

    if (flags.has_hook()) {
        const auto& hookDirection = flags.hook();
        if (hookDirection.has_south()) {
            const auto hookType = hookDirection.south();
            if (hookType == appearances::HOOK_TYPE_SOUTH) {
                m_flags |= ThingFlagAttrHookSouth;
            } else if (hookType == appearances::HOOK_TYPE_EAST) {
                m_flags |= ThingFlagAttrHookEast;
            }
        }
        if (hookDirection.has_east()) {
            const auto hookType = hookDirection.east();
            if (hookType == appearances::HOOK_TYPE_SOUTH) {
                m_flags |= ThingFlagAttrHookSouth;
            } else if (hookType == appearances::HOOK_TYPE_EAST) {
                m_flags |= ThingFlagAttrHookEast;
            }
        }
    }

    if (flags.has_light()) {
        m_flags |= ThingFlagAttrLight;
        m_light = { static_cast<uint8_t>(flags.light().brightness()), static_cast<uint8_t>(flags.light().color()) };
    }

    if (flags.has_rotate() && flags.rotate()) {
        m_flags |= ThingFlagAttrRotateable;
    }

    if (flags.has_dont_hide() && flags.dont_hide()) {
        m_flags |= ThingFlagAttrDontHide;
    }

    if (flags.has_translucent() && flags.translucent()) {
        m_flags |= ThingFlagAttrTranslucent;
    }

    if (flags.has_shift()) {
        // El shift viene en pixeles referidos a un tile de 32. En el dibujado se
        // resta junto a m_size * getSpriteSize() (linea ~807), que SI escala con el
        // tamano de sprite, asi que hay que escalarlo igual o los objetos con
        // desplazamiento (mesas, muebles) quedan colocados a la mitad de camino.
        const int shiftScale = std::max<int>(1, g_gameConfig.getSpriteSize() / 32);
        m_displacement = Point(flags.shift().x() * shiftScale, flags.shift().y() * shiftScale);
        m_flags |= ThingFlagAttrDisplacement;
    }

    if (flags.has_height()) {
        m_elevation = flags.height().elevation();
        m_flags |= ThingFlagAttrElevation;
    }

    if (flags.has_lying_object() && flags.lying_object()) {
        m_flags |= ThingFlagAttrLyingCorpse;
    }

    if (flags.has_animate_always() && flags.animate_always()) {
        m_flags |= ThingFlagAttrAnimateAlways;
    }

    if (flags.has_automap()) {
        m_minimapColor = flags.automap().color();
        m_flags |= ThingFlagAttrMinimapColor;
    }

    if (flags.has_lenshelp()) {
        m_lensHelp = flags.lenshelp().id();
        m_flags |= ThingFlagAttrLensHelp;
    }

    if (flags.has_fullbank() && flags.fullbank()) {
        m_flags |= ThingFlagAttrFullGround;
    }

    if (flags.has_ignore_look() && flags.ignore_look()) {
        m_flags |= ThingFlagAttrLook;
    }

    if (flags.has_clothes()) {
        m_clothSlot = flags.clothes().slot();
        m_flags |= ThingFlagAttrCloth;
    }

    // default action

    if (flags.has_market()) {
        m_market.category = static_cast<ITEM_CATEGORY>(flags.market().category());
        m_market.tradeAs = flags.market().trade_as_object_id();
        m_market.showAs = flags.market().show_as_object_id();
        if (g_game.getFeature(Otc::GameLoadSprInsteadProtobuf)) {
            // keep from tibia.dat
            if (m_market.name.empty() && !flags.market().name().empty()) {
                m_market.name = flags.market().name();
            }
        } else {
            m_market.name = m_name;
        }

        for (const int32_t voc : flags.market().restrict_to_profession()) {
            uint16_t vocBitMask = std::pow(2, voc - 1);
            m_market.restrictVocation |= vocBitMask;
        }

        m_market.requiredLevel = flags.market().minimum_level();
        m_flags |= ThingFlagAttrMarket;
    }

    if (flags.has_wrap() && flags.wrap()) {
        m_flags |= ThingFlagAttrWrapable;
    }

    if (flags.has_unwrap() && flags.unwrap()) {
        m_flags |= ThingFlagAttrUnwrapable;
    }

    if (flags.has_topeffect() && flags.topeffect()) {
        m_flags |= ThingFlagAttrTopEffect;
    }

    if (flags.has_default_action()) {
        m_defaultAction = static_cast<PLAYER_ACTION>(flags.default_action().action());
    }

    // npcsaledata
    if (flags.npcsaledata_size() > 0) {
        for (int i = 0; i < flags.npcsaledata_size(); ++i) {
            NPCData data;
            data.name = flags.npcsaledata(i).name();
            data.location = flags.npcsaledata(i).location();
            data.salePrice = flags.npcsaledata(i).sale_price();
            data.buyPrice = flags.npcsaledata(i).buy_price();
            data.currencyObjectTypeId = flags.npcsaledata(i).currency_object_type_id();
            data.currencyQuestFlagDisplayName = flags.npcsaledata(i).currency_quest_flag_display_name();
            m_npcData.push_back(data);
        }
        m_flags |= ThingFlagAttrNPC;
    }

    // charged to expire
    // corpse
    // player_corpse
    if (flags.has_cyclopediaitem()) {
        m_cyclopediaType = flags.cyclopediaitem().cyclopedia_type();
    }

    // ammo
    if (flags.has_ammo() && flags.ammo()) {
        m_flags |= ThingFlagAttrAmmo;
    }
    if (flags.has_show_off_socket() && flags.show_off_socket()) {
        m_flags |= ThingFlagAttrPodium;
    }

    // reportable

    if (flags.has_upgradeclassification()) {
        m_upgradeClassification = flags.upgradeclassification().upgrade_classification();
    }

    // reverse_addons_east
    // reverse_addons_west
    // reverse_addons_south
    // reverse_addons_north

    if (flags.has_wearout() && flags.wearout()) {
        m_flags |= ThingFlagAttrWearOut;
    }

    if (flags.has_clockexpire() && flags.clockexpire()) {
        m_flags |= ThingFlagAttrClockExpire;
    }

    if (flags.has_expire() && flags.expire()) {
        m_flags |= ThingFlagAttrExpire;
    }

    if (flags.has_expirestop() && flags.expirestop()) {
        m_flags |= ThingFlagAttrExpireStop;
    }

    if (flags.has_deco_kit() && flags.deco_kit()) {
        m_flags |= ThingFlagAttrDecoKit;
    }

    // proficiency flag
    if (flags.has_proficiency()) {
        if (g_game.getFeature(Otc::GameProficiency)) {
            m_proficiencyId = flags.proficiency().proficiency_id();
            m_flags |= ThingFlagAttrProficiency;
        }
    }

    // skill wheel gem
    if (flags.has_skillwheel_gem()) {
        m_skillWheelGem.gem_quality_id = flags.skillwheel_gem().gem_quality_id();
        m_skillWheelGem.vocation_id = flags.skillwheel_gem().vocation_id();
        m_flags |= ThingFlagAttrSkillWheelGem;
    }

    if (flags.has_dual_wielding() && flags.dual_wielding()) {
        m_flags |= ThingFlagAttrDualWield;
    }

    if (flags.has_imbueable()) {
        m_imbueSlots = flags.imbueable().slot_count();
        m_flags |= ThingFlagAttrImbueable;
    }

    for (int i = 0; i < flags.restrict_to_vocation_size(); ++i) {
        m_restrictVocation.push_back(static_cast<uint32_t>(flags.restrict_to_vocation(i)));
    }

    if (flags.has_minimum_level()) {
        m_minimumLevel = flags.minimum_level();
    }

    if (flags.has_weapon_type()) {
        const auto wt = flags.weapon_type();
        if (wt == otclient::protobuf::appearances::WEAPON_TYPE_SWORD)
            m_weaponType = ITEM_CATEGORY_SWORDS;
        else if (wt == otclient::protobuf::appearances::WEAPON_TYPE_AXE)
            m_weaponType = ITEM_CATEGORY_AXES;
        else if (wt == otclient::protobuf::appearances::WEAPON_TYPE_CLUB)
            m_weaponType = ITEM_CATEGORY_CLUBS;
        else if (wt == otclient::protobuf::appearances::WEAPON_TYPE_FIST)
            m_weaponType = ITEM_CATEGORY_FIST_WEAPONS;
        else if (wt == otclient::protobuf::appearances::WEAPON_TYPE_BOW
              || wt == otclient::protobuf::appearances::WEAPON_TYPE_CROSSBOW
              || wt == otclient::protobuf::appearances::WEAPON_TYPE_THROW)
            m_weaponType = ITEM_CATEGORY_DISTANCE_WEAPONS;
        else if (wt == otclient::protobuf::appearances::WEAPON_TYPE_WAND_ROD)
            m_weaponType = ITEM_CATEGORY_WANDS_RODS;
        else
            m_weaponType = 0;
    }
}
#endif

void ThingType::unserialize(const uint16_t clientId, const ThingCategory category, const FileStreamPtr& fin)
{
    m_null = false;
    m_id = clientId;
    m_category = category;

    int count = 0;
    int attr = -1;
    bool done = false;
    for (int i = 0; i < ThingLastAttr; ++i) {
        ++count;
        attr = fin->getU8();
        if (attr == ThingLastAttr) {
            done = true;
            break;
        }

        if (g_game.getClientVersion() >= 1000) {
            /* In 10.10+ all attributes from 16 and up were
             * incremented by 1 to make space for 16 as
             * "No Movement Animation" flag.
             */
            if (attr == 16)
                attr = ThingAttrNoMoveAnimation;
            else if (attr == 254) { // Usable
                attr = ThingAttrUsable;
            } else if (attr == 35) { // Default Action
                attr = ThingAttrDefaultAction;
            } else if (attr > 16)
                attr -= 1;
        } else if (g_game.getClientVersion() >= 860) {
            /* Default attribute values follow
             * the format of 8.6-9.86.
             * Therefore no changes here.
             */
        } else if (g_game.getClientVersion() >= 780) {
            /* In 7.80-8.54 all attributes from 8 and higher were
             * incremented by 1 to make space for 8 as
             * "Item Charges" flag.
             */
            if (attr == 8) {
                attr = ThingAttrChargeable;
                continue;
            }
            if (attr > 8)
                attr -= 1;
        } else if (g_game.getClientVersion() >= 755) {
            /* In 7.55-7.72 attributes 23 is "Floor Change". */
            if (attr == 23)
                attr = ThingAttrFloorChange;
        } else if (g_game.getClientVersion() >= 740) {
            /* In 7.4-7.5 attribute "Ground Border" did not exist
             * attributes 1-15 have to be adjusted.
             * Several other changes in the format.
             */
            if (attr > 0 && attr <= 15)
                attr += 1;
            else if (attr == 16)
                attr = ThingAttrLight;
            else if (attr == 17)
                attr = ThingAttrFloorChange;
            else if (attr == 18)
                attr = ThingAttrFullGround;
            else if (attr == 19)
                attr = ThingAttrElevation;
            else if (attr == 20)
                attr = ThingAttrDisplacement;
            else if (attr == 22)
                attr = ThingAttrMinimapColor;
            else if (attr == 23)
                attr = ThingAttrRotateable;
            else if (attr == 24)
                attr = ThingAttrLyingCorpse;
            else if (attr == 25)
                attr = ThingAttrHangable;
            else if (attr == 26)
                attr = ThingAttrHookSouth;
            else if (attr == 27)
                attr = ThingAttrHookEast;
            else if (attr == 28)
                attr = ThingAttrAnimateAlways;

            /* "Multi Use" and "Force Use" are swapped */
            if (attr == ThingAttrMultiUse)
                attr = ThingAttrForceUse;
            else if (attr == ThingAttrForceUse)
                attr = ThingAttrMultiUse;
        }

        const auto thingAttr = static_cast<ThingAttr>(attr);
        m_flags |= thingAttrToThingFlagAttr(thingAttr);

        switch (attr) {
            case ThingAttrDisplacement:
            {
                if (g_game.getClientVersion() >= 755) {
                    if (g_game.getFeature(Otc::GameNegativeOffset)) {
                        m_displacement.x = fin->get16();
                        m_displacement.y = fin->get16();
                    } else {
                        m_displacement.x = fin->getU16();
                        m_displacement.y = fin->getU16();
                    }
                } else {
                    m_displacement.x = 8;
                    m_displacement.y = 8;
                }
                break;
            }
            case ThingAttrLight:
            {
                m_light.intensity = fin->getU16();
                m_light.color = fin->getU16();
                break;
            }
            case ThingAttrMarket:
            {
                m_market.category = static_cast<ITEM_CATEGORY>(fin->getU16());
                m_market.tradeAs = fin->getU16();
                m_market.showAs = fin->getU16();
                m_market.name = fin->getString();
                m_market.restrictVocation = fin->getU16();
                m_market.requiredLevel = fin->getU16();
                break;
            }
            case ThingAttrElevation: m_elevation = fin->getU16(); break;
            case ThingAttrGround: m_groundSpeed = fin->getU16(); break;
            case ThingAttrWritable: m_maxTextLength = fin->getU16(); break;
            case ThingAttrWritableOnce:m_maxTextLength = fin->getU16(); break;
            case ThingAttrMinimapColor: m_minimapColor = fin->getU16(); break;
            case ThingAttrCloth: m_clothSlot = fin->getU16(); break;
            case ThingAttrLensHelp: m_lensHelp = fin->getU16(); break;
            case ThingAttrDefaultAction: m_defaultAction = static_cast<PLAYER_ACTION>(fin->getU16()); break;
        }
    }

    if (!done)
        throw Exception("corrupt data (id: {}, category: {}, count: {}, lastAttr: {})", m_id, m_category, count, attr);

    const bool hasFrameGroups = category == ThingCategoryCreature && g_game.getFeature(Otc::GameIdleAnimations);
    const uint8_t groupCount = hasFrameGroups ? fin->getU8() : 1;

    m_animationPhases = 0;
    int totalSpritesCount = 0;
    std::vector<Size> sizes;
    std::vector<int> total_sprites;

    for (int i = 0; i < groupCount; ++i) {
        uint8_t frameGroupType = FrameGroupDefault;
        if (hasFrameGroups)
            frameGroupType = fin->getU8();

        const uint8_t width = fin->getU8();
        const uint8_t height = fin->getU8();
        m_size = { width, height };
        sizes.emplace_back(m_size);
        if (width > 1 || height > 1) {
            m_realSize = std::max<int>(m_realSize, fin->getU8());
        }

        m_layers = fin->getU8();
        m_numPatternX = fin->getU8();
        m_numPatternY = fin->getU8();
        if (g_game.getClientVersion() >= 755)
            m_numPatternZ = fin->getU8();
        else
            m_numPatternZ = 1;

        const int groupAnimationsPhases = fin->getU8();
        m_animationPhases += groupAnimationsPhases;

        if (groupAnimationsPhases > 1 && g_game.getFeature(Otc::GameEnhancedAnimations)) {
            auto* animator = new Animator;
            animator->unserialize(groupAnimationsPhases, fin);

            if (frameGroupType == FrameGroupMoving)
                m_animator = animator;
            else if (frameGroupType == FrameGroupIdle)
                m_idleAnimator = animator;
        }

        const int totalSprites = m_size.area() * m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * groupAnimationsPhases;
        total_sprites.push_back(totalSprites);
        if (totalSpritesCount + totalSprites > 4096)
            throw Exception("a thing type has more than 4096 sprites");

        m_spritesIndex.resize(totalSpritesCount + totalSprites);
        for (int j = totalSpritesCount; j < (totalSpritesCount + totalSprites); ++j)
            m_spritesIndex[j] = g_game.getFeature(Otc::GameSpritesU32) ? fin->getU32() : fin->getU16();

        totalSpritesCount += totalSprites;
    }
    if (sizes.size() > 1) {
        bool hasDifferentSizes = false;
        const Size& firstSize = sizes[0];
        for (size_t i = 1; i < sizes.size(); ++i) {
            if (sizes[i] != firstSize) {
                hasDifferentSizes = true;
                break;
            }
        }
        if (hasDifferentSizes) {
            for (const auto& s : sizes) {
                m_size.setWidth(std::max<int>(m_size.width(), s.width()));
                m_size.setHeight(std::max<int>(m_size.height(), s.height()));
            }
            const size_t expectedSize = m_size.area() * m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * m_animationPhases;
            if (expectedSize != m_spritesIndex.size()) {
                const std::vector sprites(std::move(m_spritesIndex));
                m_spritesIndex.clear();
                m_spritesIndex.reserve(expectedSize);
                for (size_t i = 0, idx = 0; i < sizes.size(); ++i) {
                    const int totalSprites = total_sprites[i];
                    if (m_size == sizes[i]) {
                        for (int j = 0; j < totalSprites; ++j) {
                            m_spritesIndex.push_back(sprites[idx++]);
                        }
                        continue;
                    }
                    const size_t patterns = (totalSprites / sizes[i].area());
                    for (size_t p = 0; p < patterns; ++p) {
                        for (int x = 0; x < m_size.width(); ++x) {
                            for (int y = 0; y < m_size.height(); ++y) {
                                if (x < sizes[i].width() && y < sizes[i].height()) {
                                    m_spritesIndex.push_back(sprites[idx++]);
                                    continue;
                                }
                                m_spritesIndex.push_back(0);
                            }
                        }
                    }
                }
            }
        }
    }
    m_textureData.resize(m_animationPhases);
}

void ThingType::unserializeOtml(const OTMLNodePtr& node)
{
    for (const auto& node2 : node->children()) {
        if (node2->tag() == "opacity")
            m_opacity = node2->value<float>();
        else if (node2->tag() == "image")
            m_customImage = node2->value();
        else if (node2->tag() == "full-ground") {
            if (node2->value<bool>())
                m_flags &= ~ThingFlagAttrFullGround;
            else
                m_flags |= ThingFlagAttrFullGround;
        }
    }
}

void ThingType::drawWithFrameBuffer(const TexturePtr& texture, const Rect& screenRect, const Rect& textureRect, const Color& color) {
    const int size = static_cast<int>(g_gameConfig.getSpriteSize() * std::max<int>(m_size.area(), 2) * g_drawPool.getScaleFactor());
    const auto& p = (Point(size) - screenRect.size().toPoint()) / 2;
    const auto& destDiff = Rect(screenRect.topLeft() - p, Size{ size });

    g_drawPool.bindFrameBuffer(destDiff.size()); {
        // Debug
        // g_drawPool.addBoundingRect(Rect(Point(0), destDiff.size()), Color::red);

        g_drawPool.addTexturedRect(Rect(p, screenRect.size()), texture, textureRect, color);
    } g_drawPool.releaseFrameBuffer(destDiff);
    g_drawPool.resetShaderProgram();
}

void ThingType::draw(const Point& dest, const int layer, const int xPattern, const int yPattern, const int zPattern, const int animationPhase, const Color& color, const bool drawThings, LightView* lightView)
{
    // items:
    // layer - item layer (example: in old clients, a modified dat file was using this to show which tiles could be fished)
    // xPattern - ground pattern 1 / count or fluid based coordinate
    // yPattern - ground pattern 2 / count or fluid based coordinate
    // zPattern - ground pattern 3 (eg. zaoan roofs)

    // outfits:
    // layer = outfit layer (normal / mask)
    // xPattern = direction
    // yPattern = addon layer
    // zPattern = mounted state

    if (m_null || m_animationPhases == 0)
        return;

    // outfits like 126 and 127 don't have animation
    // this line fixes a bug that makes them disappear while moving
    int animationFrameId = animationPhase % m_animationPhases;

    // Con drawThings == false es la pasada de luz: solo consulta, no compone.
    s_soloLectura = !drawThings;
    auto texture = getTexture(animationFrameId);

    // Respaldo: si la fase pedida aun se esta construyendo, se dibuja la fase 0.
    //
    // Cada fase de animacion es una textura completa, y en HD un outfit con addons
    // y montura ocupa 8 MB por fase (1024x2048) con 9 fases: 72 MB que tardan en
    // construirse en segundo plano. Sin respaldo, la criatura simplemente no se
    // dibujaba durante esos fotogramas. Parado se usa siempre la fase 0, ya lista,
    // y por eso el hueco solo aparecia al caminar. En SD son 2 MB por fase y se
    // terminan antes de que de tiempo a verlo.
    //
    // Repetir un fotograma de animacion un instante se nota mucho menos que un
    // hueco, y en cuanto la fase termina de construirse se usa la buena.
    if (!texture && animationFrameId != 0)
        texture = getTexture(0);
    s_soloLectura = false;

    if (!texture) {
        // Diagnostico: este objeto se queda sin pintar este fotograma. Solo cuenta
        // la pasada del mapa; la de luz ahora no compone y "fallaria" a proposito.
        if (drawThings) {
            s_skipTotal.fetch_add(1, std::memory_order_relaxed);
            const bool esSuelo = isGround();
            if (esSuelo)
                s_skipGround.fetch_add(1, std::memory_order_relaxed);
            s_skipLastId.store(m_id, std::memory_order_relaxed);
            s_skipLastGround.store(esSuelo, std::memory_order_relaxed);
        }

        // Reset any pending onlyOnce state to prevent stale opacity/shader
        // from affecting subsequent draws when texture is still loading
        g_drawPool.resetOnlyOnceParameters();
        return; // texture might not exists, neither its rects.
    }

    // Los rectangulos tienen que salir de la MISMA fase que la textura usada.
    if (!m_textureData[animationFrameId].source)
        animationFrameId = 0;

    const auto& textureData = m_textureData[animationFrameId];

    const uint32_t frameIndex = getTextureIndex(layer, xPattern, yPattern, zPattern);
    if (frameIndex >= textureData.pos.size()) {
        // Diagnostico: segunda salida sin pintar, que el contador de arriba no veia.
        if (drawThings)
            s_skipSinRect.fetch_add(1, std::memory_order_relaxed);
        g_drawPool.resetOnlyOnceParameters();
        return;
    }

    const auto& textureOffset = textureData.pos[frameIndex].offsets;
    const auto& textureRect = textureData.pos[frameIndex].rects;

    const Rect screenRect(dest + (textureOffset - m_displacement - (m_size.toPoint() - Point(1)) * g_gameConfig.getSpriteSize()) * g_drawPool.getScaleFactor(), textureRect.size() * g_drawPool.getScaleFactor());

    if (drawThings && texture) {
        const auto& newColor = m_opacity < 1.0f ? Color(color, m_opacity) : color;

        if (g_drawPool.shaderNeedFramebuffer())
            drawWithFrameBuffer(texture, screenRect, textureRect, newColor);
        else
            g_drawPool.addTexturedRect(screenRect, texture, textureRect, newColor);
    } else {
        // Reset any pending onlyOnce state when not drawing things
        // to prevent stale opacity/shader from affecting subsequent draws
        g_drawPool.resetOnlyOnceParameters();
    }

    if (lightView && hasLight()) {
        lightView->addLightSource(screenRect.center(), m_light);
    }
}

const TexturePtr& ThingType::getTexture(const int animationPhase)
{
    if (m_null) return m_textureNull;

    m_lastTimeUsage.restart();

    auto& textureData = m_textureData[animationPhase];

    if (textureData.source)
        return textureData.source;

    // La pasada de LUZ solo consulta: si la textura no esta, ni la compone ni se
    // queda con el objeto. Componer desde dos hilos a la vez (mapa y luz) era lo
    // que dejaba el suelo "ocupado" justo cuando el mapa lo necesitaba.
    if (s_soloLectura)
        return m_textureNull;

    bool expected = false;
    if (m_loading.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        bool async = g_app.isLoadingAsyncTexture();
        // La interfaz construye sus texturas al momento para que no parpadeen... pero
        // un atlas de criatura son 8 MB en HD (120 poses) compuestos en el HILO
        // PRINCIPAL, y la ventana de outfits pide 130 de golpe: medido, un segundo
        // entero de tirones en la primera apertura.
        //
        // Los outfits pasan a construirse en segundo plano: la vista previa aparece
        // en blanco un instante y luego se rellena, que es como se comporta el
        // cliente oficial. Los items de la interfaz son pequenos y baratos, asi que
        // esos se siguen construyendo al momento.
        if (g_game.isUsingProtobuf() && g_drawPool.getCurrentType() == DrawPoolType::FOREGROUND
            && m_category != ThingCategoryCreature)
            async = false;

        // Los SUELOS tampoco van en segundo plano. Mientras una textura se construye
        // draw() no pinta nada, y bajo tierra el mapa se limpia a negro: un suelo sin
        // textura todavia es un agujero negro en el piso. Medido con el fondo en
        // magenta, por magma: 7.865 suelos sin pintar en un minuto, huecos de hasta un
        // segundo en HD (hojas de 768 que descomprimir, y los atlas de criatura de
        // 8 MB delante en la cola cuando hay summons). Un suelo es una baldosa, no un
        // atlas: componerla al momento cuesta mucho menos que el agujero.
        //
        // En este camino se ESPERA a la hoja de sprites si otro hilo la esta
        // descomprimiendo (ver loadTexture); abortar volveria a dejar el agujero.
        const bool esSuelo = m_category == ThingCategoryItem && (isGround() || isGroundBorder());
        if (esSuelo)
            async = false;

        if (!async) {
            AutoStat medida(STATS_GENERAL, esSuelo ? "ComponerSuelo" : "ComponerObjeto", std::to_string(m_id));
            s_esperarHojas = esSuelo;
            // El suelo se compone en el hilo del mapa: sus lecturas de disco pasan
            // por delante de las de los hilos que componen outfits.
            SpriteAppearances::setLecturaPrioritaria(esSuelo);
            loadTexture(animationPhase);
            SpriteAppearances::setLecturaPrioritaria(false);
            s_esperarHojas = false;
            m_loading.store(false, std::memory_order_release);
            if (esSuelo && !textureData.source)
                s_sueloComposicionVacia.fetch_add(1, std::memory_order_relaxed);
            return textureData.source;
        }

        auto action = [this] {
            for (int_fast8_t i = -1; ++i < m_animationPhases;)
                loadTexture(i);
            m_loading.store(false, std::memory_order_release);
        };

        g_asyncDispatcher->detach_task(std::move(action));
    }

    // Si otro hilo tiene cogido este suelo es porque lo esta componiendo ahora
    // mismo (solo la pasada del mapa compone; la de luz solo consulta). Aqui NO se
    // espera: dormir el hilo que recoge el mapa era un micro tiron medible (92
    // esperas por minuto en magma). El suelo sale en el siguiente fotograma, que
    // ya lo encontrara hecho.
    if (m_category == ThingCategoryItem && (isGround() || isGroundBorder()))
        s_sueloObjetoOcupado.fetch_add(1, std::memory_order_relaxed);

    return m_textureNull;
}

void ThingType::preload()
{
    if (m_null || m_animationPhases == 0)
        return;

    // Solo la fase 0: es la que muestra una vista previa quieta. Cargar las nueve
    // fases de cada outfit serian 72 MB por outfit en HD.
    if (!m_textureData.empty() && m_textureData[0].source)
        return;

    bool expected = false;
    if (!m_loading.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    g_asyncDispatcher->detach_task([this] {
        loadTexture(0);
        m_loading.store(false, std::memory_order_release);
    });
}

void ThingType::precalentarHojas()
{
    // Descomprime en segundo plano las hojas de sprites de este objeto, para que
    // cuando entre en pantalla componer su textura no tenga que esperar al LZMA
    // (~8 ms por hoja HD) dentro del hilo del mapa: era un micro tiron al caminar
    // cada vez que aparecia un suelo de una hoja nueva. Se llama al llegar la
    // casilla por red, que es un paso antes de que se vea.
    //
    // Solo las hojas, no la textura: componerla la hace quien la necesite, y asi
    // no se pisa con m_loading ni con el camino sincrono de los suelos.
    if (m_null || m_spritesIndex.empty() || hasTexture())
        return;
    if (!(m_fromAppearances || g_game.isUsingProtobuf()))
        return;

    std::vector<SpriteSheetPtr> pendientes;
    for (const auto spriteId : m_spritesIndex) {
        if (spriteId == 0)
            continue;
        bool cargando = false;
        const auto& hoja = g_spriteAppearances.getSheetBySpriteId(spriteId, cargando, false);
        if (!hoja || hoja->m_loadingState.load(std::memory_order_acquire) != SpriteLoadState::NONE)
            continue;
        if (std::ranges::find(pendientes, hoja) == pendientes.end())
            pendientes.emplace_back(hoja);
    }
    if (pendientes.empty())
        return;

    encolarHojas(std::move(pendientes));
}

// Cola de precarga con UN solo lector.
//
// La primera version lanzaba una tarea al pool por cada objeto, y al cargar una
// zona (login, escaleras, teleport) salian decenas de lecturas de disco a la vez.
// En un disco mecanico eso es una cola de seeks: medido, cada hoja pasaba de 8 ms
// a 300-540 ms, y el hilo del mapa, que tambien tiene que leer las suyas para los
// suelos visibles, se quedaba detras de todas (fotogramas de 400 ms a 1,5 s).
// Con un unico lector el disco atiende las peticiones de una en una y la lectura
// sincrona del hilo del mapa solo compite con una.
//
// Es una pila (la ultima que entra, primera que sale): lo recien llegado por red
// es lo mas cercano a verse; el fondo de la pila es la descripcion inicial del
// mapa, que el hilo del mapa habra compuesto ya de forma sincrona si hizo falta.
static std::mutex s_colaHojasMutex;
static std::vector<SpriteSheetPtr> s_colaHojas;
static std::atomic_bool s_lectorHojasActivo{ false };

void ThingType::encolarHojas(std::vector<SpriteSheetPtr>&& hojas)
{
    {
        std::lock_guard<std::mutex> lock(s_colaHojasMutex);
        for (auto& hoja : hojas)
            s_colaHojas.emplace_back(std::move(hoja));
        // Sin tope de tamano: cada entrada es un shared_ptr; un login mete unos
        // cientos y se vacian en segundos.
    }

    bool esperado = false;
    if (!s_lectorHojasActivo.compare_exchange_strong(esperado, true, std::memory_order_acq_rel))
        return;

    g_asyncDispatcher->detach_task([] {
        // Indices de las hojas cargadas en esta tanda: en los ratos libres se leen
        // del disco (solo leer, sin decodificar) sus vecinas por id de sprite. Los
        // sprites de un mismo tileset van seguidos, asi que la vecina de una hoja
        // que acaba de hacer falta tiene muchas papeletas de hacer falta despues,
        // y leerla ahora es un acceso casi secuencial en vez de un seek mas tarde.
        std::vector<int> recientes;
        const auto hayCola = [] {
            std::lock_guard<std::mutex> lock(s_colaHojasMutex);
            return !s_colaHojas.empty();
        };

        while (true) {
            SpriteSheetPtr hoja;
            {
                std::lock_guard<std::mutex> lock(s_colaHojasMutex);
                if (!s_colaHojas.empty()) {
                    hoja = std::move(s_colaHojas.back());
                    s_colaHojas.pop_back();
                }
            }

            if (hoja) {
                if (hoja->m_loadingState.load(std::memory_order_acquire) == SpriteLoadState::NONE)
                    g_spriteAppearances.loadSpriteSheet(hoja);
                if (hoja->indice >= 0 && recientes.size() < 32)
                    recientes.emplace_back(hoja->indice);
                continue;
            }

            // Cola vacia: lectura anticipada de vecinas mientras no llegue nada nuevo.
            bool leida = false;
            for (const int base : recientes) {
                for (const int d : { 1, -1, 2, -2 }) {
                    // leerComprimido comprueba bajo candado si ya esta leida y sale al momento
                    if (const auto& vecina = g_spriteAppearances.getSheetByIndex(base + d)) {
                        g_spriteAppearances.leerComprimido(vecina);
                        leida = true;
                        if (hayCola())
                            break;
                    }
                }
                if (hayCola())
                    break;
            }
            recientes.clear();
            if (leida && hayCola())
                continue;

            std::lock_guard<std::mutex> lock(s_colaHojasMutex);
            if (!s_colaHojas.empty())
                continue;
            // Se apaga con el candado cogido: quien encole despues vera la
            // bandera a false y arrancara un lector nuevo.
            s_lectorHojasActivo.store(false, std::memory_order_release);
            return;
        }
    });
}

void ThingType::loadTexture(const int animationPhase)
{
    auto& textureData = m_textureData[animationPhase];
    if (textureData.source)
        return;

    // we don't need layers in common items, they will be pre-drawn
    int textureLayers = 1;
    int numLayers = m_layers;
    if (m_category == ThingCategoryCreature && numLayers >= 2) {
        // 5 layers: outfit base, red mask, green mask, blue mask, yellow mask
        textureLayers = 5;
        numLayers = 5;
    }

    const bool useCustomImage = animationPhase == 0 && !m_customImage.empty();
    const int indexSize = textureLayers * m_numPatternX * m_numPatternY * m_numPatternZ;
    const auto& textureSize = getBestTextureDimension(m_size.width(), m_size.height(), indexSize);
    const auto& fullImage = useCustomImage ? Image::load(m_customImage) : std::make_shared<Image>(textureSize * g_gameConfig.getSpriteSize());
    // Decide por el ORIGEN de los datos, no por el estado de la conexion.
    const bool protobufSupported = m_fromAppearances || g_game.isUsingProtobuf();

    static Color maskColors[] = { Color::red, Color::green, Color::blue, Color::yellow };

    textureData.pos.resize(indexSize);
    for (int z = 0; z < m_numPatternZ; ++z) {
        for (int y = 0; y < m_numPatternY; ++y) {
            for (int x = 0; x < m_numPatternX; ++x) {
                for (int l = 0; l < numLayers; ++l) {
                    const bool spriteMask = m_category == ThingCategoryCreature && l > 0;
                    const int frameIndex = getTextureIndex(l % textureLayers, x, y, z);

                    const auto& framePos = Point(frameIndex % (textureSize.width() / m_size.width()) * m_size.width(),
                        frameIndex / (textureSize.width() / m_size.width()) * m_size.height()) * g_gameConfig.getSpriteSize();

                    if (!useCustomImage) {
                        if (protobufSupported) {
                            const uint32_t spriteIndex = getSpriteIndex(-1, -1, spriteMask ? 1 : l, x, y, z, animationPhase);
                            auto spriteId = m_spritesIndex[spriteIndex];
                            bool isLoading = false;
                            auto spriteImage = g_sprites.getSpriteImage(spriteId, isLoading);

                            // Otro hilo esta descomprimiendo esta hoja. Lo normal es abortar y
                            // reintentar en otro fotograma, pero componiendo un suelo al momento eso
                            // lo dejaria sin pintar: se espera a la hoja, como mucho 8 ms (mas
                            // seria un tiron; si no llega, sale en el siguiente fotograma).
                            for (int espera = 0; isLoading && s_esperarHojas && espera < 50; ++espera) {
                                stdext::millisleep(1);
                                isLoading = false;
                                spriteImage = g_sprites.getSpriteImage(spriteId, isLoading);
                            }

                            if (isLoading)
                                return;

                            if (!spriteImage) {
                                g_logger.error("Failed to fetch sprite id {} for thing {} ({}, {}), layer {}, pattern {}x{}x{}, frame {}", spriteId, m_name, m_id, categoryName(m_category), l, x, y, z, animationPhase);
                                return;
                            }

                            // verifies that the first block in the lower right corner is transparent.
                            if (!spriteImage || spriteImage->hasTransparentPixel()) {
                                fullImage->setTransparentPixel(true);
                            }

                            if (spriteMask) {
                                spriteImage->overwriteMask(maskColors[(l - 1)]);
                            }

                            auto spriteSize = spriteImage->getSize() / g_gameConfig.getSpriteSize();

                            const Point& spritePos = Point(m_size.width() - spriteSize.width(), m_size.height() - spriteSize.height()) * g_gameConfig.getSpriteSize();
                            fullImage->blit(framePos + spritePos, spriteImage);
                        } else {
                            for (int h = 0; h < m_size.height(); ++h) {
                                for (int w = 0; w < m_size.width(); ++w) {
                                    const uint32_t spriteIndex = getSpriteIndex(w, h, spriteMask ? 1 : l, x, y, z, animationPhase);
                                    auto spriteId = m_spritesIndex[spriteIndex];
                                    bool isLoading = false;
                                    auto spriteImage = g_sprites.getSpriteImage(spriteId, isLoading);

                                    // Otro hilo esta descomprimiendo esta hoja. Lo normal es abortar y
                                    // reintentar en otro fotograma, pero componiendo un suelo al momento eso
                                    // lo dejaria sin pintar: se espera a la hoja, como mucho 8 ms (mas
                            // seria un tiron; si no llega, sale en el siguiente fotograma).
                                    for (int espera = 0; isLoading && s_esperarHojas && espera < 50; ++espera) {
                                        stdext::millisleep(1);
                                        isLoading = false;
                                        spriteImage = g_sprites.getSpriteImage(spriteId, isLoading);
                                    }

                                    if (isLoading)
                                        return;

                                    if (!spriteImage) {
                                        // Skip blank sprites silently (clients converted with Assets Editor have blank sprites with non-zero IDs)
                                        continue;
                                    }

                                    // verifies that the first block in the lower right corner is transparent.
                                    if (h == 0 && w == 0 && (!spriteImage || spriteImage->hasTransparentPixel())) {
                                        fullImage->setTransparentPixel(true);
                                    }

                                    if (spriteMask) {
                                        spriteImage->overwriteMask(maskColors[(l - 1)]);
                                    }

                                    const Point& spritePos = Point(m_size.width() - w - 1, m_size.height() - h - 1) * g_gameConfig.getSpriteSize();
                                    fullImage->blit(framePos + spritePos, spriteImage);
                                }
                            }
                        }
                    }

                    auto& posData = textureData.pos[frameIndex];
                    posData.rects = { framePos + Point(m_size.width(), m_size.height()) * g_gameConfig.getSpriteSize() - Point(1), framePos };
                    for (int fx = framePos.x; fx < framePos.x + m_size.width() * g_gameConfig.getSpriteSize(); ++fx) {
                        for (int fy = framePos.y; fy < framePos.y + m_size.height() * g_gameConfig.getSpriteSize(); ++fy) {
                            const uint8_t* p = fullImage->getPixel(fx, fy);
                            if (p[3] == 0x00)
                                continue;

                            posData.rects.setTop(std::min<int>(fy, posData.rects.top()));
                            posData.rects.setLeft(std::min<int>(fx, posData.rects.left()));
                            posData.rects.setBottom(std::max<int>(fy, posData.rects.bottom()));
                            posData.rects.setRight(std::max<int>(fx, posData.rects.right()));
                        }
                    }

                    posData.originRects = Rect(framePos, Size(m_size.width(), m_size.height()) * g_gameConfig.getSpriteSize());
                    posData.offsets = posData.rects.topLeft() - framePos;
                }
            }
        }
    }

    if (m_opacity < 1.0f)
        fullImage->setTransparentPixel(true);

    if (m_opaque == -1)
        m_opaque = !fullImage->hasTransparentPixel();

    // Sin mipmaps: el segundo parametro es buildMipmaps.
    //
    // Se generaban en la CPU, nivel a nivel, con un recorrido por pixel en cada
    // uno (texture.cpp: bucle sobre Image::nextMipmap). Para un atlas de outfit de
    // 8 MB en HD eso es una pasada completa mas ~2,7 MB de niveles extra y una
    // docena de subidas de mas, y todo ocurre en el HILO PRINCIPAL, porque la
    // ventana de outfits compone sus texturas de forma sincrona.
    //
    // Y no se usan: los sprites se dibujan AMPLIADOS (64 px a ~96), asi que la GPU
    // siempre muestrea el nivel 0. Solo se notarian en una vista previa muy
    // reducida, y ahi GL_LINEAR basta.
    if (m_category == ThingCategoryItem && !useCustomImage) {
        // Volcado de referencia de la lava de magma (21478-21499): la textura que se
        // pinta DE VERDAD, no lo que hay en la hoja. Solo las primeras. Bajo try
        // porque savePNG lanza si no puede escribir, y esto corre en el hilo que
        // compone: una excepcion ahi tumba el cliente.
        if (m_id >= 21478 && m_id <= 21499 && animationPhase < 2
            && s_lavaVolcadas.fetch_add(1, std::memory_order_relaxed) < 4) {
            try {
                fullImage->savePNG("lava_" + std::to_string(m_id) + "_fase" + std::to_string(animationPhase) + ".png");
            } catch (...) {}
        }

        // Autocomprobacion de SUELOS COMPLETOS. Dentro de cada fotograma de la
        // textura compuesta no puede haber ni un pixel transparente: si lo hay, al
        // pintarse se ve el fondo a traves, y bajo tierra el fondo es negro. Se
        // cuenta, y las primeras defectuosas se vuelcan a PNG para poder mirarlas.
        if (isFullGround()) {
            const int fw = m_size.width() * g_gameConfig.getSpriteSize();
            const int fh = m_size.height() * g_gameConfig.getSpriteSize();
            const int cols = std::max<int>(1, textureSize.width() / m_size.width());
            const int tw = fullImage->getWidth();
            const uint8_t* px = fullImage->getPixelData();

            uint32_t huecos = 0;
            for (int f = 0; f < indexSize; ++f) {
                const int ox = (f % cols) * fw;
                const int oy = (f / cols) * fh;
                for (int y = 0; y < fh; ++y)
                    for (int x = 0; x < fw; ++x)
                        if (px[(static_cast<size_t>(oy + y) * tw + ox + x) * 4 + 3] == 0)
                            ++huecos;
            }

            if (huecos > 0) {
                s_sueloHuecoTexturas.fetch_add(1, std::memory_order_relaxed);
                s_sueloHuecoPixeles.fetch_add(huecos, std::memory_order_relaxed);
                s_sueloHuecoUltimoId.store(m_id, std::memory_order_relaxed);
                if (s_sueloHuecoVolcados.fetch_add(1, std::memory_order_relaxed) < 3) {
                    try {
                        fullImage->savePNG("hueco_suelo_" + std::to_string(m_id) + "_fase" + std::to_string(animationPhase) + ".png");
                    } catch (...) {}
                }
            }
        }
    }

    textureData.source = std::make_shared<Texture>(fullImage, false, false);
    textureData.source->allowAtlasCache();
}

Size ThingType::getBestTextureDimension(int w, int h, const int count)
{
    int k = 1;
    while (k < w)
        k <<= 1;
    w = k;

    k = 1;
    while (k < h)
        k <<= 1;
    h = k;

    // Tope del lado de la textura EN TILES. Antes se usaba getSpriteSize(), que
    // son PIXELES: con sprite-size 32 coincidia por casualidad, pero con 64 el
    // calculo se descuadra y el fullImage se queda corto, con lo que Image::blit
    // escribe fuera del buffer y corrompe el heap.
    static constexpr int MAX_TEXTURE_TILES = 32;

    const int numSprites = w * h * count;
    assert(numSprites <= MAX_TEXTURE_TILES * MAX_TEXTURE_TILES);
    assert(w <= MAX_TEXTURE_TILES);
    assert(h <= MAX_TEXTURE_TILES);

    Size bestDimension = { MAX_TEXTURE_TILES };
    for (int i = w; i <= MAX_TEXTURE_TILES; i <<= 1) {
        for (int j = h; j <= MAX_TEXTURE_TILES; j <<= 1) {
            Size candidateDimension = { i, j };
            if (candidateDimension.area() < numSprites)
                continue;
            if ((candidateDimension.area() < bestDimension.area()) ||
                (candidateDimension.area() == bestDimension.area() && candidateDimension.width() + candidateDimension.height() < bestDimension.width() + bestDimension.height()))
                bestDimension = candidateDimension;
        }
    }

    return bestDimension;
}

uint32_t ThingType::getSpriteIndex(const int w, const int h, const int l, const int x, const int y, const int z, const int a) const
{
    uint32_t index = ((((((a % m_animationPhases)
                      * m_numPatternZ + z)
                      * m_numPatternY + y)
                      * m_numPatternX + x)
                      * m_layers + l)
        * m_size.height() + h)
        * m_size.width() + w;

    if (w == -1 && h == -1) { // protobuf does not use width and height, because sprite image is the exact sprite size, not split by 32x32, so -1 is passed instead
        index = ((((a % m_animationPhases)
                 * m_numPatternZ + z)
                 * m_numPatternY + y)
            * m_numPatternX + x)
            * m_layers + l;
    }

    assert(index < m_spritesIndex.size());
    return index;
}

uint32_t ThingType::getTextureIndex(const int l, const int x, const int y, const int z) const
{
    return ((l * m_numPatternZ + z)
        * m_numPatternY + y)
        * m_numPatternX + x;
}

int ThingType::getExactSize(const int layer, const int xPattern, const int yPattern, const int zPattern, const int animationPhase)
{
    if (m_null)
        return 0;

    if (!getTexture(animationPhase)) // we must calculate it anyway.
        return 0;

    const int frameIndex = getTextureIndex(layer, xPattern, yPattern, zPattern);
    const auto& pos = m_textureData[animationPhase].pos;

    const auto& textureDataPos = pos[std::min<int>(frameIndex, pos.size() - 1)];
    const auto& size = textureDataPos.originRects.size() - textureDataPos.offsets.toSize();
    return std::max<int>(size.width(), size.height());
}

void ThingType::setPathable(const bool var)
{
    if (var == true)
        m_flags &= ~ThingFlagAttrNotPathable;
    else
        m_flags |= ThingFlagAttrNotPathable;
}

int ThingType::getExactHeight()
{
    if (m_null)
        return 0;

    if (m_exactHeight != 0)
        return m_exactHeight;

    getTexture(0);
    const int frameIndex = getTextureIndex(0, 0, 0, 0);

    const auto& textureDataPos = m_textureData[0].pos[frameIndex];
    const Size& size = textureDataPos.originRects.size() - textureDataPos.offsets.toSize();
    return m_exactHeight = size.height();
}

std::vector<ThingType::RedEye> ThingType::getRedEyes(const int xPattern, const int yPattern, const int zPattern, const int animationPhase)
{
    // Solo sprites de tamano exacto (appearances / protobuf), que son los del 15.x.
    if (m_null || m_animationPhases == 0 || !(m_fromAppearances || g_game.isUsingProtobuf()))
        return {};

    const uint32_t spriteIndex = getSpriteIndex(-1, -1, 0, xPattern, yPattern, zPattern, animationPhase);
    if (spriteIndex >= m_spritesIndex.size())
        return {};
    const uint32_t spriteId = m_spritesIndex[spriteIndex];
    const int spriteSize = g_gameConfig.getSpriteSize();

    // la clave lleva el modo HD: el mismo sprite tiene otra imagen en HD
    static std::mutex mutex;
    static std::unordered_map<uint64_t, std::vector<RedEye>> cache;
    const uint64_t clave = (static_cast<uint64_t>(m_id) << 33) | (static_cast<uint64_t>(spriteId) << 1) | (spriteSize > 32 ? 1u : 0u);
    {
        std::lock_guard lock(mutex);
        if (const auto it = cache.find(clave); it != cache.end())
            return it->second;
    }

    bool isLoading = false;
    const auto image = g_sprites.getSpriteImage(spriteId, isLoading);
    if (isLoading || !image || image->getBpp() < 4)
        return {};

    // mismo criterio que eye_flame.frag: rojo saturado (sat >= 0.5), con rojo >= 0.40
    // y al menos el doble que verde y azul
    const int w = image->getSize().width();
    const int h = image->getSize().height();
    std::vector<uint8_t> marca(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = image->getPixel(x, y);
            const int r = p[0], g = p[1], b = p[2];
            const int mx = std::max({ r, g, b });
            const int mn = std::min({ r, g, b });
            if (p[3] >= 128 && mx > 0 && (mx - mn) * 2 >= mx && r >= 102 && r >= 2 * g && r >= 2 * b)
                marca[static_cast<size_t>(y) * w + x] = 1;
        }
    }

    // la imagen va pegada abajo a la derecha del cuadro, igual que en loadTexture
    const Point colocado = Point(m_size.width() - w / spriteSize, m_size.height() - h / spriteSize) * spriteSize;

    // cada grupo de pixeles rojos (8 vecinos) es un ojo
    std::vector<RedEye> ojos;
    std::vector<int> pila;
    for (int i = 0; i < w * h; ++i) {
        if (marca[i] != 1)
            continue;
        int minY = h, minX = w, maxX = -1, cuenta = 0;
        double sumaX = 0;
        marca[i] = 2;
        pila.push_back(i);
        while (!pila.empty()) {
            const int k = pila.back();
            pila.pop_back();
            const int kx = k % w, ky = k / w;
            minY = std::min(minY, ky);
            minX = std::min(minX, kx);
            maxX = std::max(maxX, kx);
            sumaX += kx;
            ++cuenta;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = kx + dx, ny = ky + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const int j = ny * w + nx;
                    if (marca[j] == 1) {
                        marca[j] = 2;
                        pila.push_back(j);
                    }
                }
            }
        }
        RedEye ojo;
        ojo.top = PointF(colocado.x + static_cast<float>(sumaX / cuenta) + 0.5f, static_cast<float>(colocado.y + minY));
        ojo.width = static_cast<float>(maxX - minX + 1);
        ojos.push_back(ojo);
    }

    std::lock_guard lock(mutex);
    return cache[clave] = std::move(ojos);
}

ThingFlagAttr ThingType::thingAttrToThingFlagAttr(const ThingAttr attr) {
    switch (attr) {
        case ThingAttrDisplacement: return ThingFlagAttrDisplacement;
        case ThingAttrLight: return ThingFlagAttrLight;
        case ThingAttrElevation: return ThingFlagAttrElevation;
        case ThingAttrFloorChange: return ThingFlagAttrFloorChange;
        case ThingAttrGround: return ThingFlagAttrGround;
        case ThingAttrWritable: return ThingFlagAttrWritable;
        case ThingAttrWritableOnce: return ThingFlagAttrWritableOnce;
        case ThingAttrMinimapColor: return ThingFlagAttrMinimapColor;
        case ThingAttrCloth: return ThingFlagAttrCloth;
        case ThingAttrLensHelp: return ThingFlagAttrLensHelp;
        case ThingAttrDefaultAction: return ThingFlagAttrDefaultAction;
        case ThingAttrUsable: return ThingFlagAttrUsable;
        case ThingAttrGroundBorder: return ThingFlagAttrGroundBorder;
        case ThingAttrOnBottom: return ThingFlagAttrOnBottom;
        case ThingAttrOnTop: return ThingFlagAttrOnTop;
        case ThingAttrContainer: return ThingFlagAttrContainer;
        case ThingAttrStackable: return ThingFlagAttrStackable;
        case ThingAttrForceUse: return ThingFlagAttrForceUse;
        case ThingAttrMultiUse: return ThingFlagAttrMultiUse;
        case ThingAttrChargeable: return ThingFlagAttrChargeable;
        case ThingAttrFluidContainer: return ThingFlagAttrFluidContainer;
        case ThingAttrSplash: return ThingFlagAttrSplash;
        case ThingAttrNotWalkable: return ThingFlagAttrNotWalkable;
        case ThingAttrNotMoveable: return ThingFlagAttrNotMoveable;
        case ThingAttrBlockProjectile: return ThingFlagAttrBlockProjectile;
        case ThingAttrNotPathable: return ThingFlagAttrNotPathable;
        case ThingAttrPickupable: return ThingFlagAttrPickupable;
        case ThingAttrHangable: return ThingFlagAttrHangable;
        case ThingAttrHookSouth: return ThingFlagAttrHookSouth;
        case ThingAttrHookEast: return ThingFlagAttrHookEast;
        case ThingAttrRotateable: return ThingFlagAttrRotateable;
        case ThingAttrDontHide: return ThingFlagAttrDontHide;
        case ThingAttrTranslucent: return ThingFlagAttrTranslucent;
        case ThingAttrLyingCorpse: return ThingFlagAttrLyingCorpse;
        case ThingAttrAnimateAlways: return ThingFlagAttrAnimateAlways;
        case ThingAttrFullGround: return ThingFlagAttrFullGround;
        case ThingAttrLook: return ThingFlagAttrLook;
        case ThingAttrWrapable: return ThingFlagAttrWrapable;
        case ThingAttrUnwrapable: return ThingFlagAttrUnwrapable;
        case ThingAttrWearOut: return ThingFlagAttrWearOut;
        case ThingAttrClockExpire: return ThingFlagAttrClockExpire;
        case ThingAttrExpire: return ThingFlagAttrExpire;
        case ThingAttrExpireStop: return ThingFlagAttrExpireStop;
        case ThingAttrPodium: return ThingFlagAttrPodium;
        case ThingAttrTopEffect: return ThingFlagAttrTopEffect;
        case ThingAttrMarket: return ThingFlagAttrMarket;
        case ThingAttrDecoKit: return ThingFlagAttrDecoKit;
        default: break;
    }

    return ThingFlagAttrNone;
}

bool ThingType::isTall(const bool useRealSize) { return useRealSize ? getRealSize() > g_gameConfig.getSpriteSize() : getHeight() > 1; }
int ThingType::getAnimationPhases() const { return m_animator ? m_animator->getAnimationPhases() : m_animationPhases; }
int ThingType::getIdleAnimationPhases() const {
    if (m_idleAnimator) return m_idleAnimator->getAnimationPhases();
    if (m_animator) return std::max(0, m_animationPhases - m_animator->getAnimationPhases());
    return 0;
}

int ThingType::getMeanPrice() {
    static constexpr std::array<std::pair<uint32_t, uint32_t>, 3> forcedPrices = { {
        {3043, 10000},// Crystal Coin
        {3031, 1}, // Gold Coin
        {3035, 100} // Platinum Coin
    } };

    const uint32_t itemId = getId();

    const auto it = std::ranges::find_if(forcedPrices, [itemId](const auto& pair) { return pair.first == itemId; });

    if (it != forcedPrices.end()) {
        return it->second;
    }

    const auto npcCount = m_npcData.size();
    if (npcCount == 0) {
        return 0;
    }

    const int totalBuyPrice = std::accumulate(m_npcData.begin(), m_npcData.end(), 0,
        [](int sum, const auto& npc) { return sum + npc.buyPrice; });

    return totalBuyPrice / static_cast<int>(npcCount);
}

#ifdef FRAMEWORK_EDITOR
void ThingType::serialize(const FileStreamPtr& fin)
{
    for (int i = 0; i < ThingLastAttr; ++i) {
        int attr = i;
        if (g_game.getClientVersion() >= 780) {
            if (attr == ThingAttrChargeable)
                attr = ThingAttrWritable;
            else if (attr >= ThingAttrWritable)
                attr += 1;
        } else if (g_game.getClientVersion() >= 1000) {
            if (attr == ThingAttrNoMoveAnimation)
                attr = 16;
            else if (attr >= ThingAttrPickupable)
                attr += 1;
        }

        if (!hasAttr(static_cast<ThingAttr>(attr)))
            continue;

        switch (attr) {
            case ThingAttrDisplacement:
            {
                fin->addU16(m_displacement.x);
                fin->addU16(m_displacement.y);
                break;
            }
            case ThingAttrLight:
            {
                fin->addU16(m_light.intensity);
                fin->addU16(m_light.color);
                break;
            }
            case ThingAttrMarket:
            {
                fin->addU16(m_market.category);
                fin->addU16(m_market.tradeAs);
                fin->addU16(m_market.showAs);
                fin->addString(m_market.name);
                fin->addU16(m_market.restrictVocation);
                fin->addU16(m_market.requiredLevel);
                break;
            }

            case ThingAttrElevation: fin->addU16(m_elevation); break;
            case ThingAttrMinimapColor: fin->add16(m_minimapColor); break;
            case ThingAttrCloth: fin->add16(m_clothSlot); break;
            case ThingAttrLensHelp: fin->add16(m_lensHelp); break;
            case ThingAttrUsable: fin->add16(isUsable()); break;
            case ThingAttrGround:  fin->add16(isGround()); break;
            case ThingAttrWritable:   fin->add16(isWritable()); break;
            case ThingAttrWritableOnce:   fin->add16(isWritableOnce()); break;
                break;

            default:
                break;
        }
    }
    fin->addU8(ThingLastAttr);

    fin->addU8(m_size.width());
    fin->addU8(m_size.height());

    if (m_size.width() > 1 || m_size.height() > 1)
        fin->addU8(m_realSize);

    fin->addU8(m_layers);
    fin->addU8(m_numPatternX);
    fin->addU8(m_numPatternY);
    fin->addU8(m_numPatternZ);
    fin->addU8(m_animationPhases);

    if (g_game.getFeature(Otc::GameEnhancedAnimations)) {
        if (m_animationPhases > 1 && m_animator) {
            m_animator->serialize(fin);
        }
    }

    for (const int i : m_spritesIndex) {
        if (g_game.getFeature(Otc::GameSpritesU32))
            fin->addU32(i);
        else
            fin->addU16(i);
    }
}

void ThingType::exportImage(const std::string& fileName)
{
    if (m_null)
        throw Exception("cannot export null thingtype");

    if (m_spritesIndex.empty())
        throw Exception("cannot export thingtype without sprites");

    const auto& image = std::make_shared<Image>(Size(g_gameConfig.getSpriteSize() * m_size.width() * m_layers * m_numPatternX, g_gameConfig.getSpriteSize() * m_size.height() * m_animationPhases * m_numPatternY * m_numPatternZ));
    for (int z = 0; z < m_numPatternZ; ++z) {
        for (int y = 0; y < m_numPatternY; ++y) {
            for (int x = 0; x < m_numPatternX; ++x) {
                for (int l = 0; l < m_layers; ++l) {
                    for (int a = 0; a < m_animationPhases; ++a) {
                        for (int w = 0; w < m_size.width(); ++w) {
                            for (int h = 0; h < m_size.height(); ++h) {
                                image->blit(Point(g_gameConfig.getSpriteSize() * (m_size.width() - w - 1 + m_size.width() * x + m_size.width() * m_numPatternX * l),
                                            g_gameConfig.getSpriteSize() * (m_size.height() - h - 1 + m_size.height() * y + m_size.height() * m_numPatternY * a + m_size.height() * m_numPatternY * m_animationPhases * z)),
                                    g_sprites.getSpriteImage(m_spritesIndex[getSpriteIndex(w, h, l, x, y, z, a)]));
                            }
                        }
                    }
                }
            }
        }
    }

    image->savePNG(fileName);
}
#endif
