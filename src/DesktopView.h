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
#include <QWidget>
#include <QImage>
#include <QHash>

// Remote screen canvas: draws frames scaled or 1:1 and turns mouse and keyboard
// input into RFB pointer masks and X11 keysyms.
class DesktopView : public QWidget {
    Q_OBJECT
public:
    explicit DesktopView(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    void setControl(bool enabled);
    bool control() const { return control_; }
    void setFit(bool fit);
    bool fit() const { return fit_; }
    QSize frameSize() const { return frame_.size(); }
    void releaseAll();
    // Used by the Windows system-key hook: returns true if the key was forwarded.
    bool forwardSystemKey(quint32 keysym, bool down);
    QSize sizeHint() const override;
signals:
    void pointer(int x, int y, int mask);
    void key(quint32 sym, bool down);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab goes to the remote desktop
    bool event(QEvent*) override;
private:
    QRect imageRect() const;
    void mouse(QMouseEvent*);
    QImage frame_;
    bool control_ = false;
    bool fit_ = true;
    QHash<int, quint32> keys_;      // Qt key/native scan -> keysym currently held
    QHash<quint32, int> systemKeys_;
    QPoint last_;
    int mask_ = 0;
};
