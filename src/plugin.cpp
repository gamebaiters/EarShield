/*
 * EarShield - TeamSpeak 3 plugin
 *
 * Audio normalization + earrape limiter for TeamSpeak 3.
 * Forked from exp111/VolumeLeveler (originally Volume Leveler by exp111).
 *
 * License: GPLv3
 */

#ifdef _WIN32
#pragma warning (disable : 4100)
#include <Windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <set>
#include <fstream>
#include <string>
#include <sstream>

#include <QtWidgets/QDialog>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QMessageBox>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtCore/QPointer>

#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"
#include "plugin.h"
#include "version.h"
#include "UpdateChecker.h"
#include "debug_log.h"

static struct TS3Functions ts3Functions;

#ifdef _WIN32
#define _strcpy(dest, destSize, src) strcpy_s(dest, destSize, src)
#define snprintf sprintf_s
#else
#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); (dest)[destSize-1] = '\0'; }
#endif

#define PLUGIN_API_VERSION 26
#define PATH_BUFSIZE       512
#define COMMAND_BUFSIZE    128
#define INFODATA_BUFSIZE   128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128

static char* pluginID = nullptr;
static bool pluginEnabled = true;
static bool globalNormalize = true;
static std::string langCode = "en";

// limiterDB:   ear-protection ceiling in dBFS (-30..0). Audio NEVER exceeds this.
// normLevelDB: normalization boost in dB (0..30). How much quiet voices are amplified.
static float limiterDB = -10.0f;
static float normLevelDB = 10.0f;

struct AGCState {
    double agcGain = 1.0;
    double prevAgcGain = 1.0;
    double smoothedRMS = 0.0;
    int agcHoldCounter = 0;

    double envelope = 0.0;
    double limiterGain = 1.0;

    bool hadSpeechOnset = false;
    int silentBuffers = 0;
};
static std::map<anyID, AGCState> agcStates;
static std::set<anyID> ignoredClients;

static std::map<anyID, float> clientLimiterDBs;
static std::map<anyID, float> clientNormLevelDBs;
static std::map<anyID, bool>  clientNormalizeOverrides;

static std::set<std::string>          persistentIgnored;
static std::map<std::string, float>   persistentLimiterDBs;
static std::map<std::string, float>   persistentNormLevelDBs;
static std::map<std::string, bool>    persistentNormEnableOverrides;

static QPointer<UpdateChecker> g_updater;

static bool isItalian() { return langCode == "it"; }

static std::string getConfigFilePath() {
    char path[PATH_BUFSIZE];
    ts3Functions.getConfigPath(path, PATH_BUFSIZE);
    return std::string(path) + "EarShield.ini";
}

static void loadConfig() {
    std::ifstream file(getConfigFilePath());
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eqPos = line.find_last_of('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        if (key == "pluginEnabled")       pluginEnabled = (val == "1" || val == "true");
        else if (key == "globalNormalize") globalNormalize = (val == "1" || val == "true");
        else if (key == "lang") {
            if (val == "it" || val == "en") langCode = val;
        }
        // targetLevelDB = backward compat with v3.1 configs
        else if (key == "limiterDB" || key == "targetLevelDB") {
            try { limiterDB = std::stof(val); } catch (...) {}
        }
        else if (key == "normLevelDB") {
            try { normLevelDB = std::stof(val); } catch (...) {}
        }
        else if (key.find("ignore_") == 0) {
            if (val == "1" || val == "true") persistentIgnored.insert(key.substr(7));
        }
        else if (key.find("limiter_") == 0) {
            try { persistentLimiterDBs[key.substr(8)] = std::stof(val); } catch (...) {}
        }
        // backward compat: old target_ keys become limiter_
        else if (key.find("target_") == 0) {
            try { persistentLimiterDBs[key.substr(7)] = std::stof(val); } catch (...) {}
        }
        else if (key.find("normlvl_") == 0) {
            try { persistentNormLevelDBs[key.substr(8)] = std::stof(val); } catch (...) {}
        }
        else if (key.find("norm_") == 0) {
            persistentNormEnableOverrides[key.substr(5)] = (val == "1" || val == "true");
        }
    }

    if (limiterDB < -30.0f) limiterDB = -30.0f;
    if (limiterDB > 0.0f)   limiterDB = 0.0f;
    if (normLevelDB < 0.0f) normLevelDB = 0.0f;
    if (normLevelDB > 30.0f) normLevelDB = 30.0f;
}

static void saveConfig() {
    std::ofstream file(getConfigFilePath());
    if (!file.is_open()) return;

    file << "pluginEnabled=" << (pluginEnabled ? "1" : "0") << "\n";
    file << "globalNormalize=" << (globalNormalize ? "1" : "0") << "\n";
    file << "lang=" << langCode << "\n";
    file << "limiterDB=" << limiterDB << "\n";
    file << "normLevelDB=" << normLevelDB << "\n";
    for (const auto& uuid : persistentIgnored)
        file << "ignore_" << uuid << "=1\n";
    for (const auto& p : persistentLimiterDBs)
        file << "limiter_" << p.first << "=" << p.second << "\n";
    for (const auto& p : persistentNormLevelDBs)
        file << "normlvl_" << p.first << "=" << p.second << "\n";
    for (const auto& p : persistentNormEnableOverrides)
        file << "norm_" << p.first << "=" << (p.second ? "1" : "0") << "\n";
}

static std::string getClientUUID(uint64 schid, anyID clientID) {
    char* uuid;
    if (ts3Functions.getClientVariableAsString(schid, clientID, CLIENT_UNIQUE_IDENTIFIER, &uuid) == ERROR_ok) {
        std::string res(uuid);
        ts3Functions.freeMemory(uuid);
        return res;
    }
    return "";
}

#ifdef _WIN32
static int wcharToUtf8(const wchar_t* str, char** result) {
    int outlen = WideCharToMultiByte(CP_UTF8, 0, str, -1, 0, 0, 0, 0);
    *result = (char*)malloc(outlen);
    if (WideCharToMultiByte(CP_UTF8, 0, str, -1, *result, outlen, 0, 0) == 0) {
        *result = nullptr;
        return -1;
    }
    return 0;
}
#endif

const char* ts3plugin_name() {
#ifdef _WIN32
    static char* result = nullptr;
    if (!result) {
        const wchar_t* name = L"EarShield";
        if (wcharToUtf8(name, &result) == -1) result = (char*)"EarShield";
    }
    return result;
#else
    return "EarShield";
#endif
}

const char* ts3plugin_version()  { return EARSHIELD_VERSION_STRING; }
int         ts3plugin_apiVersion() { return PLUGIN_API_VERSION; }
const char* ts3plugin_author()   { return "Marco (gamebaiters)"; }

const char* ts3plugin_description() {
    static std::string descEn = "EarShield protects your hearing from earrape and normalizes everyone's volume to a uniform, safe level.";
    static std::string descIt = "EarShield protegge l'udito da earrape e normalizza il volume di tutti i partecipanti verso un livello uniforme e sicuro.";
    return isItalian() ? descIt.c_str() : descEn.c_str();
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) { ts3Functions = funcs; }

#ifdef _WIN32
static void sweepLegacyVariants() {
    char base[PATH_BUFSIZE];
    if (!ts3Functions.getConfigPath) return;
    ts3Functions.getConfigPath(base, PATH_BUFSIZE);
    std::string p(base);
    if (!p.empty() && p.back() != '\\' && p.back() != '/') p.push_back('\\');
    p += "plugins\\";
    const char* legacy[] = {
        "volumeleveler_win64.dll",
        "volumeleveler_win32.dll",
        "VolumeLeveler_win64.dll",
        nullptr
    };
    for (int i = 0; legacy[i]; ++i) DeleteFileA((p + legacy[i]).c_str());
}
#endif

int ts3plugin_init() {
    char path[PATH_BUFSIZE] = {0};
    if (ts3Functions.getConfigPath) ts3Functions.getConfigPath(path, PATH_BUFSIZE);
    earshield_log::init(path);
    earshield_log::install_crash_handlers();
    ESLOG("ts3plugin_init begin (version %s build %d, api %d)",
        EARSHIELD_VERSION_STRING, EARSHIELD_VERSION_BUILD, PLUGIN_API_VERSION);
    ESLOG("QCoreApplication::instance() = %p", (void*)QCoreApplication::instance());

    loadConfig();
    ESLOG("config loaded: enabled=%d normalize=%d limiterDB=%.1f normLevelDB=%.1f lang=%s",
        (int)pluginEnabled, (int)globalNormalize, limiterDB, normLevelDB, langCode.c_str());

#ifdef _WIN32
    sweepLegacyVariants();
    ESLOG("legacy DLL sweep done");
#endif

    if (QCoreApplication::instance()) {
        ESLOG("scheduling UpdateChecker creation in 3500ms");
        QTimer::singleShot(3500, []() {
            ESLOG("UpdateChecker timer fired");
            try {
                if (!g_updater) {
                    ESLOG("constructing UpdateChecker");
                    g_updater = new UpdateChecker();
                    ESLOG("UpdateChecker constructed: %p", (void*)g_updater.data());
                }
                if (g_updater) {
                    ESLOG("calling checkForUpdates(silent=true)");
                    g_updater->checkForUpdates(true);
                }
            } catch (const std::exception& e) {
                ESLOG("[EXCEPTION] in init updater: %s", e.what());
            } catch (...) {
                ESLOG("[EXCEPTION] in init updater: unknown");
            }
        });
    } else {
        ESLOG("[WARN] QCoreApplication::instance() is null at init - skipping update check");
    }
    ESLOG("ts3plugin_init end");
    return 0;
}

static void plugin_kill() {
    ESLOG("plugin_kill begin");
    if (g_updater) {
        ESLOG("deleting UpdateChecker %p", (void*)g_updater.data());
        delete g_updater.data();
        g_updater = nullptr;
    }

    if (QCoreApplication::instance()) {
        ESLOG("draining Qt event queue");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    agcStates.clear();
    ignoredClients.clear();
    clientLimiterDBs.clear();
    clientNormLevelDBs.clear();
    clientNormalizeOverrides.clear();
    ESLOG("plugin_kill end");
}

void ts3plugin_shutdown() {
    ESLOG("ts3plugin_shutdown begin");
    saveConfig();
    plugin_kill();
    if (pluginID) { free(pluginID); pluginID = nullptr; }
    ESLOG("ts3plugin_shutdown end");
    earshield_log::close();
}

int ts3plugin_offersConfigure() { return PLUGIN_OFFERS_NO_CONFIGURE; }
void ts3plugin_configure(void* /*handle*/, void* /*qParentWidget*/) {}

void ts3plugin_registerPluginID(const char* id) {
    const size_t sz = strlen(id) + 1;
    pluginID = (char*)malloc(sz * sizeof(char));
    _strcpy(pluginID, sz, id);
}

const char* ts3plugin_commandKeyword() { return ""; }
const char* ts3plugin_infoTitle()      { return "EarShield"; }
void ts3plugin_freeMemory(void* data)  { free(data); }
int  ts3plugin_requestAutoload()       { return 1; }

static struct PluginMenuItem* createMenuItem(enum PluginMenuType type, int id, const char* text, const char* icon) {
    auto* mi = (struct PluginMenuItem*)malloc(sizeof(struct PluginMenuItem));
    mi->type = type;
    mi->id = id;
    _strcpy(mi->text, PLUGIN_MENU_BUFSZ, text);
    _strcpy(mi->icon, PLUGIN_MENU_BUFSZ, icon);
    return mi;
}

#define BEGIN_CREATE_MENUS(x) const size_t sz = x + 1; size_t n = 0; *menuItems = (struct PluginMenuItem**)malloc(sizeof(struct PluginMenuItem*) * sz);
#define CREATE_MENU_ITEM(a, b, c, d) (*menuItems)[n++] = createMenuItem(a, b, c, d);
#define END_CREATE_MENUS (*menuItems)[n++] = nullptr; assert(n == sz);

enum {
    MENU_ID_GLOBAL_TOGGLE = 1,
    MENU_ID_GLOBAL_CONFIGURE,
    MENU_ID_GLOBAL_RESET_TS3_VOLUMES,
    MENU_ID_GLOBAL_CHECK_UPDATE,
    MENU_ID_CLIENT_TOGGLE_IGNORE,
    MENU_ID_CLIENT_CONFIGURE,
    MENU_ID_MAX
};

void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon) {
    BEGIN_CREATE_MENUS(MENU_ID_MAX - 1);
    if (isItalian()) {
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_TOGGLE,             "Abilita/Disabilita EarShield", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_CONFIGURE,          "Impostazioni Globali EarShield", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_RESET_TS3_VOLUMES,  "Resetta Volumi TS3 Utenti", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_CHECK_UPDATE,       "Cerca Aggiornamenti EarShield", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_CLIENT_TOGGLE_IGNORE,      "Ignora / Non Ignorare (EarShield)", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_CLIENT_CONFIGURE,          "Impostazioni EarShield (Utente)", "");
    } else {
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_TOGGLE,             "Enable/Disable EarShield", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_CONFIGURE,          "EarShield Global Settings", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_RESET_TS3_VOLUMES,  "Reset TS3 Client Volumes", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_GLOBAL, MENU_ID_GLOBAL_CHECK_UPDATE,       "Check for EarShield Updates", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_CLIENT_TOGGLE_IGNORE,      "Toggle EarShield Ignore", "");
        CREATE_MENU_ITEM(PLUGIN_MENU_TYPE_CLIENT, MENU_ID_CLIENT_CONFIGURE,          "EarShield Client Settings", "");
    }
    END_CREATE_MENUS;
    *menuIcon = (char*)malloc(PLUGIN_MENU_BUFSZ * sizeof(char));
    _strcpy(*menuIcon, PLUGIN_MENU_BUFSZ, "");
}

static void resetAllTS3ClientVolumes(uint64 schid) {
    anyID* clientList = nullptr;
    if (ts3Functions.getClientList(schid, &clientList) != ERROR_ok || !clientList) return;

    int count = 0;
    for (int i = 0; clientList[i] != 0; ++i) {
        ts3Functions.setClientVolumeModifier(schid, clientList[i], 0.0f);
        count++;
    }
    ts3Functions.freeMemory(clientList);

    char msg[256];
    if (isItalian())
        snprintf(msg, sizeof(msg), "EarShield: Resettato il volume nativo di %d utenti TeamSpeak.", count);
    else
        snprintf(msg, sizeof(msg), "EarShield: Reset native volume for %d TeamSpeak clients.", count);
    ts3Functions.printMessageToCurrentTab(msg);
}

static void resetAllSettings() {
    globalNormalize = true;
    limiterDB = -10.0f;
    normLevelDB = 10.0f;
    pluginEnabled = true;
    persistentIgnored.clear();
    persistentLimiterDBs.clear();
    persistentNormLevelDBs.clear();
    persistentNormEnableOverrides.clear();
    ignoredClients.clear();
    clientLimiterDBs.clear();
    clientNormLevelDBs.clear();
    clientNormalizeOverrides.clear();
    agcStates.clear();
    saveConfig();
}

void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID) {
    char msg[256];
    ESLOG("onMenuItemEvent type=%d id=%d schid=%llu selected=%llu",
        (int)type, menuItemID, (unsigned long long)schid, (unsigned long long)selectedItemID);
    ESLOG("  thread=main? QApplication=%p QCoreApp=%p",
        (void*)QCoreApplication::instance(), (void*)QCoreApplication::instance());

    try {

    if (type == PLUGIN_MENU_TYPE_GLOBAL) {
        if (menuItemID == MENU_ID_GLOBAL_TOGGLE) {
            pluginEnabled = !pluginEnabled;
            saveConfig();
            if (isItalian())
                snprintf(msg, sizeof(msg), "EarShield e' ora %s.", pluginEnabled ? "ATTIVO" : "DISABILITATO");
            else
                snprintf(msg, sizeof(msg), "EarShield is now %s.", pluginEnabled ? "ENABLED" : "DISABLED");
            ts3Functions.printMessageToCurrentTab(msg);
            return;
        }

        if (menuItemID == MENU_ID_GLOBAL_RESET_TS3_VOLUMES) {
            resetAllTS3ClientVolumes(schid);
            return;
        }

        if (menuItemID == MENU_ID_GLOBAL_CHECK_UPDATE) {
            ESLOG("MENU_ID_GLOBAL_CHECK_UPDATE handler");
            if (!g_updater) {
                ESLOG("creating UpdateChecker on demand");
                g_updater = new UpdateChecker();
                ESLOG("UpdateChecker = %p", (void*)g_updater.data());
            }
            ESLOG("calling checkForUpdates(silent=false)");
            g_updater->checkForUpdates(false);
            ESLOG("checkForUpdates returned");
            return;
        }

        if (menuItemID == MENU_ID_GLOBAL_CONFIGURE) {
            ESLOG("MENU_ID_GLOBAL_CONFIGURE handler - constructing global dialog");
            QDialog* dlg = new QDialog();
            ESLOG("global QDialog ctor ok = %p", (void*)dlg);
            dlg->setWindowTitle(isItalian() ? "Impostazioni Globali EarShield" : "EarShield Global Settings");
            dlg->resize(440, 480);
            QVBoxLayout* mainLayout = new QVBoxLayout(dlg);

            QCheckBox* normCheck = new QCheckBox(isItalian()
                ? "Abilita Normalizzazione (alza automaticamente le voci basse)"
                : "Enable Normalization (auto-boost quiet voices)", dlg);
            normCheck->setChecked(globalNormalize);
            mainLayout->addWidget(normCheck);

            QGroupBox* limiterGroup = new QGroupBox(isItalian()
                ? "Protezione Orecchie - Limite Massimo"
                : "Ear Protection - Maximum Limit", dlg);
            QVBoxLayout* limLayout = new QVBoxLayout(limiterGroup);
            limLayout->addWidget(new QLabel(isItalian()
                ? "L'audio non superera' MAI questo livello (dBFS):"
                : "Audio will NEVER exceed this level (dBFS):"));
            QSlider* limSlider = new QSlider(Qt::Horizontal, limiterGroup);
            limSlider->setRange(-30, 0);
            limSlider->setValue((int)limiterDB);
            limLayout->addWidget(limSlider);
            QLabel* limVal = new QLabel(limiterGroup);
            limVal->setAlignment(Qt::AlignCenter);
            auto updateLimLabel = [limVal](int v) { limVal->setText(QString::number(v) + " dBFS"); };
            updateLimLabel(limSlider->value());
            QObject::connect(limSlider, &QSlider::valueChanged, updateLimLabel);
            limLayout->addWidget(limVal);
            mainLayout->addWidget(limiterGroup);

            QGroupBox* normGroup = new QGroupBox(isItalian()
                ? "Normalizzazione - Livello di Boost"
                : "Normalization - Boost Level", dlg);
            QVBoxLayout* normLayout = new QVBoxLayout(normGroup);
            normLayout->addWidget(new QLabel(isItalian()
                ? "Quanto amplificare le voci basse (dB di boost):"
                : "How much to amplify quiet voices (dB boost):"));
            QSlider* normSlider = new QSlider(Qt::Horizontal, normGroup);
            normSlider->setRange(0, 30);
            normSlider->setValue((int)normLevelDB);
            normLayout->addWidget(normSlider);
            QLabel* normVal = new QLabel(normGroup);
            normVal->setAlignment(Qt::AlignCenter);
            auto updateNormLabel = [normVal](int v) { normVal->setText("+" + QString::number(v) + " dB"); };
            updateNormLabel(normSlider->value());
            QObject::connect(normSlider, &QSlider::valueChanged, updateNormLabel);
            normLayout->addWidget(normVal);
            mainLayout->addWidget(normGroup);

            QGroupBox* langGroup = new QGroupBox(isItalian() ? "Lingua" : "Language", dlg);
            QHBoxLayout* langLayout = new QHBoxLayout(langGroup);
            QPushButton* enBtn = new QPushButton("English", langGroup);
            QPushButton* itBtn = new QPushButton("Italiano", langGroup);
            enBtn->setCheckable(true); itBtn->setCheckable(true);
            enBtn->setChecked(!isItalian());
            itBtn->setChecked(isItalian());
            QObject::connect(enBtn, &QPushButton::clicked, [enBtn, itBtn]() {
                langCode = "en"; saveConfig(); enBtn->setChecked(true); itBtn->setChecked(false);
            });
            QObject::connect(itBtn, &QPushButton::clicked, [enBtn, itBtn]() {
                langCode = "it"; saveConfig(); itBtn->setChecked(true); enBtn->setChecked(false);
            });
            langLayout->addWidget(enBtn);
            langLayout->addWidget(itBtn);
            mainLayout->addWidget(langGroup);

            QGroupBox* ts3Group = new QGroupBox(isItalian()
                ? "Volumi Nativi TeamSpeak" : "Native TeamSpeak Volumes", dlg);
            QVBoxLayout* ts3Layout = new QVBoxLayout(ts3Group);
            ts3Layout->addWidget(new QLabel(isItalian()
                ? "Resetta tutte le regolazioni di volume per-utente salvate da TeamSpeak:"
                : "Reset all per-client volume adjustments saved by TeamSpeak:"));
            QPushButton* ts3ResetBtn = new QPushButton(isItalian()
                ? "Resetta Volumi TS3 Utenti" : "Reset TS3 Client Volumes", ts3Group);
            QObject::connect(ts3ResetBtn, &QPushButton::clicked, [schid]() { resetAllTS3ClientVolumes(schid); });
            ts3Layout->addWidget(ts3ResetBtn);
            mainLayout->addWidget(ts3Group);

            QHBoxLayout* btnLayout = new QHBoxLayout();
            QPushButton* resetBtn = new QPushButton(isItalian() ? "Ripristina Tutto" : "Reset All", dlg);
            QPushButton* okBtn    = new QPushButton(isItalian() ? "Applica e Chiudi" : "Apply & Close", dlg);
            QObject::connect(resetBtn, &QPushButton::clicked, [dlg]() { resetAllSettings(); dlg->accept(); });
            QObject::connect(okBtn, &QPushButton::clicked, [dlg, limSlider, normSlider, normCheck]() {
                limiterDB = (float)limSlider->value();
                normLevelDB = (float)normSlider->value();
                globalNormalize = normCheck->isChecked();
                saveConfig();
                dlg->accept();
            });
            btnLayout->addWidget(resetBtn);
            btnLayout->addWidget(okBtn);
            mainLayout->addLayout(btnLayout);

            dlg->setAttribute(Qt::WA_DeleteOnClose);
            ESLOG("global dialog about to show()");
            dlg->show();
            ESLOG("global dialog show() returned");
            return;
        }
        return;
    }

    if (type == PLUGIN_MENU_TYPE_CLIENT) {
        anyID clientID = (anyID)selectedItemID;
        char* clientName = nullptr;
        ts3Functions.getClientVariableAsString(schid, clientID, CLIENT_NICKNAME, &clientName);

        if (menuItemID == MENU_ID_CLIENT_TOGGLE_IGNORE) {
            if (clientName) {
                std::string uuid = getClientUUID(schid, clientID);
                if (ignoredClients.count(clientID)) {
                    ignoredClients.erase(clientID);
                    if (!uuid.empty()) persistentIgnored.erase(uuid);
                    if (isItalian())
                        snprintf(msg, sizeof(msg), "'%s' non e' piu' ignorato da EarShield.", clientName);
                    else
                        snprintf(msg, sizeof(msg), "'%s' is no longer ignored by EarShield.", clientName);
                } else {
                    ignoredClients.insert(clientID);
                    if (!uuid.empty()) persistentIgnored.insert(uuid);
                    if (isItalian())
                        snprintf(msg, sizeof(msg), "'%s' e' ora IGNORATO da EarShield.", clientName);
                    else
                        snprintf(msg, sizeof(msg), "'%s' is now IGNORED by EarShield.", clientName);
                }
                saveConfig();
                ts3Functions.printMessageToCurrentTab(msg);
                ts3Functions.freeMemory(clientName);
            }
            return;
        }

        if (menuItemID == MENU_ID_CLIENT_CONFIGURE) {
            float initLim       = clientLimiterDBs.count(clientID)         ? clientLimiterDBs[clientID]         : limiterDB;
            float initNormLvl   = clientNormLevelDBs.count(clientID)       ? clientNormLevelDBs[clientID]       : normLevelDB;
            bool  initNormEnable= clientNormalizeOverrides.count(clientID) ? clientNormalizeOverrides[clientID] : globalNormalize;

            QDialog* dlg = new QDialog();
            QString title = isItalian()
                ? QString("EarShield - Impostazioni per: ") + (clientName ? clientName : "Sconosciuto")
                : QString("EarShield - Settings for: ") + (clientName ? clientName : "Unknown");
            dlg->setWindowTitle(title);
            dlg->resize(400, 340);
            QVBoxLayout* mainLayout = new QVBoxLayout(dlg);

            QCheckBox* normCheck = new QCheckBox(isItalian()
                ? "Abilita Normalizzazione per questo utente"
                : "Enable Normalization for this client", dlg);
            normCheck->setChecked(initNormEnable);
            mainLayout->addWidget(normCheck);

            QGroupBox* limiterGroup = new QGroupBox(isItalian()
                ? "Limite Massimo per questo Utente"
                : "Maximum Limit for this Client", dlg);
            QVBoxLayout* limLayout = new QVBoxLayout(limiterGroup);
            limLayout->addWidget(new QLabel(isItalian()
                ? "Limite protezione orecchie (dBFS):"
                : "Ear protection ceiling (dBFS):"));
            QSlider* limSlider = new QSlider(Qt::Horizontal, limiterGroup);
            limSlider->setRange(-30, 0);
            limSlider->setValue((int)initLim);
            limLayout->addWidget(limSlider);
            QLabel* limVal = new QLabel(limiterGroup);
            limVal->setAlignment(Qt::AlignCenter);
            auto updateLimLabel = [limVal](int v) { limVal->setText(QString::number(v) + " dBFS"); };
            updateLimLabel(limSlider->value());
            QObject::connect(limSlider, &QSlider::valueChanged, updateLimLabel);
            limLayout->addWidget(limVal);
            mainLayout->addWidget(limiterGroup);

            QGroupBox* normGroup = new QGroupBox(isItalian()
                ? "Normalizzazione per questo Utente"
                : "Normalization for this Client", dlg);
            QVBoxLayout* normLayout = new QVBoxLayout(normGroup);
            normLayout->addWidget(new QLabel(isItalian()
                ? "Boost normalizzazione (dB):"
                : "Normalization boost (dB):"));
            QSlider* normSlider = new QSlider(Qt::Horizontal, normGroup);
            normSlider->setRange(0, 30);
            normSlider->setValue((int)initNormLvl);
            normLayout->addWidget(normSlider);
            QLabel* normVal = new QLabel(normGroup);
            normVal->setAlignment(Qt::AlignCenter);
            auto updateNormLabel = [normVal](int v) { normVal->setText("+" + QString::number(v) + " dB"); };
            updateNormLabel(normSlider->value());
            QObject::connect(normSlider, &QSlider::valueChanged, updateNormLabel);
            normLayout->addWidget(normVal);
            mainLayout->addWidget(normGroup);

            QHBoxLayout* btnLayout = new QHBoxLayout();
            QPushButton* resetBtn = new QPushButton(isItalian() ? "Ripristina a Globale" : "Reset to Global", dlg);
            QPushButton* okBtn    = new QPushButton(isItalian() ? "Applica e Chiudi" : "Apply & Close", dlg);
            QObject::connect(resetBtn, &QPushButton::clicked, [dlg, clientID, schid]() {
                clientLimiterDBs.erase(clientID);
                clientNormLevelDBs.erase(clientID);
                clientNormalizeOverrides.erase(clientID);
                agcStates.erase(clientID);
                std::string uuid = getClientUUID(schid, clientID);
                if (!uuid.empty()) {
                    persistentLimiterDBs.erase(uuid);
                    persistentNormLevelDBs.erase(uuid);
                    persistentNormEnableOverrides.erase(uuid);
                }
                saveConfig();
                dlg->accept();
            });
            QObject::connect(okBtn, &QPushButton::clicked, [dlg, limSlider, normSlider, normCheck, clientID, schid]() {
                clientLimiterDBs[clientID]         = (float)limSlider->value();
                clientNormLevelDBs[clientID]       = (float)normSlider->value();
                clientNormalizeOverrides[clientID] = normCheck->isChecked();
                agcStates.erase(clientID);
                std::string uuid = getClientUUID(schid, clientID);
                if (!uuid.empty()) {
                    persistentLimiterDBs[uuid]          = (float)limSlider->value();
                    persistentNormLevelDBs[uuid]        = (float)normSlider->value();
                    persistentNormEnableOverrides[uuid] = normCheck->isChecked();
                }
                saveConfig();
                dlg->accept();
            });
            btnLayout->addWidget(resetBtn);
            btnLayout->addWidget(okBtn);
            mainLayout->addLayout(btnLayout);

            if (clientName) ts3Functions.freeMemory(clientName);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            ESLOG("client dialog about to show()");
            dlg->show();
            ESLOG("client dialog show() returned");
            return;
        }
    }

    } catch (const std::exception& e) {
        ESLOG("[EXCEPTION] onMenuItemEvent: %s", e.what());
    } catch (...) {
        ESLOG("[EXCEPTION] onMenuItemEvent: unknown");
    }
}

// Soft-knee per-sample limiter + slow RMS AGC, per broadcast AGC/limiter design.
// AGC measures the ORIGINAL input; limiter operates on POST-AGC signal so the two
// stages don't fight. tanh-based soft saturation replaces a hard ceiling clamp:
// hard clipping flat-tops the waveform on loud peaks and produces audible harmonic
// distortion ("earrape at low volume"). Soft saturation preserves waveform shape.

static const double AGC_MAX_INT16        = 32767.0;
static const double AGC_NOISE_FLOOR_RMS  = 80.0;
static const double AGC_CREST_HEADROOM   = 0.40;

static const double AGC_ATTACK_COEFF      = 0.04;   // ~500ms
static const double AGC_FAST_ATTACK_COEFF = 0.25;   // ~80ms — used on extreme overshoot
static const double AGC_RELEASE_COEFF     = 0.015;  // ~1.3s
static const int    AGC_HOLD_BUFFERS      = 15;     // ~300ms

static const double AGC_RMS_TRACK_UP     = 0.20;
static const double AGC_RMS_TRACK_DOWN   = 0.06;

static const double ENV_RELEASE_COEFF    = 0.9996;  // ~50ms
static const double LIM_ATTACK_COEFF     = 0.18;    // ~0.15ms
static const double LIM_RELEASE_COEFF    = 0.0005;  // ~40ms

static const double SOFT_SAT_THRESHOLD   = 1.5;     // soft compress starts at 1.5x target

void ts3plugin_onEditPlaybackVoiceDataEvent(uint64 schid, anyID clientID, short* samples, int sampleCount, int channels) {
    if (!pluginEnabled) return;
    if (ignoredClients.count(clientID)) return;

    if (!agcStates.count(clientID)) {
        agcStates[clientID] = AGCState();
        std::string uuid = getClientUUID(schid, clientID);
        if (!uuid.empty()) {
            if (persistentIgnored.count(uuid))           { ignoredClients.insert(clientID); return; }
            if (persistentLimiterDBs.count(uuid))         clientLimiterDBs[clientID]         = persistentLimiterDBs[uuid];
            if (persistentNormLevelDBs.count(uuid))       clientNormLevelDBs[clientID]       = persistentNormLevelDBs[uuid];
            if (persistentNormEnableOverrides.count(uuid)) clientNormalizeOverrides[clientID] = persistentNormEnableOverrides[uuid];
        }
    }
    AGCState& state = agcStates[clientID];

    float effLimiterDB = clientLimiterDBs.count(clientID)      ? clientLimiterDBs[clientID]      : limiterDB;
    float effNormLvlDB = clientNormLevelDBs.count(clientID)    ? clientNormLevelDBs[clientID]    : normLevelDB;
    bool  effNormalize = clientNormalizeOverrides.count(clientID) ? clientNormalizeOverrides[clientID] : globalNormalize;

    // Channel commanders bypass normalization unless the user opted in explicitly
    if (!clientNormalizeOverrides.count(clientID)) {
        int isCmd = 0;
        if (ts3Functions.getClientVariableAsInt(schid, clientID, CLIENT_IS_CHANNEL_COMMANDER, &isCmd) == ERROR_ok && isCmd == 1) {
            effNormalize = false;
        }
    }

    double targetAmplitude = AGC_MAX_INT16 * pow(10.0, effLimiterDB / 20.0);
    if (targetAmplitude > AGC_MAX_INT16) targetAmplitude = AGC_MAX_INT16;
    if (targetAmplitude < 1.0)            targetAmplitude = 1.0;

    double targetRMS = targetAmplitude * AGC_CREST_HEADROOM;
    double maxBoost = 1.0;
    if (effNormalize) {
        maxBoost = pow(10.0, effNormLvlDB / 20.0);
        if (maxBoost < 1.0) maxBoost = 1.0;
    }

    int totalSamples = sampleCount * channels;
    double softSatThresh = targetAmplitude * SOFT_SAT_THRESHOLD;

    double sum = 0.0;
    double peak = 0.0;
    for (int i = 0; i < totalSamples; ++i) {
        double s = (double)samples[i];
        sum += s * s;
        double a = fabs(s);
        if (a > peak) peak = a;
    }
    double rms = sqrt(sum / totalSamples);

    bool isSilent = (rms < AGC_NOISE_FLOOR_RMS);

    if (isSilent) {
        state.agcGain += (1.0 - state.agcGain) * 0.02;
        state.prevAgcGain = state.agcGain;
        state.hadSpeechOnset = false;
        state.silentBuffers++;
        state.agcHoldCounter = 0;
    } else {
        if (state.smoothedRMS < AGC_NOISE_FLOOR_RMS) {
            state.smoothedRMS = rms;
        } else {
            double tc = (rms > state.smoothedRMS) ? AGC_RMS_TRACK_UP : AGC_RMS_TRACK_DOWN;
            state.smoothedRMS = tc * rms + (1.0 - tc) * state.smoothedRMS;
        }

        double desiredGain = targetRMS / state.smoothedRMS;
        if (desiredGain > maxBoost) desiredGain = maxBoost;
        if (desiredGain < 0.01)     desiredGain = 0.01;

        state.prevAgcGain = state.agcGain;

        if (desiredGain < state.agcGain) {
            // Adaptive attack: faster on extreme overshoot to spare the limiter
            double attackCoeff = AGC_ATTACK_COEFF;
            if (desiredGain < state.agcGain * 0.5) attackCoeff = AGC_FAST_ATTACK_COEFF;
            state.agcGain += (desiredGain - state.agcGain) * attackCoeff;
            state.agcHoldCounter = AGC_HOLD_BUFFERS;
        } else {
            if (state.agcHoldCounter > 0) state.agcHoldCounter--;
            else state.agcGain += (desiredGain - state.agcGain) * AGC_RELEASE_COEFF;
        }

        // Hold AGC while limiter is still pulling so they don't both attenuate at once
        if (state.limiterGain < 0.9) state.agcHoldCounter = AGC_HOLD_BUFFERS;

        if (!state.hadSpeechOnset) {
            state.hadSpeechOnset = true;
            state.silentBuffers = 0;
            // Cap AGC so first voiced buffer doesn't boom
            if (peak > 0.0 && peak * state.agcGain > targetAmplitude) {
                state.agcGain = targetAmplitude / peak;
                state.prevAgcGain = state.agcGain;
            }
        }
    }

    for (int i = 0; i < totalSamples; ++i) {
        double t = (double)i / (double)totalSamples;
        double agcG = state.prevAgcGain + (state.agcGain - state.prevAgcGain) * t;

        double processed = (double)samples[i] * agcG;

        double absProcessed = fabs(processed);
        if (absProcessed > softSatThresh) {
            double x = (absProcessed - softSatThresh) / softSatThresh;
            double saturated = softSatThresh + softSatThresh * tanh(x);
            processed = (processed > 0.0) ? saturated : -saturated;
            absProcessed = saturated;
        }

        if (absProcessed > state.envelope) state.envelope = absProcessed;
        else                                state.envelope *= ENV_RELEASE_COEFF;

        double instLimGain = 1.0;
        if (state.envelope > targetAmplitude) instLimGain = targetAmplitude / state.envelope;

        if (instLimGain < state.limiterGain)
            state.limiterGain += (instLimGain - state.limiterGain) * LIM_ATTACK_COEFF;
        else
            state.limiterGain += (instLimGain - state.limiterGain) * LIM_RELEASE_COEFF;

        double output = processed * state.limiterGain;

        if (output >  32767.0) output =  32767.0;
        if (output < -32768.0) output = -32768.0;

        samples[i] = (short)output;
    }
}
