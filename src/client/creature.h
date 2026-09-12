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

#pragma once

#include "outfit.h"
#include "thing.h"
#include <framework/core/declarations.h>
#include <framework/graphics/cachedtext.h>

#include "staticdata.h"

 // @bindclass
class Creature : public Thing
{
public:
    static double speedA;
    static double speedB;
    static double speedC;

    Creature();
    ~Creature() override;

    static bool hasSpeedFormula();

    void onCreate();

    void onAppear() override;
    void onDisappear() override;

    void draw(const Point& dest, bool drawThings = true, LightView* lightView = nullptr) override;
    void draw(const Rect& destRect, uint8_t size, bool center = false);
    void drawLight(const Point& dest, LightView* lightView) override;

    void internalDraw(Point dest, const Color& color = Color::white);
    void drawInformation(const MapPosInfo& mapRect, const Point& dest, int drawFlags);

    void setId(const uint32_t id) override { m_id = id; }
    void setMasterId(const uint32_t id) { m_masterId = id; }
    void setName(std::string_view name);
    void setHealthPercent(uint8_t healthPercent);
    void setManaPercent(uint8_t value) { m_manaPercent = value; }
    void setDirection(Otc::Direction direction);
    void setOutfit(const Outfit& outfit, bool fireEvent = true);
    void setLight(const Light& light) { m_light = light; }
    void setSpeed(uint16_t speed);
    void setBaseSpeed(uint16_t baseSpeed);
    void setSkull(uint8_t skull);
    void setShield(uint8_t shield);
    void setEmblem(uint8_t emblem);
    void setType(uint8_t type);
    void setIcon(uint8_t icon);
    void setIcons(const std::vector<std::tuple<uint8_t, uint8_t, uint16_t>>& icons);
    void setSkullTexture(const std::string& filename);
    void setShieldTexture(const std::string& filename, bool blink);
    void setEmblemTexture(const std::string& filename);
    void setTypeTexture(const std::string& filename);
    void setIconTexture(const std::string& filename);
    void setPassable(const bool passable) { m_passable = passable; }
    void setMountShader(std::string_view name);
    void setStaticWalking(uint16_t v);
    void setIconsTexture(const std::string& filename, const Rect& clip, const uint16_t count);

    void onStartAttachEffect(const AttachedEffectPtr& effect) override;
    void onDispatcherAttachEffect(const AttachedEffectPtr& effect) override;
    void onStartDetachEffect(const AttachedEffectPtr& effect) override;

    void addTimedSquare(uint8_t color);
    void removeTimedSquare() { m_showTimedSquare = false; }
    void showStaticSquare(const Color& color) { m_showStaticSquare = true; m_staticSquareColor = color; }
    void hideStaticSquare() { m_showStaticSquare = false; }

    // walk related
    void turn(Otc::Direction direction);
    void jump(int height, int duration);
    void allowAppearWalk() { m_allowAppearWalk = true; }
    virtual void walk(const Position& oldPos, const Position& newPos);
    virtual void stopWalk();

    bool isDrawingOutfitColor() const { return m_drawOutfitColor; }
    void setDrawOutfitColor(const bool draw) { m_drawOutfitColor = draw; }

    int getDisplacementX() const override;
    int getDisplacementY() const override;
    int getExactSize(int layer = 0, int xPattern = 0, int yPattern = 0, int zPattern = 0, int animationPhase = 0) override;

    float getStepProgress() { return m_walkTimer.ticksElapsed() / static_cast<float>(m_stepCache.duration); }
    float getStepTicksLeft() { return static_cast<float>(m_stepCache.getDuration(m_lastStepDirection)) - m_walkTimer.ticksElapsed(); }

    uint8_t getSkull() { return m_skull; }
    uint8_t getShield() { return m_shield; }
    uint8_t getEmblem() { return m_emblem; }
    uint8_t getType() { return m_type; }
    uint8_t getIcon() { return m_icon; }
    uint8_t getHealthPercent() { return m_healthPercent; }
    uint8_t getManaPercent() { return m_manaPercent; }

    uint16_t getSpeed() { return m_speed; }
    uint16_t getBaseSpeed() { return m_baseSpeed; }
    uint16_t getStepDuration(bool ignoreDiagonal = false, Otc::Direction dir = Otc::InvalidDirection);

    uint32_t getId() override { return m_id; }
    uint32_t getMasterId() { return m_masterId; }
    std::string getName() { return m_name.getText(); }

    Point getDrawOffset() { return Point(-1, -1) * getDrawElevation() + m_walkOffset; }
    int getDrawElevation();

    Otc::Direction getDirection() { return m_direction; }
    const auto& getOutfit() { return m_outfit; }
    const Light& getLight() const override;
    bool hasLight() const override { return Thing::hasLight() || getLight().intensity > 0; }
    bool hasMountShader() const { return m_mountShaderId > 0; }

    Point getDisplacement() const override;
    Point getWalkOffset() { return m_walkOffset; }

    // El mismo desplazamiento del paso, pero ya en pixeles del FRAMEBUFFER y sin
    // pasar por el redondeo a pixeles de sprite.
    //
    // El framebuffer del mapa tiene scaleFactor pixeles por cada pixel de sprite
    // (mapview.cpp: tileSize = spriteSize * scaleFactor). Calcular el avance en
    // enteros de sprite y multiplicarlo despues por la escala tiraba 2 de cada 3
    // posiciones posibles con zoom x3: el mundo avanzaba a saltos de 3 pixeles de
    // pantalla, y por eso subir los FPS no suavizaba nada.
    // Se calcula AQUI, en el instante de dibujar, en vez de leer un valor ya
    // guardado. Las criaturas que no lleva la camara solo recalculan su avance
    // cada walkDuration (como mucho 16 ms), asi que en un monitor rapido su
    // posicion se quedaria congelada varios fotogramas. Leyendo el reloj al
    // dibujar, sale exacta con cualquier refresco y sin programar mas eventos.
    Point getWalkOffsetScaled(const float scale) const {
        const auto& off = currentWalkOffset();
        return { static_cast<int>(std::lround(off.x * scale)),
                 static_cast<int>(std::lround(off.y * scale)) };
    }

    // El mismo valor SIN redondear. La camara necesita el entero para elegir el
    // texel del framebuffer, pero la parte decimal ya no se tira: se aplica al
    // muestrearlo (MapPosInfo::subPixel) y el mundo se desplaza en sub-pixel.
    PointF getWalkOffsetScaledF(const float scale) const {
        const auto& off = currentWalkOffset();
        return { off.x * scale, off.y * scale };
    }

    PointF getJumpOffset() { return m_jumpOffset; }
    Position getLastStepFromPosition() const { return m_lastStepFromPosition; }
    Position getLastStepToPosition() const { return m_lastStepToPosition; }
    bool isTimedSquareVisible() { return m_showTimedSquare; }
    Color getTimedSquareColor() { return m_timedSquareColor; }
    bool isStaticSquareVisible() { return m_showStaticSquare; }
    Color getStaticSquareColor() { return m_staticSquareColor; }

    ticks_t getWalkTicksElapsed() { return m_walkTimer.ticksElapsed(); }

    bool isPassable() const { return m_passable; }
    bool isWalking() { return m_walking; }

    bool isRemoved() { return m_removed; }
    bool isRemoved() const { return m_removed; }
    const Position& getOldPosition() const { return m_oldPosition; }
    bool isInvisible() { return m_outfit.isEffect() && m_outfit.getAuxId() == 13; }
    bool isDead() { return m_healthPercent <= 0; }
    bool isHidden() const;
    bool isFullHealth() { return m_healthPercent == 100; }
    bool canBeSeen() { return !isInvisible() || isPlayer(); }
    bool isCreature() const override { return true; }
    bool isCovered() { return m_isCovered; }

    void setCovered(bool covered);

    bool isDisabledWalkAnimation() { return m_disableWalkAnimation > 0; }
    void setDisableWalkAnimation(const bool v) {
        if (v) ++m_disableWalkAnimation; else {
            if (m_disableWalkAnimation <= 1) m_disableWalkAnimation = 0;
            else --m_disableWalkAnimation;
        }
    }

    void setTyping(bool typing);
    void sendTyping();
    bool getTyping() { return m_typing; }
    void setTypingIconTexture(const std::string& filename);
    void setBounce(const uint8_t minHeight, const uint8_t height, const uint16_t speed) {
        m_bounce = { .minHeight =
minHeight,
.height = height, .speed = speed
        };
    }

    void setWidgetInformation(const UIWidgetPtr& info);
    UIWidgetPtr getWidgetInformation() { return m_widgetInformation; }

    void setNameShader(const std::string& name) { m_nameShader = name; }
    std::string getNameShader() { return m_nameShader; }

    void setText(const std::string& text, const Color& color);
    std::string getText();
    void clearText() { setText("", Color::white); }
    bool canShoot(int distance);

    const auto& getIcons() {
        static std::vector<std::tuple<uint8_t, uint8_t, uint16_t>> vec;
        return m_icons ? m_icons->iconEntries : vec;
    }

    bool isCameraFollowing() const {
        return m_cameraFollowing;
    }

    void setCameraFollowing(bool v) {
        m_cameraFollowing = v;
    }

    void setVocation(uint8_t vocation) { m_vocation = vocation; }
    uint8_t getVocation() { return m_vocation; }

    void attachPaperdoll(const PaperdollPtr& obj);
    void clearPaperdolls();
    bool hasPaperdoll(uint16_t id);

    bool detachPaperdollById(uint16_t id);
    bool detachPaperdollByPriority(uint8_t priority);

    PaperdollPtr getPaperdollById(uint16_t id);

    const std::vector<PaperdollPtr>& getPaperdolls() { return m_paperdolls; };

protected:
    virtual void terminateWalk();
    virtual void onWalking() {};
    void updateWalkOffset(uint8_t totalPixelsWalked);
    PointF currentWalkOffset() const;
    static PointF walkOffsetFor(Otc::Direction dir, float walked, float size);
    void updateWalk();

    void setOldPositionSilently(const Position& pos) { m_oldPosition = pos; }
    void setRemovedSilently(const bool removed) { m_removed = removed; }

    void onDetachPaperdoll(const PaperdollPtr& paperdoll);
    void setPaperdollsDirection(Otc::Direction dir) const;

    ThingType* getThingType() const override;
    ThingType* getMountThingType() const;

    void onDeath();
    void onPositionChange(const Position& newPos, const Position& oldPos) override;

    bool m_walking{ false };

    Point m_walkOffset;
    PointF m_walkOffsetF;   // igual que m_walkOffset pero sin cuantizar, solo para dibujar
    Otc::Direction m_direction{ Otc::South };

    Timer m_walkTimer;

    int16_t m_lastMapDuration = -1;

private:
    void nextWalkUpdate();
    void updateJump();
    void updateShield();
    void updateWalkingTile();
    void updateWalkAnimation();

    uint16_t getCurrentAnimationPhase(bool mount = false);

    struct CachedStep
    {
        uint16_t speed{ 0 };
        uint16_t groundSpeed{ 0 };

        uint16_t duration{ 0 };
        uint16_t walkDuration{ 0 };
        uint16_t diagonalDuration{ 0 };

        uint16_t getDuration(const Otc::Direction dir) const { return Position::isDiagonal(dir) ? diagonalDuration : duration; }
    };

    struct IconRenderData
    {
        struct AtlasIconGroup
        {
            TexturePtr texture;
            Rect clip;
            uint16_t count{ 0 };
        };

        std::vector<AtlasIconGroup> atlasGroups;
        std::vector<std::tuple<uint8_t, uint8_t, uint16_t>> iconEntries; // (icon, category, count)
        CachedText numberText;
    };

    std::vector<PaperdollPtr> m_paperdolls;

    UIWidgetPtr m_widgetInformation;

    TilePtr m_walkingTile;

    std::unique_ptr<IconRenderData> m_icons;

    TexturePtr m_skullTexture;
    TexturePtr m_shieldTexture;
    TexturePtr m_emblemTexture;
    TexturePtr m_typeTexture;
    TexturePtr m_iconTexture;
    TexturePtr m_typingIconTexture;

    EventPtr m_walkUpdateEvent;
    ScheduledEventPtr m_walkFinishAnimEvent;
    ScheduledEventPtr m_outfitColorUpdateEvent;

    EventPtr m_disappearEvent;

    CachedText m_name;
    std::string m_nameShader;
    CachedStep m_stepCache;

    Position m_lastStepToPosition;
    Position m_lastStepFromPosition;
    Position m_oldPosition;

    Timer m_footTimer;
    Timer m_outfitColorTimer;

    Outfit m_outfit;
    Light m_light;

    Color m_timedSquareColor{ Color::white };
    Color m_staticSquareColor{ Color::white };
    Color m_informationColor{ Color::white };

    Bounce m_bounce;

    // jump related
    Timer m_jumpTimer;
    PointF m_jumpOffset;
    float m_jumpHeight{ 0 };
    float m_jumpDuration{ 0 };

    uint32_t m_id{ 0 };
    uint32_t m_masterId{ 0 };

    uint16_t m_calculatedStepSpeed{ 0 };
    uint16_t m_speed{ 0 };
    uint16_t m_baseSpeed{ 0 };
    uint16_t m_walkingAnimationSpeed{ 0 };

    uint8_t m_type;
    uint8_t m_healthPercent{ 101 };
    uint8_t m_manaPercent{ 101 };
    uint8_t m_skull{ Otc::SkullNone };
    uint8_t m_icon{ Otc::NpcIconNone };
    uint8_t m_shield{ Otc::ShieldNone };
    uint8_t m_emblem{ Otc::EmblemNone };

    // walk related
    uint8_t m_walkAnimationPhase{ 0 };
    uint8_t m_walkedPixels{ 0 };
    float m_walkedPixelsF{ 0.f };
    float m_walkStepDuration{ 0.f };   // ms del paso en curso, para interpolar al dibujar
    float m_walkAnimDistance{ 0.f };   // pixeles andados en toda la caminata, para el ciclo de piernas

    uint8_t m_exactSize{ 0 };

    uint8_t m_disableWalkAnimation{ 0 };

    // Mount Shader
    uint8_t m_mountShaderId{ 0 };

    Otc::Direction m_walkTurnDirection{ Otc::InvalidDirection };
    Otc::Direction m_lastStepDirection{ Otc::InvalidDirection };

    bool m_shieldBlink{ false };
    bool m_passable{ false };
    bool m_allowAppearWalk{ false };
    bool m_showTimedSquare{ false };
    bool m_showStaticSquare{ false };
    bool m_cameraFollowing{ false };

    bool m_removed{ true };
    bool m_drawOutfitColor{ true };
    bool m_showShieldTexture{ true };
    bool m_typing{ false };
    bool m_isCovered{ false };

    StaticTextPtr m_text;

    uint8_t m_vocation{ 0 };
};

// @bindclass
class Npc final : public Creature
{
public:
    bool isNpc() const override { return true; }
};

// @bindclass
class Monster final : public Creature
{
public:
    bool isMonster() const override { return true; }
};
