import QtQuick 2.3

// A Loader that the probe's `reload` command (probe/mkspec/probe.h) can
// refresh: it lets go of `file` while wmReloadToken is negative and loads it
// again, from the swapped overlay, once the token is back. What the app's C++
// binds by name stays in the file that holds the WmHot; the loaded file
// reaches that file's ids, but the Loader's own properties (width, item,
// source, file, ...) shadow outer names that are not qualified.
Loader {
    property int wmReloadToken: 0
    property url file
    source: wmReloadToken < 0 ? "" : file
}
