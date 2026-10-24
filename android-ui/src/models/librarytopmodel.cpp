#include "librarytopmodel.h"

#include "../../library/musicdb.h"
#include "../appcontext.h"
#include "../rowmodel.h"
#include "../screencontroller.h"

namespace {

struct Icon {
    int id;
    const char *image;
    const char *name;
    const char *line;
};

// The A50 series' categories, in its default order. Names are the two-line
// variants the stock icon grid shows; lines are the one-line ones of the
// icon select list.
const Icon icons[] = {
    {0, "all_songs", "020211", "020036"},      {1, "albums", "020037", "020037"},
    {2, "artists", "020038", "020038"},        {3, "genres", "020039", "020039"},
    {4, "release_year", "020212", "020040"},   {5, "composers_year", "020213", "020041"},
    {6, "playlists", "020214", "020042"},      {7, "hi_res", "020216", "020044"},
    {8, "folders", "020045", "020045"},        {10, "recently_added", "020215", "020043"},
    {12, "sensme_ww", "020217", "020005"},
};

// Every icon, in the order the user arranged them.
QVector<const Icon *> arranged(const QSettings *prefs)
{
    const QStringList order = prefs->value("library/iconOrder").toStringList();
    QVector<const Icon *> result;
    for (const QString &saved : order) {
        for (const Icon &icon : icons) {
            if (QString::number(icon.id) == saved)
                result.append(&icon);
        }
    }
    for (const Icon &icon : icons) {
        if (!result.contains(&icon))
            result.append(&icon);
    }
    return result;
}

QStringList hiddenIcons(const QSettings *prefs)
{
    return prefs->value("library/hiddenIcons").toStringList();
}

QVariantMap iconRow(const Icon &icon)
{
    const QString base = QString("qrc:/opt2/appresouce/btn_music_library_filter_%1").arg(icon.image);
    return {{"id", icon.id}, {"icon_name", sonyText("dmpapp::LibraryTopModelList", icon.name)},
            {"file_name", base + "_normal.png"}, {"pressed_file_name", base + "_pressed.png"},
            {"selected_file_name", base + "_selected.png"}, {"back_color", "#FF6757"}};
}

}

LibraryTopModel::LibraryTopModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent),
      m_icons(new RowModel({"id", "file_name", "pressed_file_name", "selected_file_name", "back_color", "icon_name"}, this))
{
    load();
    QVariantMap functions{{"function_icon_width", 120}, {"function_icon_space", 0}};
    for (const char *name : {"fm", "language", "direct_rec", "dac", "bt_receiver"}) {
        functions.insert(QString(name) + "_icon_normal", "");
        functions.insert(QString(name) + "_icon_pressed", "");
    }
    setValues({{"icon_list", QVariant::fromValue<QObject *>(m_icons)},
               {"pageindex", app->prefs()->value("library/page", 0).toInt()},
               {"isAvailableFmRadio", false}, {"isAvailableRecorder", false}, {"isAvailableLangStudy", false},
               {"isAvailableDACMode", false}, {"is_available_bt_receiver", false}, {"isDemoMode", false},
               {"is_available_basic_player_controls", true}, {"function_icon_info", functions}});
}

void LibraryTopModel::load()
{
    m_hidden = hiddenIcons(app()->prefs());
    m_order = app()->prefs()->value("library/iconOrder").toStringList();
    QVector<QVariantMap> rows;
    for (const Icon *icon : arranged(app()->prefs())) {
        if (!m_hidden.contains(QString::number(icon->id)))
            rows.append(iconRow(*icon));
    }
    m_icons->replace(rows);
}

void LibraryTopModel::activate()
{
    // The icon chooser and Reset All Settings change these elsewhere.
    if (hiddenIcons(app()->prefs()) != m_hidden || app()->prefs()->value("library/iconOrder").toStringList() != m_order) {
        load();
        call("bindingOnListChanged");
    }
}

void LibraryTopModel::save()
{
    QStringList order;
    for (const QVariantMap &row : m_icons->rows())
        order.append(row.value("id").toString());
    // Hidden icons keep their places after the shown ones.
    for (const Icon *icon : arranged(app()->prefs())) {
        if (!order.contains(QString::number(icon->id)))
            order.append(QString::number(icon->id));
    }
    app()->prefs()->setValue("library/iconOrder", order);
    m_order = order;
}

LibraryIconSelectModel::LibraryIconSelectModel(const QString &id, AppContext *app, QObject *parent)
    : ScreenModel(id, app, parent), m_rows(new RowModel({"id", "icon_name_oneline", "visibility"}, this))
{
    set("page_title", sonyText("dmpapp::LibraryIconSelectViewModel", "020262"));
    set("properties", QVariant::fromValue<QObject *>(m_rows));
    reset();
}

void LibraryIconSelectModel::reset()
{
    const QStringList hidden = hiddenIcons(app()->prefs());
    QVector<QVariantMap> rows;
    for (const Icon *icon : arranged(app()->prefs())) {
        const QString id = QString::number(icon->id);
        rows.append({{"id", icon->id}, {"icon_name_oneline", sonyText("dmpapp::LibraryTopModelList", icon->line)},
                     {"visibility", !hidden.contains(id)}});
    }
    m_rows->replace(rows);
    set("all_icon_invisible", false);
}

void LibraryIconSelectModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "checkBoxClicked") {
        const int index = args.value(0).toInt();
        QVariantMap row = m_rows->row(index);
        if (row.isEmpty())
            return;
        row.insert("visibility", !row.value("visibility").toBool());
        m_rows->setRow(index, row);
        bool none = true;
        for (const QVariantMap &each : m_rows->rows())
            none &= !each.value("visibility").toBool();
        set("all_icon_invisible", none);
        if (none)
            call("openLibraryIconSelectWarningDialog");
    } else if (signal == "accepted") {
        QStringList hidden;
        for (const QVariantMap &row : m_rows->rows()) {
            if (!row.value("visibility").toBool())
                hidden.append(row.value("id").toString());
        }
        app()->prefs()->setValue("library/hiddenIcons", hidden);
        navigate("ToMusicPlayerTop");
    } else if (signal == "rejected") {
        navigate("ToMusicPlayerTop");
    } else if (signal != "warningDialogClosed") {
        ScreenModel::handle(signal, args);
    }
}

void LibraryIconSelectModel::entering()
{
    reset();
}

void LibraryTopModel::handle(const QByteArray &signal, const QVariantList &args)
{
    if (signal == "touchListAt") {
        // An empty category answers with the stock message, not a blank list.
        const int icon = m_icons->row(args.value(0).toInt()).value("id").toInt();
        const MusicDb *db = app()->library();
        const auto hiRes = [db](int id) { return db->track(id).isHiRes(); };
        if (icon == 6 && db->playlists().isEmpty())
            app()->message(sonyText("QObject", "020267"));
        else if (icon != 8 && db->count() == 0)
            app()->message(sonyText("QObject", "020197"));
        else if (icon == 7 && db->select(hiRes).isEmpty())
            app()->message(sonyText("QObject", "020200"));
        else
            navigate("selectedIcon", icon);
    } else if (signal == "iconListMove") {
        m_icons->move(args.value(0).toInt(), args.value(1).toInt());
    } else if (signal == "iconListMoveEnd") {
        save();
        call("bindingOnListChanged");
    } else if (signal == "iconListInitialize") {
        app()->prefs()->remove("library/iconOrder");
        load();
        call("bindingOnListChanged");
    } else if (signal == "setpageindex") {
        app()->prefs()->setValue("library/page", args.value(0).toInt());
        set("pageindex", args.value(0).toInt());
    } else {
        ScreenModel::handle(signal, args);
    }
}

QVector<ScreenModel::MenuItem> LibraryTopModel::menu()
{
    const char *context = "dmpapp::LibraryTopViewModel";
    return {
        {sonyText(context, "010020"), [this] { navigate("OptionMenu0"); }},
        {sonyText(context, "020035"), [this] { navigate("OptionMenu1"); }},
        {sonyText(context, "020209"), [this] { call("openIconListInitialDialog"); }},
        {sonyText(context, "010057"), [this] { app()->openPopup("LibraryTopHelpGuide"); }},
    };
}
