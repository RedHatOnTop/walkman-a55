#include "screencontroller.h"

#include <QtCore/QDebug>

namespace {

// W.music's cruciform pager settles a tapped page change in
// (1 + pages) * 100 ms horizontally and * 150 ms vertically
// (crosstransition/view/ViewPager.java, WmVerticalViewPager.java).
int tapSettleDuration(int direction)
{
    return qAbs(direction) == 1 ? 200 : 300;
}

// W.music fades its operation-guard blind out over 150 ms
// (ScreenTransitionController.BLIND_FADEOUT_DURATION_*).
constexpr int fadeDuration = 150;

const char *swipeAction(int direction)
{
    switch (direction) {
    case ScreenController::Up: return "SwipeDown";
    case ScreenController::Down: return "SwipeUp";
    case ScreenController::Left: return "SwipeRight";
    case ScreenController::Right: return "SwipeLeft";
    default: return "";
    }
}

}

ScreenController::ScreenController(const ScreenSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_empty(new ScreenProxy({}, {}, this))
{
    qRegisterMetaType<ScreenProxy *>();
    for (const QString &id : settings->screenIds)
        m_proxies.insert(id, new ScreenProxy(id, settings->window(id), this));
    for (const QString &id : settings->windowIds)
        m_proxies.insert(id, new ScreenProxy(id, id, this));
    connect(this, &ScreenController::reconfigureScreen, this, [this] { ReconfigureScreenByWindow(); });
}

ScreenProxy *ScreenController::proxy(const QString &screen) const
{
    return m_proxies.value(screen, m_empty);
}

QObject *ScreenController::windowController() const
{
    return proxy(windowID());
}

QObject *ScreenController::neighbour(int direction) const
{
    return proxy(m_neighbours.value(direction));
}

QString ScreenController::neighbourId(int direction) const
{
    return m_neighbours.value(direction);
}

QString ScreenController::action(int direction) const
{
    return QString::fromLatin1(swipeAction(direction));
}

QVariantMap ScreenController::allViewLayouts() const
{
    QVariantMap result;
    for (auto it = m_settings->layouts.begin(); it != m_settings->layouts.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QStringList ScreenController::preload() const
{
    QStringList result;
    for (int direction : {Up, Down, Left, Right}) {
        const QString id = m_neighbours.value(direction);
        if (!id.isEmpty())
            result.append(id);
    }
    return result;
}

void ScreenController::setEarlyLoad(const QStringList &screens)
{
    if (screens == m_earlyLoad)
        return;
    m_earlyLoad = screens;
    emit earlyLoadChanged();
}

int ScreenController::conditionFor(const QString &action) const
{
    return m_condition ? m_condition(m_screen, action) : 0;
}

QString ScreenController::historyTarget(const QString &next) const
{
    // History 2 returns to where the user came from on the target's layer:
    // swiping down from the player lands on the album list it was started
    // from, not always on the library top.
    const int layer = m_settings->layer(next);
    const QString window = m_settings->window(next);
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const QString &entry = m_history.at(i);
        if (m_settings->layer(entry) == layer && m_settings->window(entry) == window)
            return entry;
    }
    return next;
}

void ScreenController::recomputeNeighbours()
{
    m_neighbours.clear();
    m_neighbourHistory.clear();
    for (int direction : {Up, Down, Left, Right}) {
        const QString name = action(direction);
        const Transition *entry = m_settings->find(m_screen, name, conditionFor(name));
        if (!entry || entry->next == m_screen)
            continue;
        const QString target = entry->history == 2 ? historyTarget(entry->next) : entry->next;
        if (target.isEmpty() || target == m_screen)
            continue;
        m_neighbours.insert(direction, target);
        m_neighbourHistory.insert(direction, entry->history);
    }
    emit updateDeploy();
}

void ScreenController::start(const QString &screen)
{
    m_screen = screen;
    m_history.clear();
    recomputeNeighbours();
    emit updateScreen();
    emit presentScreen(m_screen, windowID());
    emit proxy(m_screen)->activated();
    emit proxy(m_screen)->available(nullptr);
    emit screenChanged(m_screen);
}

bool ScreenController::beginSwipe(int direction)
{
    const QString to = m_neighbours.value(direction);
    if (to.isEmpty() || busy() || !m_enabled)
        return false;
    m_pending = {to, action(direction), m_neighbourHistory.value(direction, 1), direction, true};
    m_dragOffset = 0;
    emit busyChanged();
    // A finger-driven change always slides, as W.music's pagers do; the
    // effect table only chooses for button-driven changes.
    emit beginTransition(m_screen, to, windowID(), m_settings->window(to), "swipe", direction, true);
    return true;
}

void ScreenController::setDragOffset(qreal offset)
{
    if (m_pending.isEmpty() || !m_pending.interactive || qFuzzyCompare(offset, m_dragOffset))
        return;
    m_dragOffset = offset;
    emit dragOffsetChanged();
}

void ScreenController::settleSwipe(bool complete, int duration)
{
    if (m_pending.isEmpty())
        return;
    emit locked();
    emit settleTransition(complete, duration);
}

int ScreenController::programmaticDirection(const QString &to) const
{
    for (auto it = m_neighbours.begin(); it != m_neighbours.end(); ++it) {
        if (it.value() == to)
            return it.key();
    }
    // W.music keeps the library and the player on one vertical pager, so the
    // Library and Now Playing buttons slide the player up or down from any
    // library depth. The stock A55 only did that from the library top.
    const QStringList players = m_settings->aliases.value("MusicPlayers");
    const bool sameWindow = m_settings->window(to) == windowID();
    if (sameWindow && players.contains(to) && m_settings->layer(m_screen) == 0)
        return Down;
    if (sameWindow && players.contains(m_screen) && m_settings->layer(to) == 0)
        return Up;
    return None;
}

bool ScreenController::navigate(const QString &to, int history, const QString &actionName)
{
    if (to.isEmpty() || to == m_screen || busy() || !m_settings->screens.contains(to))
        return false;
    const int direction = programmaticDirection(to);
    const QString effect = direction == None ? QString("fade") : QString("swipe");
    m_pending = {to, actionName, history, direction == None ? Right : direction, false};
    m_dragOffset = 0;
    emit busyChanged();
    emit locked();
    emit beginTransition(m_screen, to, windowID(), m_settings->window(to), effect, m_pending.direction, false);
    emit settleTransition(true, effect == "swipe" ? tapSettleDuration(m_pending.direction) : fadeDuration);
    return true;
}

bool ScreenController::doAction(const QString &actionName, int condition)
{
    const Transition *entry = m_settings->find(m_screen, actionName,
                                               condition >= 0 ? condition : conditionFor(actionName));
    if (!entry)
        return false;
    const QString target = entry->history == 2 ? historyTarget(entry->next) : entry->next;
    return navigate(target, entry->history, actionName);
}

bool ScreenController::forward(const QString &screen, int history)
{
    return navigate(screen, history, {});
}

void ScreenController::Back()
{
    if (busy())
        return;
    if (!m_history.isEmpty()) {
        navigate(m_history.last(), -1, "Back");
        return;
    }
    if (m_settings->hasAction(m_screen, "NonHistoryBack") && doAction("NonHistoryBack"))
        return;
    emit backgroundRequested();
}

void ScreenController::BackToTop()
{
    if (doAction("MusicLibraryTop") || doAction("BackToTop"))
        return;
    if (m_screen != "MusicPlayerTop")
        navigate("MusicPlayerTop", 3, "BackToTop");
}

void ScreenController::GoToPlayback()
{
    if (doAction("NowPlaying"))
        return;
    const Transition *entry = m_settings->find("MusicPlayerTop", "SwipeUp", conditionFor("SwipeUp"));
    navigate(entry ? entry->next : QString("MusicPlayer"), 1, "NowPlaying");
}

void ScreenController::commit(const Pending &pending)
{
    const QString from = m_screen;
    switch (pending.history) {
    case -1: // back
        if (!m_history.isEmpty())
            m_history.removeLast();
        break;
    case 1:
        m_history.append(from);
        break;
    case 2: {
        const int index = m_history.lastIndexOf(pending.to);
        if (index >= 0)
            m_history = m_history.mid(0, index);
        break;
    }
    case 3: // a root: the library top
        m_history.clear();
        break;
    default:
        break;
    }
    m_screen = pending.to;
    emit proxy(from)->unavailable(nullptr);
    emit proxy(from)->inactivated();
    recomputeNeighbours();
    emit updateScreen();
    emit presentScreen(m_screen, windowID());
    emit proxy(m_screen)->activated();
    emit proxy(m_screen)->available(nullptr);
    emit screenChanged(m_screen);
}

void ScreenController::transitionDone(bool completed)
{
    if (m_pending.isEmpty())
        return;
    const Pending pending = m_pending;
    m_pending = {};
    m_dragOffset = 0;
    if (completed)
        commit(pending);
    emit busyChanged();
    emit unlocked();
}

void ScreenController::showPopup(const QString &popup)
{
    if (popup.isEmpty() || m_popups.contains(popup))
        return;
    m_popups.append(popup);
    emit updatePopup();
}

void ScreenController::ClosePopup(const QString &object_name)
{
    QString id = object_name;
    for (auto it = m_popupViews.begin(); it != m_popupViews.end(); ++it) {
        if (it.value() && it.value()->objectName() == object_name)
            id = it.key();
    }
    if (m_popups.removeAll(id) > 0) {
        m_popupViews.remove(id);
        emit updatePopup();
    }
}

void ScreenController::LoadedView(const QString &id, QObject *view)
{
    auto *item = qobject_cast<QQuickItem *>(view);
    if (!item)
        return;
    m_views.insert(id, item);
    emit viewLoaded("screen", id, item);
    emit proxy(id)->loadedViewModel(nullptr);
}

void ScreenController::LoadedWindow(const QString &id, QObject *view)
{
    auto *item = qobject_cast<QQuickItem *>(view);
    if (!item)
        return;
    m_windowViews.insert(id, item);
    emit viewLoaded("window", id, item);
}

void ScreenController::LoadedPopup(const QString &id, QObject *view)
{
    auto *item = qobject_cast<QQuickItem *>(view);
    if (!item)
        return;
    m_popupViews.insert(id, item);
    emit viewLoaded("popup", id, item);
}

void ScreenController::setWindowSize(const QSize &size)
{
    if (size == m_windowSize || size.isEmpty())
        return;
    m_windowSize = size;
    emit updatedScreenArea();
    ReconfigureScreenByWindow();
}

void ScreenController::ReconfigureScreenByWindow(QObject *window)
{
    auto *root = qobject_cast<QQuickItem *>(window);
    if (!root)
        root = currentWindowView();
    if (!root)
        return;
    int top = 0;
    int bottom = m_windowSize.height();
    int navigation = -1;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        const QString name = child->objectName();
        if (!child->isVisible() || child->opacity() < 1.0 || child->height() <= 0)
            continue;
        const QRectF rect = child->mapRectToItem(root, QRectF(0, 0, child->width(), child->height()));
        if (name == "navigationBar" && rect.center().y() < m_windowSize.height() / 2)
            navigation = int(rect.top());
        if (name == "headerArea" || (name == "navigationBar" && rect.center().y() < m_windowSize.height() / 2))
            top = qMax(top, int(rect.bottom()));
        else if (name == "basicPlayerControls" || name == "navigationBar")
            bottom = qMin(bottom, int(rect.top()));
    }
    const QRect area(0, top, m_windowSize.width(), qMax(0, bottom - top));
    navigation = navigation < 0 ? top : navigation;
    if (area == m_area && navigation == m_navigationY)
        return;
    m_area = area;
    m_navigationY = navigation;
    emit updatedScreenArea();
}
