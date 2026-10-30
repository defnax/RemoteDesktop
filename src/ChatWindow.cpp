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

#include "ChatWindow.h"
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTime>

ChatWindow::ChatWindow(const QString& peer, QWidget* parent) : QWidget(parent, Qt::Window), peer_(peer) {
    setWindowTitle("Chat with " + peer);
    resize(420, 320);
    auto* layout = new QVBoxLayout(this);
    log_ = new QTextBrowser(this);
    log_->setOpenLinks(false);
    layout->addWidget(log_, 1);
    auto* row = new QHBoxLayout;
    input_ = new QLineEdit(this);
    input_->setMaxLength(2000);
    input_->setPlaceholderText("Type a message and press Enter");
    auto* button = new QPushButton("Send", this);
    row->addWidget(input_, 1); row->addWidget(button);
    layout->addLayout(row);
    auto submit = [this] {
        const QString text = input_->text().trimmed();
        if(text.isEmpty()) return;
        emit send(text);
        addMessage("Me", text);
        input_->clear();
    };
    connect(input_, &QLineEdit::returnPressed, this, submit);
    connect(button, &QPushButton::clicked, this, submit);
}
void ChatWindow::setPeerName(const QString& name) { peer_ = name; setWindowTitle("Chat with " + name); }
void ChatWindow::addMessage(const QString& from, const QString& text) {
    // Remote text is always escaped: chat content is untrusted.
    log_->append(QString("<span style='color:gray'>%1</span> <b>%2:</b> %3")
                 .arg(QTime::currentTime().toString("HH:mm"), from.toHtmlEscaped(),
                      text.left(4000).toHtmlEscaped().replace("\n", "<br>")));
}
void ChatWindow::addNotice(const QString& text) {
    log_->append(QString("<i style='color:gray'>%1</i>").arg(text.toHtmlEscaped()));
}
