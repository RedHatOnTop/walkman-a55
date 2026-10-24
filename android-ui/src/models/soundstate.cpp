#include "soundstate.h"

#include <QtCore/QSettings>
#include <QtCore/QStringList>

#include <cmath>

#include "../appcontext.h"

constexpr double SoundState::bandLimit;
constexpr double SoundState::toneLimit;
constexpr double SoundState::balanceLimit;

namespace {

const QString current = "sound/";

QString presetPrefix(int slot)
{
    return QString("sound/preset%1/").arg(slot + 1);
}

// QSettings turns a comma list into a string list when it reads an ini file
// back, so numbers are kept in one semicolon-separated string.
QString join(const QVector<double> &values)
{
    QStringList parts;
    for (double value : values)
        parts.append(QString::number(value));
    return parts.join(';');
}

QVector<double> split(const QVariant &stored, int count, double limit, double step)
{
    QVector<double> values(count, 0.0);
    const QStringList parts = stored.toString().split(';', Qt::SkipEmptyParts);
    for (int i = 0; i < count && i < parts.size(); ++i) {
        const double raw = parts.at(i).toDouble();
        values[i] = qBound(-limit, std::round(raw / step) * step, limit);
    }
    return values;
}

double snap(double value, double limit, double step)
{
    return qBound(-limit, std::round(value / step) * step, limit);
}

}

SoundState::SoundState(QSettings *prefs, QObject *parent) : QObject(parent), m_prefs(prefs)
{
    load();
}

SoundState *SoundState::of(AppContext *app)
{
    if (auto *state = app->findChild<SoundState *>(QString(), Qt::FindDirectChildrenOnly))
        return state;
    return new SoundState(app->prefs(), app);
}

SoundEffects SoundState::read(const QString &prefix) const
{
    SoundEffects effects;
    effects.bands = split(m_prefs->value(prefix + "bands"), bandCount, bandLimit, 0.5);
    effects.tone = split(m_prefs->value(prefix + "tone"), 3, toneLimit, 1.0);
    effects.toneActive = m_prefs->value(prefix + "toneActive", false).toBool();
    effects.dseeHx = m_prefs->value(prefix + "dseeHx", false).toBool();
    effects.dcPhase = m_prefs->value(prefix + "dcPhase", false).toBool();
    effects.dcPhaseType = qBound(0, m_prefs->value(prefix + "dcPhaseType", 1).toInt(), dcPhaseTypes - 1);
    effects.dynamicNormalizer = m_prefs->value(prefix + "dynamicNormalizer", false).toBool();
    effects.vinyl = m_prefs->value(prefix + "vinyl", false).toBool();
    effects.vinylType = qBound(0, m_prefs->value(prefix + "vinylType", 0).toInt(), vinylTypes - 1);
    return effects;
}

void SoundState::write(const QString &prefix, const SoundEffects &effects)
{
    m_prefs->setValue(prefix + "bands", join(effects.bands));
    m_prefs->setValue(prefix + "tone", join(effects.tone));
    m_prefs->setValue(prefix + "toneActive", effects.toneActive);
    m_prefs->setValue(prefix + "dseeHx", effects.dseeHx);
    m_prefs->setValue(prefix + "dcPhase", effects.dcPhase);
    m_prefs->setValue(prefix + "dcPhaseType", effects.dcPhaseType);
    m_prefs->setValue(prefix + "dynamicNormalizer", effects.dynamicNormalizer);
    m_prefs->setValue(prefix + "vinyl", effects.vinyl);
    m_prefs->setValue(prefix + "vinylType", effects.vinylType);
}

void SoundState::load()
{
    m_effects = read(current);
    m_clearAudio = m_prefs->value("sound/clearAudio", false).toBool();
    m_sourceDirect = m_prefs->value("sound/sourceDirect", false).toBool() && !m_clearAudio;
    m_balance = snap(m_prefs->value("sound/balance", 0.0).toDouble(), balanceLimit, 0.5);
}

// Tells each page what differs from `before`.
void SoundState::announce(const SoundEffects &before)
{
    write(current, m_effects);
    bool any = false;
    if (before.bands != m_effects.bands || before.toneActive != m_effects.toneActive) {
        emit equalizerChanged();
        any = true;
    }
    if (before.tone != m_effects.tone) {
        emit toneChanged();
        any = true;
    }
    if (before.dseeHx != m_effects.dseeHx) {
        emit dseeHxChanged();
        any = true;
    }
    if (before.dcPhase != m_effects.dcPhase || before.dcPhaseType != m_effects.dcPhaseType) {
        emit dcPhaseChanged();
        any = true;
    }
    if (before.dynamicNormalizer != m_effects.dynamicNormalizer) {
        emit dynamicNormalizerChanged();
        any = true;
    }
    if (before.vinyl != m_effects.vinyl || before.vinylType != m_effects.vinylType) {
        emit vinylChanged();
        any = true;
    }
    if (any)
        emit changed();
}

void SoundState::setClearAudio(bool on)
{
    if (m_clearAudio == on)
        return;
    m_clearAudio = on;
    if (on)
        m_sourceDirect = false;
    m_prefs->setValue("sound/clearAudio", m_clearAudio);
    m_prefs->setValue("sound/sourceDirect", m_sourceDirect);
    emit modeChanged();
    emit changed();
}

void SoundState::setSourceDirect(bool on)
{
    if (m_sourceDirect == on)
        return;
    m_sourceDirect = on;
    if (on)
        m_clearAudio = false;
    m_prefs->setValue("sound/clearAudio", m_clearAudio);
    m_prefs->setValue("sound/sourceDirect", m_sourceDirect);
    emit modeChanged();
    emit changed();
}

void SoundState::setBand(int index, double db)
{
    if (index < 0 || index >= bandCount)
        return;
    const SoundEffects before = m_effects;
    m_effects.bands[index] = snap(db, bandLimit, 0.5);
    announce(before);
}

void SoundState::setTone(int index, double value)
{
    if (index < 0 || index >= 3)
        return;
    const SoundEffects before = m_effects;
    m_effects.tone[index] = snap(value, toneLimit, 1.0);
    announce(before);
}

void SoundState::setToneActive(bool tone)
{
    const SoundEffects before = m_effects;
    m_effects.toneActive = tone;
    announce(before);
}

void SoundState::setDseeHx(bool on)
{
    const SoundEffects before = m_effects;
    m_effects.dseeHx = on;
    announce(before);
}

void SoundState::setDcPhase(bool on)
{
    const SoundEffects before = m_effects;
    m_effects.dcPhase = on;
    announce(before);
}

void SoundState::setDcPhaseType(int type)
{
    if (type < 0 || type >= dcPhaseTypes)
        return;
    const SoundEffects before = m_effects;
    m_effects.dcPhaseType = type;
    announce(before);
}

void SoundState::setDynamicNormalizer(bool on)
{
    const SoundEffects before = m_effects;
    m_effects.dynamicNormalizer = on;
    announce(before);
}

void SoundState::setVinyl(bool on)
{
    const SoundEffects before = m_effects;
    m_effects.vinyl = on;
    announce(before);
}

void SoundState::setVinylType(int type)
{
    if (type < 0 || type >= vinylTypes)
        return;
    const SoundEffects before = m_effects;
    m_effects.vinylType = type;
    announce(before);
}

void SoundState::setBalance(double db)
{
    const double value = snap(db, balanceLimit, 0.5);
    if (value == m_balance)
        return;
    m_balance = value;
    m_prefs->setValue("sound/balance", m_balance);
    emit balanceChanged();
    emit changed();
}

bool SoundState::presetSaved(int slot) const
{
    return slot >= 0 && slot < presetCount && m_prefs->value(presetPrefix(slot) + "saved", false).toBool();
}

void SoundState::savePreset(int slot)
{
    if (slot < 0 || slot >= presetCount)
        return;
    write(presetPrefix(slot), m_effects);
    m_prefs->setValue(presetPrefix(slot) + "saved", true);
    emit presetsChanged();
}

void SoundState::loadPreset(int slot)
{
    if (slot < 0 || slot >= presetCount)
        return;
    const SoundEffects before = m_effects;
    m_effects = presetSaved(slot) ? read(presetPrefix(slot)) : SoundEffects();
    announce(before);
}

void SoundState::reset()
{
    const SoundEffects before = m_effects;
    const bool clear = m_clearAudio;
    const bool direct = m_sourceDirect;
    const double balance = m_balance;
    m_prefs->remove("sound");
    m_effects = SoundEffects();
    m_clearAudio = false;
    m_sourceDirect = false;
    m_balance = 0.0;
    announce(before);
    if (clear != m_clearAudio || direct != m_sourceDirect) {
        emit modeChanged();
        emit changed();
    }
    if (balance != m_balance) {
        emit balanceChanged();
        emit changed();
    }
    emit presetsChanged();
}
