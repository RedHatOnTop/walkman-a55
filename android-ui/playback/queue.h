#ifndef PLAYBACK_QUEUE_H
#define PLAYBACK_QUEUE_H

#include <QtCore/QVector>

struct QueueState {
    QVector<int> order;
    int cursor = -1;
    int repeat = 0;
    bool shuffled = false;
};

QueueState createQueue(int count, int selected);
int queueTrack(const QueueState &state);
QueueState advanceQueue(const QueueState &state, int direction, bool automatic);
QueueState withRepeat(const QueueState &state, int repeat);
QueueState withShuffle(const QueueState &state, bool enabled, unsigned seed);

#endif
