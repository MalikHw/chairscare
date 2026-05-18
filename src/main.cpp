#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

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
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (s_scareSprite) {
        s_scareSprite->stopAllActions();
        s_scareSprite->removeFromParent();
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
    scene->addChild(s_scareSprite, 100);
    FMODAudioEngine::sharedEngine()->playEffect("scare.mp3"_spr);
    s_scareSprite->runAction(CCSequence::create(
        CCFadeOut::create(1.0f),
        CCRemoveSelf::create(true),
        nullptr
    ));
}
static bool rollChance(double percent) {
    float roll = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 100.f;
    return roll <= static_cast<float>(percent);
}

// custom "Submit Chair" button setting, just opens a google form
class SubmitChairSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key,
        std::string const& modID,
        matjson::Value const& json
    ) {
        auto res  = std::make_shared<SubmitChairSettingV3>();
        auto root = checkJson(json, "SubmitChairSettingV3");
        res->init(key, modID, root);
        res->parseNameAndDescription(root);
        root.checkUnknownKeys();
        return root.ok(std::static_pointer_cast<SettingV3>(res));
    }
    bool load(matjson::Value const&) override { return true; }
    bool save(matjson::Value&) const override { return true; }
    bool isDefaultValue() const override { return true; }
    void reset() override {}
    SettingNodeV3* createNode(float width) override;
};

class SubmitChairSettingNodeV3 : public SettingNodeV3 {
protected:
    ButtonSprite*          m_buttonSprite;
    CCMenuItemSpriteExtra* m_button;

    bool init(std::shared_ptr<SubmitChairSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width)) return false;
        m_buttonSprite = ButtonSprite::create("Submit Chair", "bigFont.fnt", "GJ_button_01.png", .8f);
        m_buttonSprite->setScale(.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this,
            menu_selector(SubmitChairSettingNodeV3::onSubmit)
        );
        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Center);
        this->getButtonMenu()->setContentWidth(100);
        this->getButtonMenu()->updateLayout();
        this->updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto ok = this->getSetting()->shouldEnable();
        m_button->setEnabled(ok);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setOpacity(ok ? 255 : 155);
        m_buttonSprite->setColor(ok ? ccWHITE : ccGRAY);
    }

    void onSubmit(CCObject*) {
        web::openLinkInBrowser("https://forms.gle/zGDzNXyUjFeAKnwTA");
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static SubmitChairSettingNodeV3* create(
        std::shared_ptr<SubmitChairSettingV3> setting, float width
    ) {
        auto ret = new SubmitChairSettingNodeV3();
        if (ret->init(setting, width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }

    std::shared_ptr<SubmitChairSettingV3> getSetting() const {
        return std::static_pointer_cast<SubmitChairSettingV3>(SettingNodeV3::getSetting());
    }
};

SettingNodeV3* SubmitChairSettingV3::createNode(float width) {
    return SubmitChairSettingNodeV3::create(
        std::static_pointer_cast<SubmitChairSettingV3>(shared_from_this()),
        width
    );
}

$on_mod(Loaded) {
    (void)Mod::get()->registerCustomSettingType("submit-chair", &SubmitChairSettingV3::parse);
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
