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
#include <QDialog>
#include <QWidget>
#include <QTimer>
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

struct ShareOptions {
    bool control = false;
    bool clipboard = false;
    bool chat = true;
    int screen = 0;
};

// Pop-up shown for every incoming request. Nothing is shared until Accept.
class AcceptDialog : public QDialog {
    Q_OBJECT
public:
    AcceptDialog(const QString& friendName, const QString& peerId, int timeoutSeconds, QWidget* parent = nullptr);
    ShareOptions options() const;
signals:
    void shareAccepted(ShareOptions options);
    void shareDeclined();
private:
    void tick();
    QCheckBox *control_, *clipboard_, *chat_;
    QComboBox* screen_;
    QLabel* countdown_;
    QTimer timer_;
    int remaining_;
    bool answered_ = false;
};

// Always-on-top indicator while the desktop is shared, with a Stop button.
class SharingBanner : public QWidget {
    Q_OBJECT
public:
    SharingBanner(const QString& text, const QRect& screenGeometry);
    void setText(const QString& text);
signals:
    void stopRequested();
    void chatRequested();
    void sendFileRequested();
protected:
    void mousePressEvent(class QMouseEvent*) override;
    void mouseMoveEvent(class QMouseEvent*) override;
private:
    QLabel* label_;
    QPoint drag_;
};
