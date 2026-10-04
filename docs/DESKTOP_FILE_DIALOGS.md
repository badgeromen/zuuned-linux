# Desktop file and folder dialogs

The user requires the desktop's native picker and its own theme for all file
and folder selection. The app uses `QtQuick.Dialogs` for library watch folders,
artwork, wallpaper, playlist import and diagnostic export. These declarations
must keep native dialogs enabled; do not replace them with an app file browser.

## September 9 packaging regression

The `9a6eeb6` AppImage omitted
`usr/plugins/platformthemes/libqxdgdesktopportal.so`. It bundled the QML dialog
module but not the platform integration that provides the desktop chooser.
On a desktop where the host Qt theme plugin could not load into the bundled Qt,
the standard Qt dialog fell back to its plain internal file browser. The old
container gate could click this fallback and pass, so it did not prove native
desktop integration.

The corrected package explicitly bundles the portal plugin from the same Qt
installation as the app and deploys its ELF dependencies. AppRun selects
`xdgdesktopportal` instead of inheriting a host Qt plugin with an incompatible
Qt version. Native source installations default to the portal when no platform
theme is configured. The former process-level `GTK_THEME=Adwaita:dark` force
is removed; the app's dark palette applies to the app only.

Qt delegates folder/open/save requests over the session bus to the desktop's
`org.freedesktop.portal.FileChooser` service. The desktop chooses its configured
backend, so GTK/KDE and other supported desktop pickers keep their own theme,
places, sidebar and mounted locations. A default directory-opening application
is not itself a file-selection API; the portal is the desktop's integration for
returning a user's selection to the app.

References: [Qt 6.8.2 portal theme](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportaltheme.cpp),
[Qt portal file-dialog implementation](https://github.com/qt/qtbase/blob/v6.8.2/src/plugins/platformthemes/xdgdesktopportal/qxdgdesktopportalfiledialog.cpp).

## Installation and verification

The build baseline installs `qt6-xdgdesktopportal-platformtheme`. Runtime desktops
must supply `xdg-desktop-portal` and a working FileChooser backend; the AppImage
does not install or override the user's desktop services/configuration. If the
service is unavailable, Qt can still fall back; that is not the intended or
certified desktop experience.

- The native build and 20 packaging helper checks pass.
- The strengthened bundle inventory rejects the old `9a6eeb6` AppDir specifically
  because the portal plugin is missing.
- The packaged runtime gate must exercise actual desktop portal requests and
  responses, the backend's visible picker, and resulting folder/file/save
  selections. Dialog screenshots alone cannot establish this.
- The clean `87b2ecd` candidate passes actual GTK portal folder selection/import,
  playlist file selection/import and report save, with correlated D-Bus/PID/URI
  evidence. See [the candidate record](releases/TESTER_R2_DESKTOP_DIALOGS.md)
  for its checksum, exact harness and remaining host-desktop scope.
