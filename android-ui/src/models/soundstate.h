#ifndef SOUNDSTATE_H
#define SOUNDSTATE_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

class AppContext;
class QSettings;

// What one set of sound effects holds: the part a Saved Sound Setting stores.
struct SoundEffects {
    QVector<double> bands = QVector<double>(10, 0.0);   // equalizer, dB
    QVector<double> tone = QVector<double>(3, 0.0);     // tone control: low, middle, high
    bool toneActive = false;                            // tone control rather than the equalizer
    bool dseeHx = false;
    bool dcPhase = false;
    int dcPhaseType = 1;                                // A LOW, A STANDARD, A HIGH, B LOW, B STANDARD, B HIGH
    bool dynamicNormalizer = false;
    bool vinyl = false;
    int vinylType = 0;                                  // standard, turntable resonance, arm resonance, surface noise
};

// The sound effect settings every screen that shows them shares: the three
// variants of each sound page (player, settings, DAC), the setting top and the
// sound-enhancement list. Persisted in prefs under "sound/". Nothing here
// touches audio: whoever plays it connects to the change signals.
class SoundState : public QObject
{
    Q_OBJECT
public:
    static const int bandCount = 10;
    static const int presetCount = 3;
    static const int dcPhaseTypes = 6;
    static const int vinylTypes = 4;
    // The ranges the stock views work in.
    static constexpr double bandLimit = 10.0;       // EqualizerTen: -10..+10 dB in 0.5 dB steps
    static constexpr double toneLimit = 10.0;       // ToneControlDial: -10..+10 in steps of 1
    static constexpr double balanceLimit = 3.0;     // LrBalance: -3..+3 dB in 0.5 dB steps (negative is left)

    // The one SoundState of this app.
    static SoundState *of(AppContext *app);

    // ClearAudio+ and Source Direct turn the effects off; they exclude each other.
    bool clearAudio() const { return m_clearAudio; }
    bool sourceDirect() const { return m_sourceDirect; }
    void setClearAudio(bool on);
    void setSourceDirect(bool on);

    SoundEffects effects() const { return m_effects; }
    double band(int index) const { return m_effects.bands.value(index); }
    void setBand(int index, double db);
    double tone(int index) const { return m_effects.tone.value(index); }
    void setTone(int index, double value);
    // Which of the two the player applies; the last one shown.
    bool toneActive() const { return m_effects.toneActive; }
    void setToneActive(bool tone);
    bool dseeHx() const { return m_effects.dseeHx; }
    void setDseeHx(bool on);
    bool dcPhase() const { return m_effects.dcPhase; }
    int dcPhaseType() const { return m_effects.dcPhaseType; }
    void setDcPhase(bool on);
    void setDcPhaseType(int type);
    bool dynamicNormalizer() const { return m_effects.dynamicNormalizer; }
    void setDynamicNormalizer(bool on);
    bool vinyl() const { return m_effects.vinyl; }
    int vinylType() const { return m_effects.vinylType; }
    void setVinyl(bool on);
    void setVinylType(int type);
    double balance() const { return m_balance; }
    void setBalance(double db);

    // Saved Sound Settings 1-3. A slot never saved holds the defaults.
    bool presetSaved(int slot) const;
    void savePreset(int slot);
    void loadPreset(int slot);

    // Back to the defaults; prefs under "sound/" are removed.
    void reset();

signals:
    void modeChanged();         // ClearAudio+ or Source Direct
    void equalizerChanged();    // a band, or which of equalizer and tone control is active
    void toneChanged();
    void dseeHxChanged();
    void dcPhaseChanged();
    void dynamicNormalizerChanged();
    void vinylChanged();
    void balanceChanged();
    void presetsChanged();
    // Fires after any of the signals above: the one to connect when applying every effect at once.
    void changed();

private:
    explicit SoundState(QSettings *prefs, QObject *parent = nullptr);
    SoundEffects read(const QString &prefix) const;
    void write(const QString &prefix, const SoundEffects &effects);
    void load();
    void announce(const SoundEffects &before);

    QSettings *m_prefs;
    SoundEffects m_effects;
    bool m_clearAudio = false;
    bool m_sourceDirect = false;
    double m_balance = 0.0;
};

#endif
