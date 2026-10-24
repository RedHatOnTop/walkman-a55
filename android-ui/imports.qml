// Not loaded at runtime. It lists the QML modules the Sony stock UI imports from
// inside stock.rcc (which is registered as a binary resource at startup, so
// androiddeployqt's qmlimportscanner cannot see those imports). qml-root-path
// points at this directory, so listing them here makes the APK bundle the
// modules. Import versions mirror the highest the Sony QML uses.
import QtQuick 2.3
import QtQuick.Window 2.2
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import QtQuick.Dialogs 1.2
import QtQml.Models 2.1
import QtGraphicalEffects 1.0

Item {}
