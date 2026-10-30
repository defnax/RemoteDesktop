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

#include "SystemKeys.h"
#include "DesktopView.h"
#include <QPointer>
#include <QWidget>
#include <rfb/keysym.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QPointer<QWidget> window;
QPointer<DesktopView> view;
bool enabled = true;
#ifdef Q_OS_WIN
HHOOK hook = nullptr;
LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if(code == HC_ACTION && enabled && window && view && GetForegroundWindow() == reinterpret_cast<HWND>(window->winId())) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool alt = (k->flags & LLKHF_ALTDOWN) != 0;
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        quint32 sym = 0;
        switch(k->vkCode) {
        case VK_LWIN: sym = XK_Super_L; break;
        case VK_RWIN: sym = XK_Super_R; break;
        case VK_APPS: sym = XK_Menu; break;
        case VK_TAB: if(alt) sym = XK_Tab; break;
        case VK_ESCAPE: if(alt || ctrl) sym = XK_Escape; break;
        case VK_SPACE: if(alt) sym = XK_space; break;
        default: break;
        }
        if(sym && view->forwardSystemKey(sym, down)) return 1;   // swallowed: the remote desktop gets it
    }
    return CallNextHookEx(hook, code, wParam, lParam);
}
#endif
}

namespace SystemKeys {
void install(QWidget* w, DesktopView* v) {
    window = w; view = v;
#ifdef Q_OS_WIN
    if(!hook) hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
#endif
}
void uninstall() {
#ifdef Q_OS_WIN
    if(hook) { UnhookWindowsHookEx(hook); hook = nullptr; }
#endif
    window = nullptr; view = nullptr;
}
void setEnabled(bool e) { enabled = e; }
bool supported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
}
