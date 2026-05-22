#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// scare sprite globals
static CCSprite* s_scareSprite = nullptr;
static std::vector<std::string> s_scareImages = {
    "chair1.png"_spr,
    "chair2.png"_spr
};

static std::string getRandomImage() {
    return s_scareImages[rand() % s_scareImages.size()];
}
static void triggerScare() {
    if (s_scareSprite) {
        s_scareSprite->removeFromParentAndCleanup(true);
        s_scareSprite = nullptr;
    }
    auto img = getRandomImage();
    s_scareSprite = CCSprite::create(img.c_str());
    if (!s_scareSprite) return;
    s_scareSprite->setID("chairscare-sprite");
    CCSize winSize = CCDirector::get()->getWinSize();
    CCSize sprSize = s_scareSprite->getContentSize();
    s_scareSprite->setScaleX(winSize.width  / sprSize.width);
    s_scareSprite->setScaleY(winSize.height / sprSize.height);
    s_scareSprite->setPosition({ winSize.width / 2, winSize.height / 2 });
    s_scareSprite->setOpacity(255);
    geode::OverlayManager::get()->addChild(s_scareSprite);
    FMODAudioEngine::sharedEngine()->playEffect("scare.mp3"_spr);
    s_scareSprite->runAction(CCSequence::create(
        CCFadeOut::create(1.0f),
        CCRemoveSelf::create(),
        nullptr
    ));
}
static bool rollChance(double percent) {
    float roll = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 100.f;
    return roll <= static_cast<float>(percent);
}

// click mode
class $modify(ChairscarePlayer, PlayerObject) {
    struct Fields {
        bool m_holding = false;
    };
    void playerDestroyed(bool p0) {
        PlayerObject::playerDestroyed(p0);
        m_fields->m_holding = false;
    }
    bool releaseButton(PlayerButton p0) {
        bool ret = PlayerObject::releaseButton(p0);
        if (p0 == PlayerButton::Jump)
            m_fields->m_holding = false;
        return ret;
    }
    bool pushButton(PlayerButton p0) {
        bool ret = PlayerObject::pushButton(p0);
        if (p0 != PlayerButton::Jump) return ret;
        m_fields->m_holding = true;
        if (!Mod::get()->getSettingValue<bool>("enabled")) return ret;
        if (Mod::get()->getSettingValue<std::string>("trigger-mode") != "click based") return ret;
        if (!GameManager::sharedState()->getPlayLayer()) return ret;
        if (rollChance(Mod::get()->getSettingValue<double>("click-chance")))
            triggerScare();
        return ret;
    }
};

// timer mode
class $modify(ChairscarePlayLayer, PlayLayer) {
    struct Fields {
        float m_elapsed = 0.f;
        float m_nextInterval = 0.f;
    };
    float randomInterval() {
        double lo = Mod::get()->getSettingValue<double>("interval-min");
        double hi = Mod::get()->getSettingValue<double>("interval-max");
        if (lo > hi) std::swap(lo, hi);
        float r = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * static_cast<float>(hi - lo);
        return static_cast<float>(lo) + r;
    }
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->m_elapsed = 0.f;
        m_fields->m_nextInterval = randomInterval();
        this->schedule(schedule_selector(ChairscarePlayLayer::scareTick));
        return true;
    }
    void scareTick(float dt) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) return;
        if (Mod::get()->getSettingValue<std::string>("trigger-mode") != "time based") return;
        m_fields->m_elapsed += dt;
        if (m_fields->m_elapsed < m_fields->m_nextInterval) return;
        m_fields->m_elapsed = 0.f;
        m_fields->m_nextInterval = randomInterval();
        triggerScare();
    }
};