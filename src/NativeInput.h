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
#include <QRect>
#include <QSet>
#include <QString>
#include <QSize>
class NativeInput {
public:
    ~NativeInput();
    static QString unavailableReason();
    // Target screen: logical geometry, platform device name and captured image size.
    // Pointer coordinates are in captured-image pixels.
    void setTarget(const QRect& geometry, const QString& deviceName, qreal devicePixelRatio, const QSize& imageSize);
    void pointer(int x, int y, int mask);
    // Directory containing this plugin module, used to find the bundled viewer.
    static QString moduleDirectory();
    void key(quint32 sym, bool down);
    void releaseAll();
private:
    QRect geometry_;
    QString device_;
    qreal ratio_ = 1;
    QSize image_;
    QSet<quint32> keys_;
    int buttons_ = 0;
    int lastX_ = 0, lastY_ = 0;
};
