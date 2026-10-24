#include "queue.h"

#include <algorithm>
#include <numeric>
#include <random>

QueueState createQueue(int count, int selected)
{
    QueueState state;
    if (count <= 0 || selected < 0 || selected >= count)
        return state;
    QVector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    state.order = order;
    state.cursor = selected;
    return state;
}

int queueTrack(const QueueState &state)
{
    return state.cursor >= 0 && state.cursor < state.order.size()
        ? state.order.at(state.cursor) : -1;
}

QueueState advanceQueue(const QueueState &state, int direction, bool automatic)
{
    if (queueTrack(state) < 0 || (automatic && state.repeat == 1))
        return state;
    const int next = state.cursor + (direction < 0 ? -1 : 1);
    QueueState result = state;
    if (next >= 0 && next < state.order.size())
        result.cursor = next;
    else if (state.repeat == 2)
        result.cursor = direction < 0 ? state.order.size() - 1 : 0;
    else if (automatic)
        result.cursor = -1;
    return result;
}

QueueState withRepeat(const QueueState &state, int repeat)
{
    QueueState result = state;
    result.repeat = repeat >= 0 && repeat <= 2 ? repeat : state.repeat;
    return result;
}

QueueState withShuffle(const QueueState &state, bool enabled, unsigned seed)
{
    const int current = queueTrack(state);
    if (current < 0 || enabled == state.shuffled)
        return state;
    QueueState result = createQueue(state.order.size(), current);
    result.repeat = state.repeat;
    result.shuffled = enabled;
    if (!enabled)
        return result;
    QVector<int> order = result.order;
    order.removeAt(current);
    std::mt19937 random(seed);
    std::shuffle(order.begin(), order.end(), random);
    order.prepend(current);
    result.order = order;
    result.cursor = 0;
    return result;
}
