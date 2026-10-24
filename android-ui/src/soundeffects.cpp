#include "soundeffects.h"

#include <QtCore/QByteArray>

#include <memory>
#include <string.h>

#include "../audiofx/fxsettings.h"
#include "../playback/platform.h"
#include "../playback/player.h"
#include "models/soundstate.h"

namespace {

QByteArray settingsFor(const SoundState *sound, const PlayerController *player)
{
    WalkmanFxSettings settings;
    memset(&settings, 0, sizeof settings);
    settings.mode = sound->sourceDirect() ? WALKMANFX_SOURCE_DIRECT
                    : sound->clearAudio() ? WALKMANFX_CLEAR_AUDIO
                                          : WALKMANFX_NORMAL;
    const SoundEffects effects = sound->effects();
    settings.toneActive = effects.toneActive;
    for (int i = 0; i < WALKMANFX_BANDS; ++i)
        settings.bands[i] = float(effects.bands.value(i));
    for (int i = 0; i < 3; ++i)
        settings.tone[i] = float(effects.tone.value(i));
    settings.dseeHx = effects.dseeHx;
    settings.dcPhase = effects.dcPhase;
    settings.dcPhaseType = effects.dcPhaseType;
    settings.dynamicNormalizer = effects.dynamicNormalizer;
    settings.vinyl = effects.vinyl;
    settings.vinylType = effects.vinylType;
    settings.hiResSource = player->hiRes();
    return QByteArray(reinterpret_cast<const char *>(&settings), sizeof settings);
}

}

void attachSoundEffects(SoundState *sound, PlayerController *player)
{
    struct Sent {
        QByteArray settings;
        bool playing = false;
    };
    const auto sent = std::make_shared<Sent>();
    const auto send = [sound, player, sent](bool always) {
        const QByteArray settings = settingsFor(sound, player);
        if (!always && settings == sent->settings)
            return;
        sent->settings = settings;
        applySoundEffects(settings);
    };
    QObject::connect(sound, &SoundState::changed, player, [send] { send(false); });
    QObject::connect(player, &PlayerController::trackChanged, player, [send] { send(false); });
    // A restarted mediaserver starts with every effect off.
    QObject::connect(player, &PlayerController::transportChanged, player, [player, sent, send] {
        if (player->playing() && !sent->playing)
            send(true);
        sent->playing = player->playing();
    });
    send(true);
}
