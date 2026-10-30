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
class QTextBrowser;
class QLineEdit;

// Small chat window used on both sides of a remote desktop session.
class ChatWindow : public QWidget {
    Q_OBJECT
public:
    explicit ChatWindow(const QString& peerName, QWidget* parent = nullptr);
    void addMessage(const QString& from, const QString& text);
    void addNotice(const QString& text);
    void setPeerName(const QString& name);
signals:
    void send(QString text);
private:
    QTextBrowser* log_;
    QLineEdit* input_;
    QString peer_;
};
