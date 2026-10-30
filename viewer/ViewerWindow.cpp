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

#include "ViewerWindow.h"
#include "DesktopView.h"
#include "ChatWindow.h"
#include "SystemKeys.h"
#include "RsVncProtocol.h"
#include <rfb/keysym.h>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QStyle>
#include <QScreen>

ViewerWindow::ViewerWindow(const ViewerOptions& o) : options_(o) {
    setWindowTitle((o.title.isEmpty() ? o.host : o.title) + " — Remote Desktop");
    view_ = new DesktopView;
    scroll_ = new QScrollArea(this);
    scroll_->setWidget(view_);
    scroll_->setWidgetResizable(true);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet("QScrollArea{background:#181b1f}");
    setCentralWidget(scroll_);
    status_ = new QLabel("Connecting…", this);
    statusBar()->addWidget(status_, 1);

    client_ = new VncClient(o.host, o.port, o.password, this);
    options_.password.fill('\0'); options_.password.clear();
    connect(client_, &VncClient::connected, this, &ViewerWindow::connected);
    connect(client_, &VncClient::hello, this, &ViewerWindow::hello);
    connect(client_, &VncClient::frameReady, this, &ViewerWindow::frameReady);
    connect(client_, &VncClient::failed, this, &ViewerWindow::failed);
    connect(client_, &VncClient::resized, this, [this](QSize s) { remoteSize_ = s; updateStatus(); });
    connect(client_, &VncClient::chatReceived, this, [this](QString text) {
        openChat(); chat_->addMessage(options_.title.isEmpty() ? "Remote" : options_.title, text); QApplication::alert(chat_);
    });
    connect(client_, &VncClient::clipboardReceived, this, [this](QString text) {
        if(!clipboard_->isChecked()) return;
        lastRemoteClipboard_ = text; QApplication::clipboard()->setText(text);
    });
    connect(view_, &DesktopView::pointer, client_, &VncClient::pointer, Qt::DirectConnection);
    connect(view_, &DesktopView::key, client_, &VncClient::key, Qt::DirectConnection);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &ViewerWindow::localClipboardChanged);

    buildActions();
    QSettings settings("RetroShare", "RemoteDesktop"); settings.beginGroup("viewer");
    restoreGeometry(settings.value("geometry").toByteArray());
    if(!settings.contains("geometry")) resize(1200, 760);
    connect(&statusTimer_, &QTimer::timeout, this, &ViewerWindow::updateStatus);
    statusTimer_.start(1000); fpsClock_.start();
    SystemKeys::install(this, view_);
    client_->start();
}
ViewerWindow::~ViewerWindow() {
    SystemKeys::uninstall();
    client_->stop(); client_->wait();
    if(chat_) chat_->close();
}

void ViewerWindow::buildActions() {
    auto* bar = addToolBar("Session");
    bar->setObjectName("session");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QStyle* st = style();
    QSettings settings("RetroShare", "RemoteDesktop"); settings.beginGroup("viewer");

    fullScreen_ = bar->addAction(QIcon(":/remotedesktop/images/screen-full.png"), "Full screen");
    fullScreen_->setCheckable(true);
    fullScreen_->setShortcut(QKeySequence("Ctrl+Alt+Shift+F"));
    fullScreen_->setToolTip("Full screen (Ctrl+Alt+Shift+F)");
    connect(fullScreen_, &QAction::triggered, this, &ViewerWindow::toggleFullScreen);

    fit_ = bar->addAction(st->standardIcon(QStyle::SP_DesktopIcon), "Fit to window");
    fit_->setCheckable(true); fit_->setChecked(settings.value("fit", true).toBool());
    fit_->setShortcut(QKeySequence("Ctrl+Alt+Shift+S"));
    fit_->setToolTip("Scale the remote screen to the window, or show it at 100% with scroll bars (Ctrl+Alt+Shift+S)");
    connect(fit_, &QAction::toggled, this, [this](bool on) {
        scroll_->setWidgetResizable(on); view_->setFit(on);
        QSettings("RetroShare", "RemoteDesktop").setValue("viewer/fit", on);
    });
    scroll_->setWidgetResizable(fit_->isChecked()); view_->setFit(fit_->isChecked());

    auto* scaleButton = new QToolButton(bar);
    scaleButton->setText("Server scale");
    scaleButton->setToolTip("Let the sharing computer shrink the picture to save bandwidth");
    scaleButton->setPopupMode(QToolButton::InstantPopup);
    auto* scaleMenu = new QMenu(scaleButton);
    auto* scaleGroup = new QActionGroup(this);
    for(int d : {1, 2, 3, 4}) {
        QAction* a = scaleMenu->addAction(d == 1 ? "100%" : QString("1/%1").arg(d));
        a->setCheckable(true); a->setChecked(d == 1); scaleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, d] { client_->setServerScale(d); });
        scaleActions_ << a;
    }
    scaleButton->setMenu(scaleMenu);
    bar->addWidget(scaleButton);
    scaleButton->setEnabled(false);
    connect(client_, &VncClient::hello, scaleButton, [scaleButton] { scaleButton->setEnabled(true); });

    auto* qualityButton = new QToolButton(bar);
    qualityButton->setText("Quality");
    qualityButton->setPopupMode(QToolButton::InstantPopup);
    auto* qualityMenu = new QMenu(qualityButton);
    auto* qualityGroup = new QActionGroup(this);
    const QList<QPair<QString, VncClient::Quality>> qualities = {
        {"Balanced (Tight + JPEG)", VncClient::Balanced}, {"Best quality (lossless)", VncClient::BestQuality},
        {"Low bandwidth", VncClient::LowBandwidth}};
    const int savedQuality = settings.value("quality", int(VncClient::Balanced)).toInt();
    for(const auto& q : qualities) {
        QAction* a = qualityMenu->addAction(q.first);
        a->setCheckable(true); a->setChecked(q.second == savedQuality); qualityGroup->addAction(a);
        const VncClient::Quality value = q.second;
        connect(a, &QAction::triggered, this, [this, value] {
            client_->setQuality(value); QSettings("RetroShare", "RemoteDesktop").setValue("viewer/quality", int(value));
        });
    }
    if(savedQuality != VncClient::Balanced) client_->setQuality(VncClient::Quality(savedQuality));
    qualityButton->setMenu(qualityMenu);
    bar->addWidget(qualityButton);
    bar->addSeparator();

    auto* cad = bar->addAction("Ctrl+Alt+Del");
    cad->setToolTip("Send Ctrl+Alt+Del. Windows only accepts it from a system service; on a normal "
                    "RetroShare host it reaches applications but cannot open the security screen.");
    connect(cad, &QAction::triggered, this, [this] { sendKeys({XK_Control_L, XK_Alt_L, XK_Delete}); });
    controlActions_ << cad;

    auto* keysButton = new QToolButton(bar);
    keysButton->setText("Keys");
    keysButton->setPopupMode(QToolButton::InstantPopup);
    auto* keysMenu = new QMenu(keysButton);
    const QList<QPair<QString, QList<quint32>>> combos = {
        {"Windows / Start key", {XK_Super_L}}, {"Ctrl+Esc", {XK_Control_L, XK_Escape}},
        {"Alt+Tab", {XK_Alt_L, XK_Tab}}, {"Alt+F4", {XK_Alt_L, XK_F4}},
        {"Ctrl+Shift+Esc (Task Manager)", {XK_Control_L, XK_Shift_L, XK_Escape}},
        {"Windows+L (lock)", {XK_Super_L, XK_l}}, {"Windows+D (desktop)", {XK_Super_L, XK_d}},
        {"Windows+E (explorer)", {XK_Super_L, XK_e}}, {"Windows+R (run)", {XK_Super_L, XK_r}},
        {"Print Screen", {XK_Print}}, {"F11", {XK_F11}}};
    for(const auto& combo : combos) {
        const QList<quint32> keys = combo.second;
        connect(keysMenu->addAction(combo.first), &QAction::triggered, this, [this, keys] { sendKeys(keys); });
    }
    keysMenu->addSeparator();
    systemKeys_ = keysMenu->addAction("Capture Windows keys (Win, Alt+Tab) while focused");
    systemKeys_->setCheckable(true);
    systemKeys_->setChecked(settings.value("systemKeys", true).toBool());
    systemKeys_->setEnabled(SystemKeys::supported());
    SystemKeys::setEnabled(systemKeys_->isChecked());
    connect(systemKeys_, &QAction::toggled, this, [](bool on) {
        SystemKeys::setEnabled(on); QSettings("RetroShare", "RemoteDesktop").setValue("viewer/systemKeys", on);
    });
    keysButton->setMenu(keysMenu);
    bar->addWidget(keysButton);
    controlActions_ << keysMenu->menuAction();

    auto* refresh = bar->addAction(QIcon(":/remotedesktop/images/refresh.png"), "Refresh");
    refresh->setShortcut(QKeySequence("Ctrl+Alt+Shift+R"));
    connect(refresh, &QAction::triggered, client_, &VncClient::refresh);

    viewOnly_ = bar->addAction("View only");
    viewOnly_->setCheckable(true);
    viewOnly_->setToolTip("Do not send mouse or keyboard input");
    connect(viewOnly_, &QAction::toggled, this, &ViewerWindow::updateControl);

    clipboard_ = bar->addAction("Clipboard");
    clipboard_->setCheckable(true);
    clipboard_->setChecked(settings.value("clipboard", true).toBool());
    clipboard_->setToolTip("Synchronize text clipboard with the remote computer (if the host allows it)");
    connect(clipboard_, &QAction::toggled, this, [](bool on) { QSettings("RetroShare", "RemoteDesktop").setValue("viewer/clipboard", on); });
    bar->addSeparator();

    chatAction_ = bar->addAction(QIcon(":/remotedesktop/images/chat.png"), "Chat");
    connect(chatAction_, &QAction::triggered, this, &ViewerWindow::openChat);
    sendFile_ = bar->addAction(QIcon(":/remotedesktop/images/file-send.png"), "Send file…");
    sendFile_->setToolTip("Send files with RetroShare's file transfer: your friend gets a download link in RetroShare chat");
    connect(sendFile_, &QAction::triggered, this, &ViewerWindow::sendFiles);
    auto* moreButton = new QToolButton(bar);
    moreButton->setText("More");
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(moreButton);
    connect(moreMenu->addAction("Save screenshot…"), &QAction::triggered, this, &ViewerWindow::saveScreenshot);
    connect(moreMenu->addAction(st->standardIcon(QStyle::SP_MessageBoxInformation), "Connection info"), &QAction::triggered, this, &ViewerWindow::showInfo);
    moreButton->setMenu(moreMenu);
    bar->addWidget(moreButton);
    bar->addSeparator();
    // Disconnect first so it is never pushed into the toolbar overflow on small windows.
    auto* disconnect = new QAction(st->standardIcon(QStyle::SP_DialogCloseButton), "Disconnect", this);
    connect(disconnect, &QAction::triggered, this, &ViewerWindow::close);
    bar->insertAction(bar->actions().first(), disconnect);
    bar->insertSeparator(bar->actions().at(1));

    for(QAction* a : {fit_, fullScreen_, refresh}) addAction(a);   // shortcuts also work in full screen
    updateControl();
}

void ViewerWindow::connected() {
    connected_ = true;
    const QString name = client_->desktopName();
    if(options_.title.isEmpty() && !name.isEmpty()) setWindowTitle(name + " — Remote Desktop");
    view_->setFocus();
    updateControl(); updateStatus();
}
void ViewerWindow::hello(uint32_t caps) {
    hostAllowsControl_ = caps & rsvnc::CapControl;
    chatAction_->setEnabled(caps & rsvnc::CapChat);
    clipboard_->setEnabled(caps & rsvnc::CapClipboard);
    if(!hostAllowsControl_) viewOnly_->setChecked(true);
    viewOnly_->setEnabled(hostAllowsControl_);
    updateControl(); updateStatus();
}
void ViewerWindow::updateControl() {
    const bool control = connected_ && hostAllowsControl_ && !viewOnly_->isChecked();
    view_->setControl(control);
    for(QAction* a : controlActions_) a->setEnabled(control);
    updateStatus();
}
void ViewerWindow::frameReady() {
    const QImage frame = client_->frame();
    if(frame.isNull()) return;
    view_->setFrame(frame);
    ++fpsFrames_;
}
void ViewerWindow::updateStatus() {
    const qint64 ms = fpsClock_.restart();
    if(ms > 0) fps_ = fpsFrames_ * 1000.0 / ms;
    fpsFrames_ = 0;
    if(!connected_) return;
    QStringList parts;
    parts << QString("%1×%2").arg(remoteSize_.width()).arg(remoteSize_.height());
    parts << QString("%1 fps").arg(fps_, 0, 'f', 1);
    parts << (view_->control() ? "control" : "view only");
    if(client_->extended()) {
        const uint32_t caps = client_->caps();
        if(caps & rsvnc::CapClipboard) parts << (clipboard_->isChecked() ? "clipboard on" : "clipboard off");
        parts << "RetroShare encrypted link";
    }
    status_->setText(parts.join("  ·  "));
}
void ViewerWindow::sendKeys(const QList<quint32>& keys) {
    if(!view_->control()) return;
    for(quint32 k : keys) client_->key(k, true);
    for(auto it = keys.crbegin(); it != keys.crend(); ++it) client_->key(*it, false);
    view_->setFocus();
}
void ViewerWindow::toggleFullScreen() {
    if(isFullScreen()) {
        showNormal(); fullScreen_->setChecked(false); statusBar()->show();
        fullScreen_->setIcon(QIcon(":/remotedesktop/images/screen-full.png"));
        fullScreen_->setText("Full screen");
    } else {
        showFullScreen(); fullScreen_->setChecked(true); statusBar()->hide();
        fullScreen_->setIcon(QIcon(":/remotedesktop/images/screen-normal.png"));
        fullScreen_->setText("Normal screen");
    }
    view_->setFocus();
}
void ViewerWindow::openChat() {
    if(client_->extended() && !(client_->caps() & rsvnc::CapChat)) return;
    if(!chat_) {
        chat_ = new ChatWindow(options_.title.isEmpty() ? "Remote" : options_.title);
        chat_->setAttribute(Qt::WA_DeleteOnClose);
        connect(chat_, &ChatWindow::send, client_, &VncClient::sendChat, Qt::DirectConnection);
    }
    chat_->show(); chat_->raise(); chat_->activateWindow();
}
void ViewerWindow::sendFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Send files with RetroShare");
    if(files.isEmpty()) return;
    emit sendFilesRequested(files);
    statusBar()->showMessage(QString("%1 file(s) handed to RetroShare; download links appear in your private chat.").arg(files.size()), 8000);
}
void ViewerWindow::saveScreenshot() {
    const QImage frame = client_->frame();
    if(frame.isNull()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString name = QFileDialog::getSaveFileName(this, "Save screenshot",
        QDir(dir).filePath("remote-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png"), "PNG image (*.png)");
    if(!name.isEmpty() && !frame.save(name, "PNG")) QMessageBox::warning(this, "Screenshot", "Could not save the screenshot.");
}
void ViewerWindow::showInfo() {
    QStringList lines;
    lines << "Desktop: " + client_->desktopName();
    lines << QString("Resolution: %1×%2").arg(remoteSize_.width()).arg(remoteSize_.height());
    lines << QString("Frames received: %1").arg(client_->framesReceived());
    if(client_->extended()) {
        const uint32_t caps = client_->caps();
        lines << "Host: RetroShare Remote Desktop";
        lines << QString("Allowed by host: %1%2%3")
                 .arg((caps & rsvnc::CapControl) ? "control" : "view only")
                 .arg((caps & rsvnc::CapClipboard) ? ", clipboard" : "")
                 .arg((caps & rsvnc::CapChat) ? ", chat" : "");
        lines << "Transport: RetroShare friend connection (TLS). The VNC endpoints listen on 127.0.0.1 only.";
    } else {
        lines << QString("Server: %1:%2 (standard VNC, not encrypted by this viewer)").arg(options_.host).arg(options_.port);
    }
    QMessageBox::information(this, "Connection info", lines.join("\n"));
}
void ViewerWindow::localClipboardChanged() {
    if(!connected_ || !clipboard_->isChecked() || !view_->control()) return;
    if(client_->extended() && !(client_->caps() & rsvnc::CapClipboard)) return;
    const QString text = QApplication::clipboard()->text();
    if(text.isEmpty() || text == lastRemoteClipboard_) return;
    client_->sendClipboard(text);
}
void ViewerWindow::failed(const QString& reason) {
    if(closing_) return;
    connected_ = false;
    view_->setControl(false);
    status_->setText(reason);
    emit connectionLost(reason);   // the plugin ends the session and closes this window
}
void ViewerWindow::endSession() {
    if(closing_) return;
    closing_ = true;
    close();
}
void ViewerWindow::changeEvent(QEvent* e) {
    if(e->type() == QEvent::ActivationChange && !isActiveWindow()) view_->releaseAll();
    QMainWindow::changeEvent(e);
}
void ViewerWindow::closeEvent(QCloseEvent* e) {
    if(!closing_) { closing_ = true; emit closedByUser(); }
    if(!isFullScreen()) QSettings("RetroShare", "RemoteDesktop").setValue("viewer/geometry", saveGeometry());
    view_->releaseAll();
    client_->stop();
    if(chat_) chat_->close();
    QMainWindow::closeEvent(e);
}
