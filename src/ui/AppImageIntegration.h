#pragma once

#include <QString>

#include "Common.h"

namespace namx::appimage
{

// Desktop integration for the AppImage (ROADMAP Phase 8): an AppImage is one file
// with nothing installed, so it can add itself to the applications menu — the
// .desktop launcher, the icon and the .nam / .nampreset MIME types under
// ~/.local/share — and remove itself again. Everything here is a no-op outside an
// AppImage ($APPIMAGE unset).

// Running from an AppImage.
bool is_running_as_appimage();

// Our launcher is in ~/.local/share/applications (written by add_to_menu()).
bool is_added_to_menu();

// Some launcher already starts this AppImage file: ours, or one written by
// AppImageLauncher / Gear Lever / a distro package of the same app ID. The first-run
// offer is skipped then.
bool is_integrated_by_anyone();

// Writes the launcher (Exec = this AppImage's path), the icon and the MIME types, then
// refreshes the desktop and MIME databases when their tools are installed.
expected<void, QString> add_to_menu();

// Removes what add_to_menu() wrote (files from other tools are left alone).
expected<void, QString> remove_from_menu();

// The AppImage was moved or replaced by a newer file since add_to_menu(): point our
// launcher at the current path. Silent; does nothing when not added.
void refresh_launcher_path();

} // namespace namx::appimage
