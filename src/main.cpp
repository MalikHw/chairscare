#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>

using namespace geode::prelude;

static CCSprite* s_scareSprite = nullptr;
static std::vector<std::string> s_scareImages;
static void loadScareImages() {
    s_scareImages.clear();
    auto resourcesDir = Mod::get()->getResourcesDir();
    for (auto& entry : std::filesystem::directory_iterator(resourcesDir)) {
        if (entry.path().extension() == ".png") {
            s_scareImages.push_back(entry.path().filename().string());
        }
    }
}
static std::string getRandomImage() {
    if (s_scareImages.empty()) loadScareImages();
    auto name = s_scareImages[rand() % s_scareImages.size()];
    return Mod::get()->expandSpriteName(name.c_str()).c_str();
}
static void triggerScare() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    // don't stack sprites
    if (s_scareSprite) {
        s_scareSprite->stopAllActions();
        s_scareSprite->removeFromParent();
        s_scareSprite = nullptr;
    }
    s_scareSprite = CCSprite::create(getRandomImage().c_str());
    if (!s_scareSprite) return;
    s_scareSprite->setID("chairscare-sprite");
    CCSize winSize = CCDirector::get()->getWinSize();
    float scale = winSize.height / s_scareSprite->getContentSize().height;
    s_scareSprite->setScale(scale);
    s_scareSprite->setPosition({ winSize.width / 2, winSize.height / 2 });
    s_scareSprite->setOpacity(255);
    scene->addChild(s_scareSprite, 100);
    FMODAudioEngine::sharedEngine()->playEffect("scare.mp3"_spr);
    auto remove = CCCallFunc::create([]() {
        if (s_scareSprite) {
            s_scareSprite->removeFromParent();
            s_scareSprite = nullptr;
        }
    });
    s_scareSprite->runAction(CCSequence::create(CCFadeOut::create(1.0f), remove, nullptr));
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
