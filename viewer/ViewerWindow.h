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
#include <QMainWindow>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include "VncClient.h"
class DesktopView;
class QScrollArea;
class QLabel;
class QAction;
class ChatWindow;

struct ViewerOptions {
    QString host = "127.0.0.1";
    quint16 port = 5900;
    QByteArray password;
    QString title;          // friend name
};

class ViewerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ViewerWindow(const ViewerOptions& options);
    ~ViewerWindow() override;
    DesktopView* view() const { return view_; }
    // Called by the plugin when the session ends: closes without emitting closedByUser().
    void endSession();
signals:
    void closedByUser();
    void connectionLost(QString reason);
    void sendFilesRequested(QStringList files);
protected:
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;
private:
    void buildActions();
    void connected();
    void hello(uint32_t caps);
    void frameReady();
    void failed(const QString& reason);
    void updateStatus();
    void updateControl();
    void toggleFullScreen();
    void sendKeys(const QList<quint32>& keys);
    void openChat();
    void sendFiles();
    void saveScreenshot();
    void showInfo();
    void localClipboardChanged();
    ViewerOptions options_;
    VncClient* client_;
    DesktopView* view_;
    QScrollArea* scroll_;
    QLabel* status_;
    QPointer<ChatWindow> chat_;
    QAction *fullScreen_, *fit_, *viewOnly_, *clipboard_, *chatAction_, *sendFile_, *systemKeys_;
    QList<QAction*> controlActions_, scaleActions_;
    QTimer statusTimer_;
    QElapsedTimer fpsClock_;
    int fpsFrames_ = 0;
    double fps_ = 0;
    bool connected_ = false;
    bool closing_ = false;
    bool hostAllowsControl_ = true;
    QString lastRemoteClipboard_;
    QSize remoteSize_;
};
