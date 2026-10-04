#pragma once

class QGuiApplication;

namespace Zuuned {

// Give direct AppImage launches the same desktop identity as an installed
// zuuned.desktop entry. Compositors use this for the live window/taskbar icon.
void configureApplicationIdentity(QGuiApplication &app);

} // namespace Zuuned
