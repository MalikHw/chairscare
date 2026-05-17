#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <filesystem>
#include <fstream>

using namespace geode::prelude;

// chairs zip/ver URLs
static const char* CHAIRS_ZIP_URL = "https://github.com/MalikHw/chairscare/raw/refs/heads/main/chairs.zip";
static const char* CHAIRS_VER_URL = "https://github.com/MalikHw/chairscare/raw/refs/heads/main/chairs-ver.txt";
// folder inside save dir where chairs are stored
static std::filesystem::path getChairsDir() {
    return Mod::get()->getSaveDir() / "chairs";
}
static std::filesystem::path getVerFile() {
    return Mod::get()->getSaveDir() / "chairs-ver.txt";
}
// scare sprite globals
static CCSprite* s_scareSprite = nullptr;
static std::vector<std::string> s_scareImages;

static void loadScareImages() {
    s_scareImages.clear();
    auto dir = getChairsDir();
    if (!std::filesystem::exists(dir)) return;
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        auto ext = entry.path().extension().string();
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp")
            s_scareImages.push_back(entry.path().string());
    }
}
static std::string getRandomImage() {
    if (s_scareImages.empty()) loadScareImages();
    if (s_scareImages.empty()) return "";
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
    if (img.empty()) return;
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
// extract a zip archive to a directory using Geode's file utils
static Result<> extractZipTo(std::vector<uint8_t> const& data, std::filesystem::path const& dir) {
    // write zip to a temp file first, then use file::Unzip
    auto tmpZip = Mod::get()->getSaveDir() / "chairs_tmp.zip";
    std::ofstream out(tmpZip, std::ios::binary);
    if (!out) return Err("Failed to write temp zip");
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();
    GEODE_UNWRAP(file::Unzip::intoDir(tmpZip, dir));
    std::filesystem::remove(tmpZip);
    return Ok();
}
// download chairs.zip, extract to save dir, reload image list
static void downloadAndInstallChairs(std::function<void(bool)> callback) {
    auto req = web::WebRequest();
    async::spawn(
        req.get(CHAIRS_ZIP_URL),
        [callback](web::WebResponse res) {
            if (!res.ok()) {
                FLAlertLayer::create("Download Failed", "Could not download chairs. Check your connection!", "OK")->show();
                if (callback) callback(false);
                return;
            }
            auto data = res.data();
            auto dir  = getChairsDir();
            if (std::filesystem::exists(dir))
                std::filesystem::remove_all(dir);
            std::filesystem::create_directories(dir);
            auto result = extractZipTo(data, dir);
            if (!result) {
                FLAlertLayer::create("Extract Failed", result.unwrapErr().c_str(), "OK")->show();
                if (callback) callback(false);
                return;
            }
            s_scareImages.clear(); // force reload next trigger
            if (callback) callback(true);
        }
    );
}
// save local version string to disk
static void saveLocalVersion(std::string const& ver) {
    std::ofstream f(getVerFile());
    if (f) f << ver;
}
// read local version string from disk
static std::string loadLocalVersion() {
    std::ifstream f(getVerFile());
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    return s;
}
// check remote version and show update popup if different
static void checkForChairUpdate() {
    auto req = web::WebRequest();
    async::spawn(
        req.get(CHAIRS_VER_URL),
        [](web::WebResponse res) {
            if (!res.ok()) return;
            auto remoteVer = res.string().unwrapOr("");
            // trim whitespace
            while (!remoteVer.empty() && (remoteVer.back() == '\n' || remoteVer.back() == '\r' || remoteVer.back() == ' '))
                remoteVer.pop_back();
            if (remoteVer.empty()) return;
            auto localVer = loadLocalVersion();
            if (remoteVer == localVer) return;
            // different version, ask user
            std::string msg = localVer.empty()
                ? "Want to download chairs now?\nYou need them for the mod to work!"
                : "Chair pack update available! Want to update chairs?";
            geode::createQuickPopup(
                "Chairscare",
                msg,
                "Nah", "Yeah!",
                [remoteVer](auto, bool yes) {
                    if (!yes) return;
                    downloadAndInstallChairs([remoteVer](bool ok) {
                        if (!ok) return;
                        saveLocalVersion(remoteVer);
                        FLAlertLayer::create("Done!", "Chairs downloaded successfully!", "OK")->show();
                    });
                }
            );
        }
    );
}
// custom "Submit Chair" button setting
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
    ButtonSprite* m_buttonSprite;
    CCMenuItemSpriteExtra*  m_button;
    async::TaskHolder<web::WebResponse> m_uploadTask;
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
        async::spawn(
            file::pick(file::PickMode::OpenFile, {
                "Pick a chair image",
                std::nullopt,
                { { "Images", { "png", "jpg", "jpeg", "webp", "bmp" } } }
            }),
            [this](Result<std::optional<std::filesystem::path>> result) {
                if (!result.isOk()) {
                    FLAlertLayer::create("Error", "Failed to open file picker.", "OK")->show();
                    return;
                }
                auto opt = result.unwrap();
                if (!opt) return; // user cancelled
                this->uploadFile(opt.value());
            }
        );
    }
    void uploadFile(std::filesystem::path const& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            FLAlertLayer::create("Error", "Could not read the selected file.", "OK")->show();
            return;
        }
        std::vector<uint8_t> bytes(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );
        // build multipart/form-data body
        std::string boundary = "----ChairscareUpload7f3a9b";
        std::string filename = path.filename().string();
        std::string ext = path.extension().string();
        std::string mimeType = "image/png";
        if (ext == ".jpg" || ext == ".jpeg") mimeType = "image/jpeg";
        else if (ext == ".webp") mimeType = "image/webp";
        else if (ext == ".bmp")  mimeType = "image/bmp";
        std::string header =
            "--" + boundary + "\r\n"
            "Content-Disposition: form-data; name=\"chair\"; filename=\"" + filename + "\"\r\n"
            "Content-Type: " + mimeType + "\r\n\r\n";
        std::string footer = "\r\n--" + boundary + "--\r\n";
        std::vector<uint8_t> body;
        body.insert(body.end(), header.begin(), header.end());
        body.insert(body.end(), bytes.begin(), bytes.end());
        body.insert(body.end(), footer.begin(), footer.end());
        auto req = web::WebRequest();
        req.header("Content-Type", "multipart/form-data; boundary=" + boundary);
        req.body(body);
        m_uploadTask.spawn(
            req.post("https://chairss.rf.gd/save.php"),
            [](web::WebResponse res) {
                if (!res.ok()) {
                    auto body = res.string().unwrapOr("");
                    if (body.find("max_chairs") != std::string::npos) {
                        FLAlertLayer::create("Slow down!", "Max 3 chairs a day! Come back tomorrow :)", "OK")->show();
                    } else {
                        FLAlertLayer::create("Upload Failed", "Something went wrong. Try again later!", "OK")->show();
                    }
                    return;
                }
                FLAlertLayer::create("Thank you!", "Your chair has been submitted!\nIt might appear in a future update :D", "OK")->show();
            }
        );
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
    checkForChairUpdate(); // check for chair pack updates on every boot
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
