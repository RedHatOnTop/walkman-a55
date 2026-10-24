#ifndef SENSME_H
#define SENSME_H

#include <QtCore/QByteArray>
#include <QtCore/QString>

// SensMe Channels from 12 TONE ANALYSIS results (the SMFMF blob Music Center
// for PC stores in a track), classified the way the A55's media store did.
namespace SensMe {

// The media store's channel numbers; a track's channel set is a mask of 1 << Channel.
enum Channel {
    Morning, Daytime, Evening, Night, Midnight,
    Active, Relax, Mellow, Upbeat, Emotional, Lounge, Dance, Extreme,
    ChannelCount
};

const int kFeatureCount = 86;

// The feature bytes (Sony's feature words 4..89) and the highlight start of
// the last STSA chunk in ms, or -1. False when the blob yields no features.
bool parse(const QByteArray &smfmf, QByteArray *features, qint32 *sabiMs);

// The cluster centroids and lookup tables, staged into res/sensme.bin from the
// device's libMediaStoreService.so. Until loaded, channels() returns -1.
bool loadModel(const QString &path);

// The channel mask of a track, 0 when it fits no channel (Shuffle All only;
// also when a cluster word is missing), -1 without a model or with malformed
// features. *cluster gets the 1..48 cluster, 0 when there is none.
int channels(const QByteArray &features, int *cluster = nullptr);

}

#endif
