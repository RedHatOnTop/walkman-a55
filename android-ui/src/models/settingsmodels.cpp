#include "settingsmodels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtCore/QStorageInfo>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtQml/QQmlEngine>

#include <memory>

#include "../../library/musicdb.h"
#include "../../playback/player.h"
#include "../appcontext.h"
#include "../dialogmanager.h"
#include "../rowmodel.h"
#include "../screencontroller.h"
#include "choicelist.h"
#include "soundmodels.h"
#include "soundstate.h"

namespace {

QString qmlText(const char *context, const char *id)
{
    return QCoreApplication::translate(context, id);
}

QString objectText(const char *id)
{
    return QCoreApplication::translate("QObject", id);
}

QStringList texts(const char *context, std::initializer_list<const char *> ids)
{
    QStringList list;
    for (const char *id : ids)
        list.append(QCoreApplication::translate(context, id));
    return list;
}

// The labels and defaults of the radio lists the settings keep in prefs.
QStringList screenOffLabels()
{
    return texts("dmpapp::ScreenOffTimerSettingViewModel", {"000051", "000052", "000053", "000054", "000055", "000056", "000043"});
}

QStringList autoShutdownLabels()
{
    return texts("dmpapp::AutoShutdownSettingViewModel", {"000145", "000146", "000147", "000148", "000149"});
}

QStringList imeLabels()
{
    return texts("dmpapp::DeviceSettingViewModel", {"990001", "990002", "990003", "990004"});
}

QStringList sleepTimerLabels()
{
    return texts("dmpapp::PlaySettingViewModel", {"000043", "000056", "000057", "000058", "000059"});
}

const int sleepTimerMinutes[] = {0, 30, 60, 90, 120};

// Every screen with settings of its own: prefs under "settings/", the
// refresh that reads them, and the scroll position the stock lists reset
// when they are shown again.
class Page : public ScreenModel
{
public:
    Page(const QString &id, AppContext *app) : ScreenModel(id, app, app) {}
    void activate() override { refresh(); }

protected:
    virtual void refresh() {}
    QVariant pref(const char *key, const QVariant &fallback = {}) const
    {
        return app()->prefs()->value(QString("settings/") + key, fallback);
    }
    void setPref(const char *key, const QVariant &value) { app()->prefs()->setValue(QString("settings/") + key, value); }
    void togglePref(const char *key, bool fallback = false) { setPref(key, !pref(key, fallback).toBool()); }
    void scrollToTop()
    {
        if (view())
            call("onInitializeVPosition");
    }
    void goBack()
    {
        QTimer::singleShot(0, this, [this] { app()->controller()->Back(); });
    }
};

// A page of radio rows whose choice is an index kept in a pref.
class ChoicePage : public Page
{
public:
    ChoicePage(const QString &id, AppContext *app, const char *listProperty, const char *signal, const char *key,
               int fallback)
        : Page(id, app), m_list(new ChoiceList(this)), m_property(listProperty), m_signal(signal), m_key(key),
          m_fallback(fallback)
    {
        set(m_property, QVariant::fromValue<QObject *>(m_list));
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == m_signal) {
            const int index = args.value(0).toInt();
            if (index >= 0 && index < m_list->size()) {
                setPref(m_key, index);
                m_list->setChecked(index);
                changed(index);
            }
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    virtual QStringList labels() const = 0;
    virtual void changed(int) {}
    int choice() const { return qBound(0, pref(m_key, m_fallback).toInt(), labels().size() - 1); }
    void refresh() override { m_list->setChoices(labels(), choice()); }

    ChoiceList *m_list;

private:
    QString m_property;
    QByteArray m_signal;
    const char *m_key;
    int m_fallback;
};

// The clock the library stamps its changes with. Android keeps the real
// one, so a setting is an offset from it.
QDateTime clockNow(AppContext *app)
{
    return QDateTime::currentDateTime().addSecs(app->prefs()->value("settings/clockOffset", 0).toLongLong());
}

void setClock(AppContext *app, const QDateTime &when)
{
    app->prefs()->setValue("settings/clockOffset", QDateTime::currentDateTime().secsTo(when));
}

const char *const dateFormats[] = {"yyyy-MM-dd", "MM-dd-yyyy", "dd-MM-yyyy"};

QString timeText(const QDateTime &when, bool hours24)
{
    if (hours24)
        return when.toString("HH:mm");
    const int hour = when.time().hour();
    return QString("%1:%2 %3").arg(hour % 12 == 0 ? 12 : hour % 12).arg(when.time().minute(), 2, 10, QChar('0'))
        .arg(hour < 12 ? "AM" : "PM");
}

// The languages the translations ship, in the stock list's order.
struct Language {
    const char *name;
    const char *locale;
};

const Language languages[] = {
    {"010036", "de_DE"}, {"010037", "en_US"}, {"010038", "es_ES"}, {"010039", "fr_FR"}, {"010040", "it_IT"},
    {"010041", "pl_PL"}, {"010042", "pr_PR"}, {"010043", "ru_RU"}, {"010044", "tr_TR"}, {"010045", "ja_JP"},
    {"010046", "ko_KR"}, {"010047", "zh_CN"}, {"010048", "zh_HK"},
};
const int languageCount = int(sizeof(languages) / sizeof(languages[0]));

QStringList languageLabels()
{
    QStringList labels;
    for (const Language &language : languages)
        labels.append(objectText(language.name));
    return labels;
}

// The system's language, by the rule main() picks it with when none is saved.
int systemLanguage()
{
    const QByteArray forced = qgetenv("WALKMANUI_LANG");
    QString name = forced.isEmpty() ? QLocale::system().name() : QString::fromLatin1(forced);
    if (name.startsWith("pt"))
        name = "pr_PR";
    for (int i = 0; i < languageCount; ++i) {
        if (name == languages[i].locale)
            return i;
    }
    for (int i = 0; i < languageCount; ++i) {
        if (QString(languages[i].locale).startsWith(name.left(2)))
            return i;
    }
    return 1;
}

int currentLanguage(AppContext *app)
{
    const QString stored = app->prefs()->value("settings/language").toString();
    for (int i = 0; i < languageCount; ++i) {
        if (stored == languages[i].locale)
            return i;
    }
    return systemLanguage();
}

QString translationFile(const QString &locale)
{
#ifdef Q_OS_ANDROID
    const QString res = "assets:/res";
#else
    const QByteArray configured = qgetenv("WALKMANUI_RES");
    const QString res = configured.isEmpty() ? QStringLiteral(WALKMANUI_RES_DIR) : QString::fromLocal8Bit(configured);
#endif
    return res + "/tr/HgrmMediaPlayerApp_" + locale + ".qm";
}

// Swaps the Sony translations in place and has the QML views re-evaluate
// their strings; the strings the ViewModels fetched stay as they were until
// the screen that holds them refreshes.
void applyLanguage(QQuickItem *view, int language)
{
    const QList<QTranslator *> translators = qApp->findChildren<QTranslator *>(QString(), Qt::FindDirectChildrenOnly);
    const QString file = translationFile(languages[language].locale);
    if (translators.isEmpty() || !QFile::exists(file) || !translators.first()->load(file))
        return;
    QEvent change(QEvent::LanguageChange);
    QCoreApplication::sendEvent(qApp, &change);
    if (view) {
        if (QQmlEngine *engine = qmlEngine(view))
            engine->retranslate();
    }
}

// Whatever a screen that exists only for hardware this build lacks shows.
class DefaultsModel : public ScreenModel
{
public:
    DefaultsModel(const QString &id, AppContext *app, const QVariantMap &values, const QStringList &lists = {})
        : ScreenModel(id, app, app)
    {
        QVariantMap all = values;
        for (const QString &name : lists)
            all.insert(name, QVariant::fromValue<QObject *>(new ChoiceList(this)));
        setValues(all);
    }
};

// ---------------------------------------------------------------- settings top

class SettingTopModel : public Page
{
public:
    SettingTopModel(const QString &id, AppContext *app)
        : Page(id, app), m_sound(SoundState::of(app)), m_headphones(new ChoiceList(this))
    {
        // ClearAudio+, Source Direct, saved sound settings and the sound guide;
        // no DSD remastering, no headphone selection, NC/ASM, Bluetooth, BT
        // receiver, remote, USB DAC, FM, recorder, language study. Android owns
        // the brightness the quick bar would set.
        setValues({{"hasFeatureClearAudio", true}, {"hasFeatureSourceDirect", true},
                   {"hasFeatureDsdRemastering", false}, {"isDsdRemasteringOn", false},
                   {"hasFeatureHighQualitySoundGuide", true}, {"hasFeatureUserPreset", true},
                   {"hasFeatureHeadphoneSelection", false}, {"hasFeatureNoiseCancel", false},
                   {"isNcAvailable", false}, {"isAsmAvailable", false}, {"isNcOn", false}, {"isAsmOn", false},
                   {"isNcCapableHeadphoneModel", false}, {"isAsmCapableHeadphoneModel", false},
                   {"isClearPhaseAvailable", false}, {"isClearPhaseOn", false}, {"isOtherHeadphone", false},
                   {"ncSettingInfo", QString()}, {"asmSettingInfo", QString()},
                   {"headphoneList", QVariant::fromValue<QObject *>(m_headphones)}, {"headphoneName", QString()},
                   {"hasFeatureBLERemote", false}, {"blueToothIconVisible", false},
                   {"remoteControlIconVisible", false}, {"btQuickButtonState", "BtOff"},
                   {"bleRemoteQuickButtonState", "BleRemoteOff"}, {"btStatusInfo", QString()},
                   {"bleRemoteStatusInfo", QString()}, {"has_feature_bt_receiver", false},
                   {"is_exist_bt_receiver_paired_device", false}, {"isNfcOn", false},
                   {"is_not_setting_for_bt_receiver", true}, {"isNotSettingForDac", true},
                   {"hasFeatureDacModel", false}, {"hasFeatureLanguageStudy", false},
                   {"hasFeatureFmRadio", false}, {"hasFeatureRecorder", false},
                   {"isAvailablePowerSupplySetting", false}, {"hasFeatureRegulationInfo", false},
                   {"is_usb_audio_connect", false}, {"brightnessIconVisible", false},
                   {"quickButtonStatusText", QString()}});
        connect(m_sound, &SoundState::modeChanged, this, [this] { refresh(); });
        refresh();
    }

    void activate() override
    {
        Page::activate();
        scrollToTop();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "clearAudioOnOffToggled")
            m_sound->setClearAudio(!m_sound->clearAudio());
        else if (signal == "sourceDirectOnOffToggled")
            m_sound->setSourceDirect(!m_sound->sourceDirect());
        else if (signal == "goToOutputSetting")
            navigate("SelectOutputSetting");
        else
            Page::handle(signal, args);
    }

protected:
    void attached() override
    {
        // The Bluetooth category shows whenever the screen is not a DAC page, and
        // its Bluetooth information row has no flag of its own.
        const QString title = qmlText("SettingTop", "230000");
        for (QQuickItem *item : view()->findChildren<QQuickItem *>()) {
            if (item->property("title").toString() == title)
                item->setVisible(false);
        }
    }

    void refresh() override
    {
        setValues({{"isClearAudioOn", m_sound->clearAudio()}, {"isSourceDirectOn", m_sound->sourceDirect()}});
    }

private:
    SoundState *m_sound;
    ChoiceList *m_headphones;
};

class SoundEnhancementModel : public Page
{
public:
    SoundEnhancementModel(const QString &id, AppContext *app) : Page(id, app), m_sound(SoundState::of(app))
    {
        // No VPT and no DSEE AI; the vinyl processor chooses a type.
        setValues({{"hasFeatureToneControl", true}, {"hasFeatureVpt", false}, {"has_feature_dc_phase", true},
                   {"has_feature_vinyl_processor", true}, {"has_feature_dsee_ai", false}, {"vpt_enabled", false},
                   {"is_dsee_ai_on", false}, {"vpt_type", QString()}, {"is_enable_vinyl_processor_type", true},
                   {"usb_connect", false}, {"usb_dialog_animation", true}});
        connect(m_sound, &SoundState::changed, this, [this] { refresh(); });
        refresh();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "goToEqAndToneControl")
            navigate("GoToEqAndToneControl", m_sound->toneActive() ? 2 : 3);
        else if (signal == "selectVinylProcessor")
            navigate("GoToVinylProcessor");
        else if (signal != "selectDseeAi")
            Page::handle(signal, args);
    }

protected:
    void refresh() override
    {
        setValues({{"dsee_hx_enabled", m_sound->dseeHx()}, {"dsee_hx_type", qmlText("SoundEnhancementSetting", "000042")},
                   {"dc_phase_enabled", m_sound->dcPhase()}, {"dc_phase_type", dcPhaseTypeLabel(m_sound->dcPhaseType())},
                   {"dynamic_normalizer_enabled", m_sound->dynamicNormalizer()},
                   {"is_vinyl_processor_on", m_sound->vinyl()},
                   {"vinyl_processor_select_type", vinylTypeLabel(m_sound->vinylType())}});
    }

private:
    SoundState *m_sound;
};

// ---------------------------------------------------------------- play and output

class PlaySettingModel : public Page
{
public:
    PlaySettingModel(const QString &id, AppContext *app)
        : Page(id, app), m_sleepList(new ChoiceList(this)), m_timer(new QTimer(this))
    {
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this] { sleep(); });
        connect(app->player(), &PlayerController::settingsChanged, this, [this] { refreshPlayer(); });
        set("sleepTimerList", QVariant::fromValue<QObject *>(m_sleepList));
        refresh();
    }

    void activate() override
    {
        Page::activate();
        scrollToTop();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        PlayerController *player = app()->player();
        if (signal == "shuffleOnOffToggled") {
            player->setShuffle(!player->shuffle());
        } else if (signal == "changeRepeatMode") {
            player->setRepeatMode(qBound(0, args.value(0).toInt(), 2));
        } else if (signal == "changePlaybackRange") {
            player->setAllRange(args.value(0).toInt() == 0);
        } else if (signal == "changeArtistDisplayMode") {
            setPref("artistDisplayMode", args.value(0).toInt());
            set("artistDisplayMode", args.value(0).toInt());
        } else if (signal == "displayOnShowingLyricsOnOffToggled") {
            togglePref("keepScreenOn");
            set("isDisplayOnShowingLyrics", pref("keepScreenOn").toBool());
        } else if (signal == "setSleepTimer") {
            setSleepTimer(args.value(0).toInt());
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        refreshPlayer();
        m_sleepList->setChoices(sleepTimerLabels(), m_sleep);
        setValues({{"artistDisplayMode", pref("artistDisplayMode", 0).toInt()},
                   {"isDisplayOnShowingLyrics", pref("keepScreenOn", false).toBool()},
                   {"sleepTimerName", m_sleepList->checkedLabel()}});
    }

private:
    void refreshPlayer()
    {
        const PlayerController *player = app()->player();
        setValues({{"isShuffle", player->shuffle()}, {"repeatMode", player->repeatMode()},
                   {"playbackRange", player->allRange() ? 0 : 1}});
    }

    // Playback stops when the chosen time has passed, as the stock timer did.
    void setSleepTimer(int index)
    {
        if (index < 0 || index >= int(sizeof(sleepTimerMinutes) / sizeof(sleepTimerMinutes[0])))
            return;
        m_sleep = index;
        m_sleepList->setChecked(index);
        set("sleepTimerName", m_sleepList->checkedLabel());
        if (index == 0)
            m_timer->stop();
        else
            m_timer->start(sleepTimerMinutes[index] * 60 * 1000);
    }

    void sleep()
    {
        app()->player()->pause();
        setSleepTimer(0);
        app()->message(objectText("010094"));
    }

    ChoiceList *m_sleepList;
    QTimer *m_timer;
    int m_sleep = 0;
};

class OutputSettingModel : public Page
{
public:
    OutputSettingModel(const QString &id, AppContext *app) : Page(id, app)
    {
        // No USB DAC: "USB Output for DSD" is hidden the way the stock list
        // hides it in DAC mode. No DAC filter, no auto muting, no high gain
        // amplifier; Android plays no DSD (patch 0006 hides its settings). No
        // L/R balance either (user's call: nobody uses it).
        setValues({{"is_available_avls", true}, {"hasFeatureLrBalanceSetting", false},
                   {"hasFeatureDsdPcmConvert", false}, {"isDsdNativePlaying", false},
                   {"hasFeatureHighGainSetting", false}, {"isFunctionDac", true}, {"has_feature_dac_filter", false},
                   {"is_function_bt_receiver", false}, {"has_feature_automuting_setting", false},
                   {"is_automuting_on", false}});
        connect(app->player(), &PlayerController::settingsChanged, this, [this] { refresh(); });
        refresh();
    }

    void activate() override
    {
        Page::activate();
        scrollToTop();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "avlsOnOffToggled") {
            app()->player()->setAvls(!app()->player()->avls());
        } else if (signal == "dacFilterSelected") {
            navigate("GoToDacFilterSelect");
        } else if (signal != "dsdNativePlayingOnOffToggled" && signal != "autoMutingOnOffToggled") {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        set("isAvlsOn", app()->player()->avls());
    }
};

class HighGainModel : public Page
{
public:
    HighGainModel(const QString &id, AppContext *app) : Page(id, app) { refresh(); }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "btlGainModeToggled") {
            togglePref("highGainBtl");
            refresh();
        } else if (signal == "seGainModeToggled") {
            togglePref("highGainSe");
            refresh();
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        setValues({{"isBtlGainModeHigh", pref("highGainBtl", false).toBool()},
                   {"isSeGainModeHigh", pref("highGainSe", false).toBool()}});
    }
};

class DsdPlayModel : public Page
{
public:
    DsdPlayModel(const QString &id, AppContext *app) : Page(id, app) { refresh(); }

    void activate() override
    {
        Page::activate();
        scrollToTop();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "changeDsdConvFilterType") {
            setPref("dsdFilter", args.value(0).toInt());
            refresh();
        } else if (signal == "changeDsdConvGainMode") {
            setPref("dsdGain", args.value(0).toInt());
            refresh();
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        setValues({{"dsdConvFilterType", pref("dsdFilter", 0).toInt()}, {"dsdConvGainMode", pref("dsdGain", 0).toInt()}});
    }
};

// ---------------------------------------------------------------- device

class DeviceSettingModel : public Page
{
public:
    DeviceSettingModel(const QString &id, AppContext *app) : Page(id, app)
    {
        // No beep, HOLD switch, battery care or battery preference; the SD card
        // is the storage the library reads, not something to mount. Android
        // owns power, the keyboard (patch 0006: the screen timeout, USB
        // connection and clock too).
        setValues({{"is_available_operation_sound", false}, {"isOperationSoundOn", false},
                   {"is_available_hw_hold_sw", false}, {"isDisableScreenTouchDuringHold", false},
                   {"is_available_battery_care_setting", false}, {"isBatteryCareOn", false},
                   {"isAvailableLanguageSelection", true}, {"isAvailableIME", false}, {"hasSDCard", false},
                   {"sdCardStateInfo", QString()}, {"sd_card_slot_num", 0}, {"isNotSettingForDac", true},
                   {"is_not_setting_for_bt_receiver", true}, {"is_available_auto_shutdown", false},
                   {"is_available_autosleep_directly_shutdown", false}, {"is_autosleep_directly_shutdown", false},
                   {"is_available_priority_battery_mode", false}, {"is_priority_battery_mode", false}});
        refresh();
    }

    void activate() override
    {
        Page::activate();
        scrollToTop();
    }

protected:
    void refresh() override
    {
        set("selectedLanguageName", languageLabels().value(currentLanguage(app())));
    }
};

class ScreenOffTimerModel : public ChoicePage
{
public:
    ScreenOffTimerModel(const QString &id, AppContext *app)
        : ChoicePage(id, app, "screenOffTimerList", "selectScreenOffTimer", "screenOff", 1)
    {
        refresh();
    }

protected:
    QStringList labels() const override { return screenOffLabels(); }
};

class AutoShutdownModel : public ChoicePage
{
public:
    AutoShutdownModel(const QString &id, AppContext *app)
        : ChoicePage(id, app, "autoShutdownList", "selectAutoShutdown", "autoShutdown", 0)
    {
        refresh();
    }

protected:
    QStringList labels() const override { return autoShutdownLabels(); }
};

class ImeModel : public ChoicePage
{
public:
    ImeModel(const QString &id, AppContext *app) : ChoicePage(id, app, "keyboardListModel", "keyboardChanged", "ime", 0)
    {
        refresh();
    }

protected:
    QStringList labels() const override { return imeLabels(); }
};

class LanguageModel : public Page
{
public:
    LanguageModel(const QString &id, AppContext *app) : Page(id, app), m_list(new ChoiceList(this))
    {
        setValues({{"languageInfoList", QVariant::fromValue<QObject *>(m_list)},
                   {"isDuringInitialSettingWizard", false}});
        refresh();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "selectLanguageType") {
            m_list->setChecked(args.value(0).toInt());
        } else if (signal == "setLanguageType") {
            const int language = qBound(0, m_list->checkedItemId(), languageCount - 1);
            setPref("language", languages[language].locale);
            applyLanguage(view(), language);
            refresh();
            goBack();
        } else if (signal == "cancel") {
            refresh();
            goBack();
        } else if (signal != "prevScreen" && signal != "nextScreen") {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        const int language = currentLanguage(app());
        m_list->setChoices(languageLabels(), language);
        set("languageType", language);
    }

private:
    ChoiceList *m_list;
};

class DeviceInfoModel : public Page
{
public:
    DeviceInfoModel(const QString &id, AppContext *app) : Page(id, app), m_sd(new ListRows({"title", "info"}, this))
    {
        setValues({{"modelName", "NW-A55"}, {"softwareVersion", "WalkmanUI 0.3"},
                   {"copyright", objectText("010011").section('\n', 0, 0)},
                   {"sd_card_info_list", QVariant::fromValue<QObject *>(m_sd)},
                   {"isAvailablePlaybackTimeCounter", false}, {"workTime", QString()},
                   {"isAvailableWifi", false}, {"btAddress", objectText("230021")}});
        connect(app->library(), &MusicDb::changed, this, [this] { refresh(); });
        refresh();
    }

protected:
    void refresh() override
    {
        const QVector<Storage> storages = app()->library()->storages();
        QVector<QVariantMap> rows;
        for (int i = 1; i < storages.size(); ++i) {
            const char *title = storages.size() > 2 ? (i == 1 ? "010145" : "010146") : "010014";
            rows.append({{"title", objectText(title)}, {"info", freeTotal(storages.at(i).path)}});
        }
        m_sd->replace(rows);
        setValues({{"memorySize", freeTotal(storages.value(0).path)},
                   {"contentsCount", QString::number(app()->library()->count())}});
    }

private:
    static QString size(qint64 bytes)
    {
        const double gigabytes = bytes / 1e9;
        return gigabytes >= 1.0 ? QString::number(gigabytes, 'f', 1) + " GB"
                                : QString::number(bytes / 1e6, 'f', 0) + " MB";
    }

    static QString freeTotal(const QString &path)
    {
        const QStorageInfo info(path);
        if (path.isEmpty() || !info.isValid() || !info.isReady())
            return objectText("000137") + " / " + objectText("000137");
        return size(info.bytesAvailable()) + " / " + size(info.bytesTotal());
    }

    ListRows *m_sd;
};

class InitializeModel : public Page
{
public:
    InitializeModel(const QString &id, AppContext *app) : Page(id, app)
    {
        setValues({{"sd_card_slot_num", 0}, {"sd_card_state_info", QString()}, {"has_sd_card", false}});
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "startSettingInit") {
            confirm(objectText("010078"), [this] {
                resetSettings();
                app()->message(objectText("010079"));
            });
        } else if (signal == "startReUpdateDB") {
            confirm(objectText("010088"), [this] { rebuildDatabase(); });
        } else if (signal != "selectSdInitializeSetting") {
            Page::handle(signal, args);
        }
    }

private:
    // Asks the stock Yes/No question; the answer runs once the dialog has closed.
    void confirm(const QString &text, std::function<void()> yes)
    {
        DialogManager *dialogs = app()->dialogs();
        if (dialogs->open("kMid9200", QString(), text, QVariant()) == "NoneDialog")
            return;
        auto *guard = new QObject(this);
        connect(dialogs, &DialogManager::accepted, guard, [this, guard, yes] {
            guard->deleteLater();
            QTimer::singleShot(0, this, yes);
        });
        connect(dialogs, &DialogManager::dialogInitialize, guard, [guard] { guard->deleteLater(); });
    }

    void rebuildDatabase()
    {
        MusicDb *library = app()->library();
        auto done = std::make_shared<QMetaObject::Connection>();
        *done = connect(library, &MusicDb::busyChanged, this, [this, library, done] {
            if (library->busy())
                return;
            disconnect(*done);
            app()->message(objectText("010097"));
        });
        library->rescan();
    }

    // Every setting this player owns goes back to its default; songs, the
    // queue, bookmarks and playlists stay.
    void resetSettings()
    {
        QSettings *prefs = app()->prefs();
        SoundState::of(app())->reset();
        prefs->remove("settings");
        prefs->remove("library/iconOrder");
        prefs->remove("library/hiddenIcons");
        app()->player()->setShuffle(false);
        app()->player()->setRepeatMode(0);
        app()->player()->setAvls(false);
        app()->player()->setAllRange(true);
        applyLanguage(view(), systemLanguage());
    }
};

// ---------------------------------------------------------------- date and time

class DateSettingTopModel : public Page
{
public:
    DateSettingTopModel(const QString &id, AppContext *app) : Page(id, app) { refresh(); }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "timeDisplayFormatToggled") {
            togglePref("time24");
            refresh();
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override
    {
        const QDateTime now = clockNow(app());
        const bool hours24 = pref("time24", true).toBool();
        const int format = qBound(0, pref("dateFormat", 0).toInt(), 2);
        setValues({{"dateTime", now.toString(dateFormats[format]) + " " + timeText(now, hours24)},
                   {"timeDisplayFormatText", objectText(hours24 ? "010064" : "010063")},
                   {"dateDisplayFormatText", objectText(format == 0 ? "010065" : format == 1 ? "010066" : "010067")},
                   {"is24HourDisplay", hours24}});
    }
};

class DateSettingModel : public Page
{
public:
    DateSettingModel(const QString &id, AppContext *app) : Page(id, app)
    {
        setValues({{"launch_year", 2018}, {"isDuringInitialSettingWizard", false}});
        refresh();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        QDateTime when = clockNow(app());
        QDate date = when.date();
        QTime time = when.time();
        const int value = args.value(0).toInt();
        if (signal == "setYear" || signal == "setMonth" || signal == "setDay") {
            int year = date.year(), month = date.month(), day = date.day();
            (signal == "setYear" ? year : signal == "setMonth" ? month : day) = value;
            date = QDate(year, month, 1);
            date = QDate(year, month, qMin(day, date.daysInMonth()));
        } else if (signal == "setHour") {
            // The picker counts 12, 1 ... 11 in twelve-hour mode.
            const bool pm = time.hour() >= 12;
            time.setHMS(pref("time24", true).toBool() ? value : value % 12 + (pm ? 12 : 0), time.minute(), 0);
        } else if (signal == "setMinute") {
            time.setHMS(time.hour(), value, 0);
        } else if (signal == "setAmPm") {
            time.setHMS(time.hour() % 12 + (args.value(0).toBool() ? 12 : 0), time.minute(), 0);
        } else {
            Page::handle(signal, args);
            return;
        }
        if (date.isValid() && time.isValid()) {
            setClock(app(), QDateTime(date, time));
            refresh();
        }
    }

protected:
    void refresh() override
    {
        const QDateTime now = clockNow(app());
        const bool hours24 = pref("time24", true).toBool();
        setValues({{"value_year", now.date().year()}, {"value_month", now.date().month()},
                   {"value_day", now.date().day()}, {"value_hour", hours24 ? now.time().hour() : now.time().hour() % 12},
                   {"value_minute", now.time().minute()}, {"isPm", now.time().hour() >= 12},
                   {"is24HourDisplay", hours24}, {"dateDisplayFormat", qBound(0, pref("dateFormat", 0).toInt(), 2)}});
    }
};

class DateFormatModel : public Page
{
public:
    DateFormatModel(const QString &id, AppContext *app) : Page(id, app) { refresh(); }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "setDateDisplayFormat") {
            setPref("dateFormat", qBound(0, args.value(0).toInt(), 2));
            refresh();
        } else {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override { set("dateDisplayFormat", qBound(0, pref("dateFormat", 0).toInt(), 2)); }
};

// ---------------------------------------------------------------- the rest

class UsbSettingModel : public Page
{
public:
    UsbSettingModel(const QString &id, AppContext *app) : Page(id, app)
    {
        // There is no mass storage mode on Android.
        setValues({{"usbConnectStatus", false}, {"unmountExported", false}});
        refresh();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "usbAutoStatusCliked") {
            togglePref("usbAuto");
            refresh();
        } else if (signal != "enableExportAsMsc" && signal != "dialogClose") {
            Page::handle(signal, args);
        }
    }

protected:
    void refresh() override { set("usbAutoStatus", pref("usbAuto", false).toBool()); }
};

class HighQualitySoundGuideModel : public Page
{
public:
    HighQualitySoundGuideModel(const QString &id, AppContext *app) : Page(id, app)
    {
        // This player's guide has the one page on the equalizer and tone control.
        setValues({{"isInitialSettingWizard", false}, {"totalPage", 1}});
        refresh();
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "goToPrevPage" || signal == "goToNextPage")
            refresh();
        else if (signal == "goToFinishPage")
            goBack();
        else
            Page::handle(signal, args);
    }

protected:
    void refresh() override
    {
        setValues({{"currentPage", 1}, {"contentsText", objectText("010109")},
                   {"contentsImage", "ic_hq_sound_guide_equalizer_and_tone_control.png"}});
    }
};

class RegulationModel : public ScreenModel
{
public:
    RegulationModel(const QString &id, AppContext *app) : ScreenModel(id, app, app)
    {
        // The certification marks of the hardware are not part of this app.
        setValues({{"isValidImagePath", false}, {"isEmptyImage", false}, {"regulationInfoImagePath", QString()}});
    }
};

// Screens of hardware an Android phone does not give this player.
ScreenModel *hardwareModel(const QString &id, AppContext *app)
{
    const QString address = objectText("230021");
    if (id == "NcSetting")
        return new DefaultsModel(id, app, {{"isNcOn", false}, {"ncLevelValue", 0}, {"currentNcEnvironmentName", QString()}},
                                 {"ncEnvironmentList"});
    if (id == "AsmSetting")
        return new DefaultsModel(id, app, {{"isAsmOn", false}, {"asmLevelValue", 0}, {"currentAsmModeName", QString()},
                                           {"isAvailableAsmModeSetting", false}},
                                 {"asmModeList"});
    if (id == "BluetoothConnect")
        return new DefaultsModel(id, app, {{"bluetoothStatus", false}, {"validPairedList", false},
                                           {"validSearchedList", false}, {"screenStateId", 0},
                                           {"myDeviceName", QString()}, {"myDeviceBDAddress", address},
                                           {"screen_title", sonyText("dmpapp::BtConnectViewModel", "230001")}},
                                 {"pairedList", "modelList", "contextList"});
    if (id == "BluetoothDeviceInfo" || id == "BtInfomation")
        return new DefaultsModel(id, app, {}, {"infomation"});
    if (id == "BtAutoConnectSetting")
        return new DefaultsModel(id, app, {{"autoConnectSet", 0}}, {"pairedDeviceList"});
    if (id == "BtWirelessQualitySetting")
        return new DefaultsModel(id, app, {}, {"setting_list"});
    if (id == "AudioConnectSetting")
        return new DefaultsModel(id, app, {{"wirelessQualitySet", 0}, {"wirelessQualityName", QString()},
                                           {"is_absolute_volume_on", false}});
    if (id == "RemoconConnect")
        return new DefaultsModel(id, app, {{"deviceTitle", QString()}, {"deleteStatus", false}, {"remoconSetting", 1},
                                           {"duringOnOff", false}});
    if (id == "DACSetting")
        return new DefaultsModel(id, app, {{"is_charge_mode", false}});
    if (id == "MSC")
        return new DefaultsModel(id, app, {{"enableMscOperation", false}, {"unmountExported", false},
                                           {"has_charge_from_usb", false}});
    if (id == "DAC")
        return new DefaultsModel(id, app, {{"isDacConnect", false}, {"streamingInfo", QString()}, {"isAvls", false},
                                           {"is_high_gain", false}, {"minVolume", 0}, {"maxVolume", 120},
                                           {"avlsMaxVolume", 120}, {"volume", app->player()->volume()}});
    if (id == "BtReceiver")
        return new DefaultsModel(id, app, {{"state_type", 0}, {"main_button_title", QString()},
                                           {"main_message", QString()}, {"connected_device_name", QString()},
                                           {"connection_codec_name", QString()}, {"is_show_progress", false}});
    if (id == "BtReceiverRegisteredManagement")
        return new DefaultsModel(id, app, {}, {"registered_devices"});
    if (id == "BtReceiverPlayingQualitySetting")
        return new DefaultsModel(id, app, {{"player_quaity_setting", 0}});
    return nullptr;
}

}

ScreenModel *createSettingsModel(const QString &id, AppContext *app)
{
    if (ScreenModel *sound = createSoundModel(id, app))
        return sound;
    if (id == "SettingTop")
        return new SettingTopModel(id, app);
    if (id == "SoundEnhancementSetting")
        return new SoundEnhancementModel(id, app);
    if (id == "PlaySetting")
        return new PlaySettingModel(id, app);
    if (id == "OutputSetting")
        return new OutputSettingModel(id, app);
    if (id == "HighGainOutput")
        return new HighGainModel(id, app);
    if (id == "DsdPlaySetting")
        return new DsdPlayModel(id, app);
    if (id == "DeviceSetting")
        return new DeviceSettingModel(id, app);
    if (id == "ScreenOffTimerSetting")
        return new ScreenOffTimerModel(id, app);
    if (id == "AutoShutdownSetting")
        return new AutoShutdownModel(id, app);
    if (id == "IMESetting")
        return new ImeModel(id, app);
    if (id == "LanguageSetting")
        return new LanguageModel(id, app);
    if (id == "DeviceInfo")
        return new DeviceInfoModel(id, app);
    if (id == "InitializeSettingTop")
        return new InitializeModel(id, app);
    if (id == "DateSettingTop")
        return new DateSettingTopModel(id, app);
    if (id == "DateSetting")
        return new DateSettingModel(id, app);
    if (id == "DateDisplayFormatSetting")
        return new DateFormatModel(id, app);
    if (id == "USBSetting")
        return new UsbSettingModel(id, app);
    if (id == "SettingsHighQualitySoundGuide")
        return new HighQualitySoundGuideModel(id, app);
    if (id == "RegulationInfo")
        return new RegulationModel(id, app);
    return hardwareModel(id, app);
}
