#include "screensettings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

static QJsonObject readObject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = "Cannot open " + path;
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!document.isObject()) {
        *error = path + ": " + parse.errorString();
        return {};
    }
    return document.object();
}

static QStringList strings(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &item : value.toArray())
        result.append(item.toString());
    return result;
}

QString ScreenSettings::load(const QString &settingsPath, const QString &layoutsPath)
{
    QString error;
    const QJsonObject root = readObject(settingsPath, &error);
    if (!error.isEmpty())
        return error;
    const QJsonObject model = root.value("TestModel").toObject();
    if (model.isEmpty())
        return settingsPath + ": no TestModel section";
    const QJsonObject layoutObject = readObject(layoutsPath, &error);
    if (!error.isEmpty())
        return error;

    const QJsonObject screenObject = model.value("screen").toObject();
    for (auto it = screenObject.begin(); it != screenObject.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        ScreenInfo info;
        info.window = entry.value("window").toString();
        info.viewModel = entry.value("viewmodel").toString();
        info.earlyLoad = entry.value("earlyload").toBool();
        info.layer = entry.value("layer").toInt();
        info.alignment = entry.value("alignment").toInt();
        screens.insert(it.key(), info);
        screenIds.append(it.key());
    }
    const QJsonObject windowObject = model.value("window").toObject();
    for (auto it = windowObject.begin(); it != windowObject.end(); ++it) {
        windowIds.append(it.key());
        windowViewModels.insert(it.key(), it.value().toObject().value("viewmodel").toString());
    }
    popupIds = model.value("view").toObject().keys();
    for (auto it = layoutObject.begin(); it != layoutObject.end(); ++it)
        layouts.insert(it.key(), it.value().toString());

    const QJsonObject transitionObject = model.value("transition").toObject();
    for (auto it = transitionObject.begin(); it != transitionObject.end(); ++it) {
        QVector<Transition> list;
        for (const QJsonValue &value : it.value().toArray()) {
            const QJsonObject entry = value.toObject();
            list.append({entry.value("action").toString(), entry.value("condition").toInt(),
                         entry.value("history").toInt(), entry.value("next").toString()});
        }
        transitions.insert(it.key(), list);
    }
    const QJsonObject popupObject = model.value("popup").toObject();
    for (auto it = popupObject.begin(); it != popupObject.end(); ++it) {
        QVector<PopupRule> list;
        for (const QJsonValue &value : it.value().toArray()) {
            const QJsonObject entry = value.toObject();
            list.append({entry.value("action").toString(), entry.value("condition").toInt(),
                         entry.value("show").toString()});
        }
        popups.insert(it.key(), list);
    }
    const QJsonObject aliasObject = model.value("alias").toObject();
    for (auto it = aliasObject.begin(); it != aliasObject.end(); ++it)
        aliases.insert(it.key(), strings(it.value()));
    style = model.value("style").toObject().toVariantMap();

    for (const QString &id : screenIds) {
        if (!layouts.contains(id))
            return "No QML layout for screen " + id;
    }
    return {};
}

const Transition *ScreenSettings::find(const QString &screen, const QString &action, int condition) const
{
    QStringList owners{screen};
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        if (it.value().contains(screen))
            owners.append(it.key());
    }
    for (const QString &owner : owners) {
        const auto list = transitions.find(owner);
        if (list == transitions.end())
            continue;
        const Transition *fallback = nullptr;
        for (const Transition &entry : *list) {
            if (entry.action != action)
                continue;
            if (entry.condition == condition)
                return &entry;
            if (entry.condition == 0 && !fallback)
                fallback = &entry;
        }
        if (fallback)
            return fallback;
    }
    return nullptr;
}

bool ScreenSettings::hasAction(const QString &screen, const QString &action) const
{
    for (const Transition &entry : transitions.value(screen)) {
        if (entry.action == action)
            return true;
    }
    return false;
}

QString ScreenSettings::popupFor(const QString &screen, const QString &action, int condition) const
{
    QStringList owners{screen};
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        if (it.value().contains(screen))
            owners.append(it.key());
    }
    for (const QString &owner : owners) {
        for (const PopupRule &rule : popups.value(owner)) {
            if (rule.action == action && (rule.condition == condition || rule.condition == 0))
                return rule.show;
        }
    }
    return {};
}
