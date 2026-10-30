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

// Private RFB extension shared by the sharing side and the viewer window.
// It carries capabilities, chat and UTF-8 clipboard. Files are not sent over VNC:
// they go through RetroShare's own file transfer (a file link in private chat).
// The viewer advertises the extension with a pseudo-encoding; the host only
// answers clients that advertised it, and the viewer only sends after Hello.
#pragma once
#include <QByteArray>
#include <QtEndian>
#include <cstdint>

namespace rsvnc {
constexpr uint8_t MessageType = 230;   // unused by RFB, UltraVNC and LibVNC (250 is XVP)
constexpr int32_t PseudoEncoding = -0x52534401;
constexpr int HeaderSize = 20;                 // including the type byte
constexpr uint32_t MaxClipboard = 1024 * 1024; // UTF-8 bytes
constexpr uint32_t MaxChat = 4096;

enum Op : uint8_t {
    Hello = 1,      // S->C value=caps, payload=desktop label
    Chat = 2,       // both, payload=UTF-8 text
    Clipboard = 3   // both, payload=UTF-8 text
};
enum Caps : uint32_t { CapControl = 1, CapClipboard = 2, CapChat = 8, CapAll = CapControl | CapClipboard | CapChat };

struct Header { uint8_t op = 0; uint32_t id = 0; uint64_t value = 0; uint32_t length = 0; };

inline QByteArray encode(uint8_t op, uint32_t id, uint64_t value, const QByteArray& payload = {}) {
    QByteArray out(HeaderSize, '\0');
    auto* p = reinterpret_cast<uchar*>(out.data());
    p[0] = MessageType; p[1] = op;
    qToBigEndian<quint32>(id, p + 4);
    qToBigEndian<quint64>(value, p + 8);
    qToBigEndian<quint32>(quint32(payload.size()), p + 16);
    return out + payload;
}
// Decodes the 19 header bytes that follow the already consumed type byte.
inline Header decode(const char* rest) {
    const auto* p = reinterpret_cast<const uchar*>(rest);
    Header h; h.op = p[0];
    h.id = qFromBigEndian<quint32>(p + 3);
    h.value = qFromBigEndian<quint64>(p + 7);
    h.length = qFromBigEndian<quint32>(p + 15);
    return h;
}
// Largest payload accepted for each message; unknown messages carry none.
inline uint32_t limitFor(uint8_t op) {
    switch(op) {
    case Clipboard: return MaxClipboard;
    case Chat: case Hello: return MaxChat;
    default: return 0;
    }
}
}
