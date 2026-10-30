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

#include "HostDialogs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QMouseEvent>


AcceptDialog::AcceptDialog(const QString& name, const QString& peerId, int timeout, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowStaysOnTopHint), remaining_(timeout) {
    setWindowTitle("Remote desktop request");
    setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(this);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setText(name + " wants to see your screen.\nPeer ID: " + peerId);
    QFont bold = title->font(); bold.setBold(true); title->setFont(bold);
    layout->addWidget(title);

    auto* form = new QFormLayout;
    screen_ = new QComboBox(this);
    const auto screens = QGuiApplication::screens();
    for(int i = 0; i < screens.size(); ++i) {
        QScreen* s = screens[i];
        QString label = QString("%1: %2 (%3×%4)").arg(i + 1).arg(s->name()).arg(s->size().width()).arg(s->size().height());
        if(s == QGuiApplication::primaryScreen()) label += " — primary";
        screen_->addItem(label, i);
        if(s == QGuiApplication::primaryScreen()) screen_->setCurrentIndex(i);
    }
    form->addRow("Share screen:", screen_);
    layout->addLayout(form);

    control_ = new QCheckBox("Allow keyboard and mouse control", this);
    clipboard_ = new QCheckBox("Share clipboard (text)", this);
    chat_ = new QCheckBox("Allow chat", this);
    QSettings s("RetroShare", "RemoteDesktop");
    clipboard_->setChecked(s.value("clipboard", false).toBool());
    chat_->setChecked(s.value("chat", true).toBool());
    for(auto* box : {control_, clipboard_, chat_}) layout->addWidget(box);
    auto* files = new QLabel("Files are sent with RetroShare's own file transfer (\"Send file…\" during the session).", this);
    files->setWordWrap(true);
    layout->addWidget(files);
    auto* warning = new QLabel("Control gives your friend full use of this computer, as if they were sitting at it. "
                               "Only accept people you trust.", this);
    warning->setWordWrap(true);
    warning->setStyleSheet("color:#b35c00");
    layout->addWidget(warning);
    countdown_ = new QLabel(this);
    layout->addWidget(countdown_);
    auto* buttons = new QHBoxLayout;
    auto* accept = new QPushButton("Accept", this);
    auto* decline = new QPushButton("Decline", this);
    decline->setDefault(true);
    buttons->addStretch(); buttons->addWidget(decline); buttons->addWidget(accept);
    layout->addLayout(buttons);
    connect(accept, &QPushButton::clicked, this, [this] {
        QSettings st("RetroShare", "RemoteDesktop");
        st.setValue("clipboard", clipboard_->isChecked()); st.setValue("chat", chat_->isChecked());
        answered_ = true; emit shareAccepted(options()); close();
    });
    connect(decline, &QPushButton::clicked, this, [this] { answered_ = true; emit shareDeclined(); close(); });
    connect(this, &QDialog::finished, this, [this] { if(!answered_) { answered_ = true; emit shareDeclined(); } });
    connect(&timer_, &QTimer::timeout, this, &AcceptDialog::tick);
    timer_.start(1000); tick();
}
void AcceptDialog::tick() {
    if(remaining_ <= 0) { if(!answered_) { answered_ = true; emit shareDeclined(); } close(); return; }
    countdown_->setText(QString("Declines automatically in %1 s").arg(remaining_--));
}
ShareOptions AcceptDialog::options() const {
    ShareOptions o;
    o.control = control_->isChecked();
    o.clipboard = clipboard_->isChecked();
    o.chat = chat_->isChecked();
    o.screen = screen_->currentData().toInt();
    return o;
}

SharingBanner::SharingBanner(const QString& text, const QRect& screen)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet("SharingBanner{background:#b3261e;border-radius:6px} QLabel{color:white;font-weight:bold}"
                  "QPushButton{background:white;color:#b3261e;border:none;border-radius:4px;padding:3px 10px}");
    setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 6, 4);
    label_ = new QLabel(this);
    label_->setTextFormat(Qt::PlainText);
    auto* chat = new QPushButton("Chat", this);
    auto* file = new QPushButton("Send file…", this);
    auto* stop = new QPushButton("Stop sharing", this);
    layout->addWidget(label_); layout->addWidget(chat); layout->addWidget(file); layout->addWidget(stop);
    connect(file, &QPushButton::clicked, this, &SharingBanner::sendFileRequested);
    connect(stop, &QPushButton::clicked, this, &SharingBanner::stopRequested);
    connect(chat, &QPushButton::clicked, this, &SharingBanner::chatRequested);
    setText(text);
    adjustSize();
    move(screen.x() + (screen.width() - width()) / 2, screen.y());
}
void SharingBanner::setText(const QString& text) { label_->setText("● " + text); adjustSize(); }
void SharingBanner::mousePressEvent(QMouseEvent* e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    drag_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
#else
    drag_ = e->globalPos() - frameGeometry().topLeft();
#endif
}
void SharingBanner::mouseMoveEvent(QMouseEvent* e) {
    if(!(e->buttons() & Qt::LeftButton)) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    move(e->globalPosition().toPoint() - drag_);
#else
    move(e->globalPos() - drag_);
#endif
}
