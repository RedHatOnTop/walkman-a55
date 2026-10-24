#include "soundmodels.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

#include <initializer_list>

#include "../appcontext.h"
#include "../screencontroller.h"
#include "choicelist.h"
#include "soundstate.h"

namespace {

// A string of one of the Sony view models, or of the shared QObject context
// the translations fall back to.
QString sony(const char *id, std::initializer_list<const char *> contexts)
{
    for (const char *context : contexts) {
        const QString text = QCoreApplication::translate(context, id);
        if (text != QLatin1String(id))
            return text;
    }
    return QString::fromLatin1(id);
}

const char *const eqContext = "dmpapp::EqualizerViewModel";
const char *const bandNames[SoundState::bandCount] = {"31hz", "62hz", "125hz", "250hz", "500hz",
                                                      "1Khz", "2Khz", "4Khz", "8Khz", "16Khz"};
const char *const dcTypeIds[SoundState::dcPhaseTypes] = {"020093", "020094", "020095", "020096", "020097", "020098"};
const char *const vinylTypeIds[SoundState::vinylTypes] = {"020340", "020343", "020344", "020345"};

QStringList dcTypeLabels()
{
    QStringList labels;
    for (const char *id : dcTypeIds)
        labels.append(sony(id, {"dmpapp::DcPhaseLinearizerViewModel", "QObject"}));
    return labels;
}

QStringList vinylTypeLabels()
{
    QStringList labels;
    for (const char *id : vinylTypeIds)
        labels.append(sony(id, {"dmpapp::VinylProcessorViewModel", "QObject"}));
    return labels;
}

// Every sound page but the balance: the header's title, the Source Direct
// and ClearAudio+ state that blanks the page, and the option menu.
class SoundModel : public ScreenModel
{
public:
    SoundModel(const QString &id, AppContext *app, const char *titleContext, const char *titleId)
        : ScreenModel(id, app, app), m_sound(SoundState::of(app)), m_titleContext(titleContext), m_titleId(titleId)
    {
        setValues({{"usb_connect", false}, {"usb_dialog_animation", true}});
        connect(m_sound, &SoundState::modeChanged, this, [this] { refreshMode(); });
        refreshMode();
    }

    // The language can change while the app runs; what a ViewModel words
    // itself is set again each time the page is about to show.
    void entering() override { retranslate(); }

    QVector<MenuItem> menu() override
    {
        QVector<MenuItem> items = extraMenu();
        if (app()->screens()->hasAction(id(), "GoToUserPresetSetting"))
            items.append({sonyText(eqContext, "020050"), [this] { navigate("GoToUserPresetSetting"); }});
        return items;
    }

protected:
    SoundState *sound() const { return m_sound; }
    virtual void retranslate() { set("title", sonyText(m_titleContext, m_titleId)); }
    bool fromSettings() const { return !id().endsWith("FromPlayer") && !id().endsWith("FromDAC"); }
    virtual QVector<MenuItem> extraMenu() { return {}; }

private:
    // The page shows what is on: SrcDirectOrClearAudioDialog names Source
    // Direct whenever it is available, so "available" follows "on".
    void refreshMode()
    {
        setValues({{"is_source_direct", m_sound->sourceDirect()}, {"is_clear_audio_plus", m_sound->clearAudio()},
                   {"is_available_source_direct", m_sound->sourceDirect()},
                   {"is_available_clear_audio_plus", m_sound->clearAudio()}});
    }

    SoundState *m_sound;
    const char *const m_titleContext;
    const char *const m_titleId;
};

class EqualizerModel : public SoundModel
{
public:
    EqualizerModel(const QString &id, AppContext *app)
        : SoundModel(id, app, eqContext, "020068")
    {
        retranslate();
        // The ten-band equalizer has one adjustable setting, so the adjust
        // button is always there; the page opens locked, with its title.
        setValues({{"lock", true}, {"select_usersetting", true}, {"active_slider_10band", -1}});
        connect(sound(), &SoundState::equalizerChanged, this, [this] {
            if (!m_editing)
                pushBands();
        });
    }

    void activate() override
    {
        sound()->setToneActive(false);
        set("lock", true);
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "equalizerLockChanged") {
            set("lock", args.value(0).toBool());
        } else if (signal.startsWith("equalizer10bandValue") && signal.endsWith("Changed")) {
            const QByteArray name = signal.mid(20, signal.size() - 27);
            for (int i = 0; i < SoundState::bandCount; ++i) {
                if (name == bandNames[i]) {
                    m_editing = true;
                    sound()->setBand(i, args.value(0).toDouble());
                    m_editing = false;
                }
            }
        } else if (signal == "activeSlider10bandChanged") {
            // The view owns the selection; the next visit starts with none.
        } else {
            SoundModel::handle(signal, args);
        }
    }

protected:
    void attached() override { pushBands(); }

    QVector<MenuItem> extraMenu() override
    {
        if (!app()->screens()->hasAction(id(), "SwitchToneEqualizer"))
            return {};
        return {{sonyText(eqContext, "020067"), [this] { navigate("SwitchToneEqualizer"); }}};
    }

private:
    // The view takes bands as its own modifyEqualizer10bandValue... signals.
    void pushBands()
    {
        if (!view())
            return;
        for (int i = 0; i < SoundState::bandCount; ++i) {
            const QByteArray signal = QByteArray("modifyEqualizer10bandValue") + bandNames[i];
            QMetaObject::invokeMethod(view(), signal.constData(), Q_ARG(double, sound()->band(i)));
        }
    }

    bool m_editing = false;
};

class ToneControlModel : public SoundModel
{
public:
    ToneControlModel(const QString &id, AppContext *app)
        : SoundModel(id, app, eqContext, "020067")
    {
        retranslate();
        refresh();
        connect(sound(), &SoundState::toneChanged, this, [this] { refresh(); });
    }

    void activate() override { sound()->setToneActive(true); }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "toneValueLowChanged")
            sound()->setTone(0, args.value(0).toDouble());
        else if (signal == "toneValueMiddleChanged")
            sound()->setTone(1, args.value(0).toDouble());
        else if (signal == "toneValueHighChanged")
            sound()->setTone(2, args.value(0).toDouble());
        else
            SoundModel::handle(signal, args);
    }

protected:
    QVector<MenuItem> extraMenu() override
    {
        if (!app()->screens()->hasAction(id(), "SwitchToneEqualizer"))
            return {};
        return {{sonyText(eqContext, "020068"), [this] { navigate("SwitchToneEqualizer"); }}};
    }

private:
    void refresh()
    {
        setValues({{"tone_value_low", sound()->tone(0)}, {"tone_value_middle", sound()->tone(1)},
                   {"tone_value_high", sound()->tone(2)}});
    }
};

class DseeHxModel : public SoundModel
{
public:
    DseeHxModel(const QString &id, AppContext *app)
        : SoundModel(id, app, "dmpapp::DseeHxViewModel", "020102"),
          m_types(new ChoiceList(this))
    {
        retranslate();
        // DSEE HX of this player only switches on and off.
        setValues({{"dsee_hx_type_name", QString()}, {"dsee_hx_type_list", QVariant::fromValue<QObject *>(m_types)},
                   {"is_available_dseehx_custom", false}, {"dsee_hx", sound()->dseeHx()}});
        connect(sound(), &SoundState::dseeHxChanged, this, [this] { set("dsee_hx", sound()->dseeHx()); });
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "dseeHxChange")
            sound()->setDseeHx(args.value(0).toBool());
        else if (signal != "dseeHxTypeNameChange")
            SoundModel::handle(signal, args);
    }

protected:
    void retranslate() override
    {
        SoundModel::retranslate();
        set("dsee_hx_explanation", sonyText("dmpapp::DseeHxViewModel", "020103"));
    }

private:
    ChoiceList *m_types;
};

class DcPhaseModel : public SoundModel
{
public:
    DcPhaseModel(const QString &id, AppContext *app)
        : SoundModel(id, app, "dmpapp::DcPhaseLinearizerViewModel", "020090"),
          m_types(new ChoiceList(dcTypeLabels(), -1, this))
    {
        setValues({{"dc_phase_linearizer_type_list", QVariant::fromValue<QObject *>(m_types)},
                   {"is_available_dcphaselinearlizer_custom", true}});
        retranslate();
        refresh();
        connect(sound(), &SoundState::dcPhaseChanged, this, [this] { refresh(); });
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "dcPhaseLinearizerChange")
            sound()->setDcPhase(args.value(0).toBool());
        else if (signal == "dcPhaseLinearizerTypeNameChange")
            sound()->setDcPhaseType(args.value(0).toInt());
        else
            SoundModel::handle(signal, args);
    }

protected:
    void retranslate() override
    {
        SoundModel::retranslate();
        set("dc_phase_linearizer_explanation", sonyText("dmpapp::DcPhaseLinearizerViewModel", "020091"));
        m_types->setChoices(dcTypeLabels(), sound()->dcPhaseType());
        if (view())
            refresh();
    }

private:
    void refresh()
    {
        m_types->setChecked(sound()->dcPhaseType());
        setValues({{"dc_phase_linearizer", sound()->dcPhase()},
                   {"dc_phase_linearizer_type_name", m_types->checkedLabel()}});
    }

    ChoiceList *m_types;
};

class DynamicNormalizerModel : public SoundModel
{
public:
    DynamicNormalizerModel(const QString &id, AppContext *app)
        : SoundModel(id, app, "dmpapp::DynamicNormalizerViewModel", "020110")
    {
        retranslate();
        refresh();
        connect(sound(), &SoundState::dynamicNormalizerChanged, this, [this] { refresh(); });
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "dynamicNormalizerChange")
            sound()->setDynamicNormalizer(args.value(0).toBool());
        else
            SoundModel::handle(signal, args);
    }

protected:
    void retranslate() override
    {
        SoundModel::retranslate();
        set("dynamic_normalizer_explanation", sonyText("dmpapp::DynamicNormalizerViewModel", "020111"));
    }

private:
    void refresh() { set("dynamic_normalizer", sound()->dynamicNormalizer()); }
};

class VinylModel : public SoundModel
{
public:
    VinylModel(const QString &id, AppContext *app)
        : SoundModel(id, app, "dmpapp::VinylProcessorViewModel", "020338"),
          m_types(new ChoiceList(vinylTypeLabels(), -1, this))
    {
        setValues({{"vinyl_processor_type_list", QVariant::fromValue<QObject *>(m_types)},
                   {"is_enable_vinyl_processor_type", true}});
        retranslate();
        refresh();
        connect(sound(), &SoundState::vinylChanged, this, [this] { refresh(); });
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "isVinylProcessorChange")
            sound()->setVinyl(args.value(0).toBool());
        else if (signal == "vinylProcessorTypeChanged")
            sound()->setVinylType(args.value(0).toInt());
        else
            SoundModel::handle(signal, args);
    }

protected:
    void retranslate() override
    {
        SoundModel::retranslate();
        set("vinyl_processor_explanation", sonyText("dmpapp::VinylProcessorViewModel", "020339"));
        m_types->setChoices(vinylTypeLabels(), sound()->vinylType());
        if (view())
            refresh();
    }

private:
    void refresh()
    {
        m_types->setChecked(sound()->vinylType());
        setValues({{"is_vinyl_processor_on", sound()->vinyl()},
                   {"vinyl_processor_select_type", m_types->checkedLabel()}});
    }

    ChoiceList *m_types;
};

// Saved Sound Settings: three slots the current effects are written to and
// read back from. The view asks its own questions (read this slot? replace
// it?) and reports the answers.
class PresetModel : public SoundModel
{
public:
    PresetModel(const QString &id, AppContext *app)
        : SoundModel(id, app, "dmpapp::UserPresetViewModel", "020050"),
          m_slots(new ChoiceList(this))
    {
        retranslate();
        setValues({{"user_preset_list", QVariant::fromValue<QObject *>(m_slots)},
                   {"from_sound_setting", !fromSettings()}});
    }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "registPresetIndex") {
            // The list dialog is still closing; the overwrite question follows it.
            m_slot = qBound(0, args.value(0).toInt(), SoundState::presetCount - 1);
            QTimer::singleShot(0, this, [this] { call("openConfirmDialog", {m_slot}); });
        } else if (signal == "selectPresetConfirmed") {
            sound()->savePreset(m_slot);
            QTimer::singleShot(0, this, [this] { call("openRegisteredDialog", {m_slot}); });
        } else if (signal == "selectPresetIndex") {
            read(args.value(0).toInt());
        } else {
            SoundModel::handle(signal, args);
        }
    }

protected:
    void retranslate() override
    {
        SoundModel::retranslate();
        const char *const ids[SoundState::presetCount] = {"020051", "020052", "020053"};
        QStringList labels;
        for (const char *label : ids)
            labels.append(sonyText("dmpapp::UserPresetViewModel", label));
        m_slots->setChoices(labels, -1);
    }

private:
    void read(int slot)
    {
        sound()->loadPreset(slot);
        // From the player the equalizer or tone control the setting came
        // from shows what was loaded.
        if (!fromSettings())
            QTimer::singleShot(0, this, [this] { navigate("BackEqTone", sound()->toneActive() ? 2 : 3); });
    }

    ChoiceList *m_slots;
    int m_slot = 0;
};

class BalanceModel : public ScreenModel
{
public:
    BalanceModel(const QString &id, AppContext *app)
        : ScreenModel(id, app, app), m_sound(SoundState::of(app))
    {
        set("title", sonyText("dmpapp::LrBalanceViewModel", "020112"));
        set("balance_value", m_sound->balance());
        connect(m_sound, &SoundState::balanceChanged, this, [this] { set("balance_value", m_sound->balance()); });
    }

    void entering() override { set("title", sonyText("dmpapp::LrBalanceViewModel", "020112")); }

    void handle(const QByteArray &signal, const QVariantList &args) override
    {
        if (signal == "balanceValueChange")
            m_sound->setBalance(args.value(0).toDouble());
        else
            ScreenModel::handle(signal, args);
    }

private:
    SoundState *m_sound;
};

}

QString dcPhaseTypeLabel(int type)
{
    return type >= 0 && type < SoundState::dcPhaseTypes ? sony(dcTypeIds[type], {"dmpapp::DcPhaseLinearizerViewModel", "QObject"})
                                                         : QString();
}

QString vinylTypeLabel(int type)
{
    return type >= 0 && type < SoundState::vinylTypes ? sony(vinylTypeIds[type], {"dmpapp::VinylProcessorViewModel", "QObject"})
                                                       : QString();
}

ScreenModel *createSoundModel(const QString &id, AppContext *app)
{
    QString page = id;
    if (page.endsWith("FromPlayer"))
        page.chop(10);
    else if (page.endsWith("FromDAC"))
        page.chop(7);
    if (page == "TenBandEqualizer")
        return new EqualizerModel(id, app);
    if (page == "ToneControl")
        return new ToneControlModel(id, app);
    if (page == "DseeHx" || page == "DseeHX")
        return new DseeHxModel(id, app);
    if (page == "DcPhaseLinearizer" || page == "Lenearizer")
        return new DcPhaseModel(id, app);
    if (page == "DynamicNormalizer")
        return new DynamicNormalizerModel(id, app);
    if (page == "VinylProcessor")
        return new VinylModel(id, app);
    if (page == "UserPreset")
        return new PresetModel(id, app);
    if (page == "LRBalance")
        return new BalanceModel(id, app);
    return nullptr;
}
