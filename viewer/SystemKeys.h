/*
 * RetroShare Remote Desktop plugin
 * Copyright (C) 2026 RemoteDesktop contributors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
class QWidget;
class DesktopView;

// Windows only: while the viewer window is active and the remote desktop has
// keyboard focus, keys the OS would otherwise keep (Win, Alt+Tab, Alt+Esc,
// Ctrl+Esc, Alt+Space, Menu) are sent to the remote desktop instead.
// On other platforms these functions do nothing.
namespace SystemKeys {
void install(QWidget* window, DesktopView* view);
void uninstall();
void setEnabled(bool enabled);
bool supported();
}
