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
#include <retroshare-gui/mainpage.h>
#include <retroshare/rspeers.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QPointer>
#include "NativeInput.h"
#include "Service.h"
#include "HostDialogs.h"
#include <vector>
class RsFiles;
class RsChats;
class QLabel;
class QComboBox;
class QPushButton;
class QCheckBox;
class QPlainTextEdit;
class QScreen;
class VncWorker;
class ChatWindow;
class ViewerWindow;
class SessionPage : public MainPage {
    Q_OBJECT
public:
    SessionPage(DesktopService*,RsPeers*,RsFiles*,RsChats*);
    ~SessionPage() override;
    void shutdown();
private:
    enum State { Idle, Incoming, Outgoing, StartingHost, Hosting, Viewing } state_=Idle;
    void refreshPeers();
    void request();
    void accept(const ShareOptions& options);
    void process();
    void receive(const DesktopItem&);
    void send(Message,uint64_t=0,const std::string& = {});
    void stop(QString reason,bool notify=true);
    void attach(QTcpSocket*);
    void pump();
    void startViewer(const QByteArray& password);
    QImage grab() const;
    void capture();
    void buttons();
    void log(const QString& text);
    void openChat();
    void clipboardChanged();
    void chooseFiles();
    void sendFile(const QString& path);
    void pollFiles();
    DesktopService* service_;
    RsPeers* peers_;
    RsFiles* files_;
    RsChats* chats_;
    struct PendingFile { QString path; RsPeerId peer; QString peerName; QElapsedTimer started; };
    std::vector<PendingFile> pendingFiles_;
    QTimer fileTimer_;
    RsPeerId peer_;
    QString peerName_;
    std::string session_;
    uint32_t caps_=0;
    bool transportReady_=false;
    uint64_t sent_=0, acked_=0, received_=0, delivered_=0;
    QElapsedTimer activity_, ping_, refresh_;
    QTimer timer_, captureTimer_;
    QTcpServer listener_;
    QTcpSocket* socket_=nullptr;
    VncWorker* worker_=nullptr;
    QPointer<ViewerWindow> viewer_;
    NativeInput input_;
    QPointer<QScreen> screen_;
    QRect geometry_;
    QSize captureSize_;
    QString lastRemoteClipboard_;
    QPointer<AcceptDialog> dialog_;
    SharingBanner* banner_=nullptr;
    QPointer<ChatWindow> chat_;
    QLabel* status_;
    QComboBox* friends_;
    QCheckBox* allowRequests_;
    QPushButton *request_, *stop_, *sendFile_;
    QPlainTextEdit* log_;
};
