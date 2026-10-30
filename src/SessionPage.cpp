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

#include "SessionPage.h"
#include "VncWorker.h"
#include "ChatWindow.h"
#include "RsVncProtocol.h"
#include "ViewerWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QGuiApplication>
#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QCursor>
#include <QPainter>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>
#include <retroshare/rsfiles.h>
#include <retroshare/rschats.h>

namespace {
constexpr int RequestTimeoutSeconds = 55;
QString capsText(uint32_t caps) {
    QStringList parts;
    parts << ((caps & rsvnc::CapControl) ? "control" : "view only");
    if(caps & rsvnc::CapClipboard) parts << "clipboard";
    if(caps & rsvnc::CapChat) parts << "chat";
    return parts.join(", ");
}
// Same format as RetroShare's own file links (RetroShareLink::createFile).
QString fileLinkHtml(const QString& name, uint64_t size, const QString& hash) {
    QUrl url; url.setScheme("retroshare"); url.setHost("file");
    QUrlQuery query;
    query.addQueryItem("name", QString::fromLatin1(QUrl::toPercentEncoding(name)));
    query.addQueryItem("size", QString::number(size));
    query.addQueryItem("hash", hash);
    url.setQuery(query);
    const QString href = url.toString(QUrl::EncodeSpaces | QUrl::EncodeUnicode);
    const QString human = size >= (1u<<20) ? QString::number(size/1048576.0,'f',1)+" MB"
                        : size >= 1024 ? QString::number(size/1024.0,'f',1)+" KB" : QString::number(size)+" B";
    return QString("<a href=\"%1\">%2</a> (%3)").arg(href, name.toHtmlEscaped(), human);
}
void drawCursor(QImage& image, QScreen* screen) {
    const QRect g = screen->geometry();
    const QPoint local = QCursor::pos(screen) - g.topLeft();
    if(!QRect(QPoint(0,0), g.size()).contains(local) || g.width() <= 0 || g.height() <= 0) return;
    const qreal sx = image.width() / qreal(g.width()), sy = image.height() / qreal(g.height());
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(local.x() * sx, local.y() * sy);
    p.scale(qMax<qreal>(1, sx), qMax<qreal>(1, sy));
    const QPointF arrow[] = {{0,0},{0,16},{4,12.5},{7,19},{9.5,18},{6.5,11.5},{11.5,11.5}};
    p.setPen(QPen(Qt::black, 1)); p.setBrush(Qt::white);
    p.drawPolygon(arrow, 7);
}
}

SessionPage::SessionPage(DesktopService* s,RsPeers* p,RsFiles* f,RsChats* c):service_(s),peers_(p),files_(f),chats_(c) {
    setPageName("Remote Desktop");
    QSettings settings("RetroShare","RemoteDesktop");
    auto* layout=new QVBoxLayout(this);

    auto* viewBox=new QGroupBox("View a friend's desktop",this);
    auto* viewLayout=new QVBoxLayout(viewBox);
    auto* row=new QHBoxLayout;
    friends_=new QComboBox(viewBox); request_=new QPushButton("Request desktop",viewBox);
    row->addWidget(friends_,1); row->addWidget(request_); viewLayout->addLayout(row);
    auto* hint=new QLabel("The remote desktop opens in its own window.",viewBox);
    viewLayout->addWidget(hint);
    layout->addWidget(viewBox);

    auto* shareBox=new QGroupBox("Share this desktop",this);
    auto* shareLayout=new QVBoxLayout(shareBox);
    allowRequests_=new QCheckBox("Allow incoming desktop requests (each one asks you first)",shareBox);
    allowRequests_->setChecked(settings.value("allowRequests",false).toBool());
    shareLayout->addWidget(allowRequests_);
    layout->addWidget(shareBox);

    status_=new QLabel("Disconnected",this); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText);
    QFont bold=status_->font(); bold.setBold(true); status_->setFont(bold);
    layout->addWidget(status_);
    stop_=new QPushButton("Stop session",this);
    sendFile_=new QPushButton("Send file…",this);
    sendFile_->setToolTip("Send files to your friend with RetroShare's file transfer (a file link in private chat)");
    auto* actions=new QHBoxLayout; actions->addWidget(stop_); actions->addWidget(sendFile_); actions->addStretch(); layout->addLayout(actions);
    log_=new QPlainTextEdit(this); log_->setReadOnly(true); log_->setMaximumBlockCount(500);
    log_->setPlaceholderText("Session log");
    layout->addWidget(log_,1);

    connect(request_,&QPushButton::clicked,this,&SessionPage::request);
    connect(stop_,&QPushButton::clicked,this,[this]{stop("Disconnected locally");});
    connect(sendFile_,&QPushButton::clicked,this,&SessionPage::chooseFiles);
    connect(&fileTimer_,&QTimer::timeout,this,&SessionPage::pollFiles);
    connect(allowRequests_,&QCheckBox::toggled,this,[this](bool enabled){
        QSettings("RetroShare","RemoteDesktop").setValue("allowRequests",enabled);
        if(!enabled && state_==Incoming) stop("Request declined");
    });
    connect(&listener_,&QTcpServer::newConnection,this,[this]{
        // One connection only: the listener closes after the viewer connects.
        if(state_==Viewing && !socket_) { attach(listener_.nextPendingConnection()); listener_.close(); }
    });
    connect(QGuiApplication::clipboard(),&QClipboard::dataChanged,this,&SessionPage::clipboardChanged);
    connect(&timer_,&QTimer::timeout,this,&SessionPage::process); timer_.start(20);
    connect(&captureTimer_,&QTimer::timeout,this,&SessionPage::capture);
    refresh_.start(); refreshPeers(); buttons();
}
SessionPage::~SessionPage() { shutdown(); }
void SessionPage::shutdown() { timer_.stop(); stop("Stopped"); }
void SessionPage::buttons() {
    request_->setEnabled(state_==Idle && friends_->count()>0); friends_->setEnabled(state_==Idle);
    stop_->setEnabled(state_!=Idle);
    sendFile_->setEnabled((state_==Hosting || state_==Viewing) && files_ && chats_);
}
void SessionPage::log(const QString& text) {
    log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ")+text);
}
void SessionPage::refreshPeers() {
    if(state_!=Idle || !peers_) return;
    QString selected=friends_->currentData().toString(); friends_->clear();
    std::list<RsPeerId> ids; peers_->getOnlineList(ids);
    for(const auto& id:ids) if(peers_->isFriend(id)) {
        RsPeerDetails details; peers_->getPeerDetails(id,details);
        friends_->addItem(QString::fromStdString(details.name)+" ("+QString::fromStdString(id.toStdString()).left(8)+")",QString::fromStdString(id.toStdString()));
    }
    int index=friends_->findData(selected); if(index>=0) friends_->setCurrentIndex(index);
    buttons();
}
void SessionPage::send(Message m,uint64_t v,const std::string& p) { if(!session_.empty()) service_->transmit(peer_,session_,m,v,p); }
void SessionPage::request() {
    if(state_!=Idle || friends_->currentIndex()<0) return;
    peer_=RsPeerId(friends_->currentData().toString().toStdString());
    if(!peers_->isFriend(peer_)||!peers_->isOnline(peer_)) { refreshPeers(); return; }
    RsPeerDetails details; peers_->getPeerDetails(peer_,details); peerName_=QString::fromStdString(details.name);
    session_=QUuid::createUuid().toString(QUuid::Id128).toStdString(); state_=Outgoing;
    activity_.restart(); ping_.restart(); send(Message::Request);
    status_->setText("Waiting for "+peerName_+" to accept…"); log("Requested the desktop of "+peerName_); buttons();
}
QImage SessionPage::grab() const {
    if(!screen_) return {};
    QImage image=screen_->grabWindow(0).toImage().convertToFormat(QImage::Format_RGB32);
    if(!image.isNull()) drawCursor(image,screen_);
    return image;
}
void SessionPage::accept(const ShareOptions& options) {
    if(state_!=Incoming) return;
    QString reason=NativeInput::unavailableReason();
    // X11 capture is required on Linux even for view-only sessions.
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if(!reason.isEmpty()) { stop(reason); return; }
#else
    if(options.control && !reason.isEmpty()) { stop(reason); return; }
#endif
    const auto screens=QGuiApplication::screens();
    if(options.screen<0 || options.screen>=screens.size()) { stop("The selected screen is no longer available."); return; }
    screen_=screens[options.screen];
    QImage image=grab();
    if(image.isNull()) { stop("Screen capture failed. Check OS screen recording permissions."); return; }
    geometry_=screen_->geometry(); captureSize_=image.size();
    input_.setTarget(geometry_,screen_->name(),screen_->devicePixelRatio(),captureSize_);
    caps_=(options.control?rsvnc::CapControl:0)|(options.clipboard?rsvnc::CapClipboard:0)|(options.chat?rsvnc::CapChat:0);
    QByteArray password;
    static constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for(int i=0;i<8;++i) password.append(chars[QRandomGenerator::system()->bounded(62)]);
    state_=StartingHost; activity_.restart(); ping_.restart();
    const std::string token=session_;
    worker_=new VncWorker(password,caps_,this); worker_->setFrame(image);
    connect(worker_,&VncWorker::failed,this,[this,token](QString e){if(session_==token) stop(e);});
    connect(worker_,&VncWorker::listening,this,[this,password,token](quint16 port){
        if(state_!=StartingHost || session_!=token) return;
        auto* socket=new QTcpSocket(this); attach(socket);
        connect(socket,&QTcpSocket::connected,this,[this,password]{
            if(state_!=StartingHost) return;
            state_=Hosting; send(Message::Accept,caps_,password.toStdString());
            const QString text="Sharing your screen with "+peerName_+" ("+capsText(caps_)+")";
            status_->setText(text); log(text);
            banner_=new SharingBanner("Sharing screen with "+peerName_+((caps_&rsvnc::CapControl)?" — control allowed":" — view only"),geometry_);
            connect(banner_,&SharingBanner::stopRequested,this,[this]{stop("Sharing stopped");});
            connect(banner_,&SharingBanner::chatRequested,this,&SessionPage::openChat);
            connect(banner_,&SharingBanner::sendFileRequested,this,&SessionPage::chooseFiles);
            banner_->show();
            captureTimer_.start(100); buttons();
        });
        socket->connectToHost(QHostAddress::LocalHost,port);
    });
    connect(worker_,&VncWorker::remotePointer,this,[this,token](int x,int y,int mask){
        if(session_==token && state_==Hosting && (caps_&rsvnc::CapControl)) input_.pointer(x,y,mask&127);
    });
    connect(worker_,&VncWorker::remoteKey,this,[this,token](quint32 k,bool down){
        if(session_==token && state_==Hosting && (caps_&rsvnc::CapControl)) input_.key(k,down);
    });
    connect(worker_,&VncWorker::remoteClipboard,this,[this,token](QString text){
        if(session_!=token || state_!=Hosting || !(caps_&rsvnc::CapClipboard) || !(caps_&rsvnc::CapControl)) return;
        lastRemoteClipboard_=text; QGuiApplication::clipboard()->setText(text);
    });
    connect(worker_,&VncWorker::chatReceived,this,[this,token](QString text){
        if(session_!=token || state_!=Hosting) return;
        openChat(); chat_->addMessage(peerName_,text); QApplication::alert(chat_);
    });
    worker_->start(); status_->setText("Starting desktop sharing…"); log("Accepted: "+capsText(caps_)); buttons();
}
void SessionPage::openChat() {
    if(!(caps_&rsvnc::CapChat) || !worker_) return;
    if(!chat_) {
        chat_=new ChatWindow(peerName_);
        chat_->setAttribute(Qt::WA_DeleteOnClose);
        connect(chat_,&ChatWindow::send,this,[this](QString text){ if(worker_ && state_==Hosting) worker_->sendChat(text); });
    }
    chat_->show(); chat_->raise();
}
void SessionPage::clipboardChanged() {
    if(state_!=Hosting || !worker_ || !(caps_&rsvnc::CapClipboard)) return;
    const QString text=QGuiApplication::clipboard()->text();
    if(text.isEmpty() || text==lastRemoteClipboard_) return;
    worker_->sendClipboard(text);
}
void SessionPage::startViewer(const QByteArray& password) {
    if(!listener_.listen(QHostAddress::LocalHost,0)) { stop("Cannot create local VNC bridge."); return; }
    const std::string token=session_;
    ViewerOptions options;
    options.host="127.0.0.1"; options.port=listener_.serverPort(); options.password=password; options.title=peerName_;
    viewer_=new ViewerWindow(options);   // separate top-level window, not part of this page
    viewer_->setAttribute(Qt::WA_DeleteOnClose);
    connect(viewer_,&ViewerWindow::closedByUser,this,[this,token]{ if(session_==token) stop("The viewer window was closed"); });
    connect(viewer_,&ViewerWindow::connectionLost,this,[this,token](QString reason){ if(session_==token) stop(reason); });
    connect(viewer_,&ViewerWindow::sendFilesRequested,this,[this,token](QStringList files){
        if(session_!=token) return;
        for(const QString& file:files) sendFile(file);
    });
    viewer_->show(); viewer_->raise(); viewer_->activateWindow();
    const QString text="Connected to "+peerName_+" ("+capsText(caps_)+") — the remote desktop is open in its own window";
    status_->setText(text); log(text); buttons();
}
void SessionPage::attach(QTcpSocket* socket) {
    socket_=socket; socket->setParent(this); socket->setReadBufferSize(DesktopWindow);
    connect(socket,&QTcpSocket::readyRead,this,&SessionPage::pump);
    connect(socket,&QTcpSocket::bytesWritten,this,[this](qint64 count){
        if(state_==Hosting || state_==Viewing) { delivered_+=uint64_t(count); send(Message::Ack,delivered_); }
    });
    connect(socket,&QTcpSocket::disconnected,this,[this]{stop("VNC connection ended");});
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket,&QTcpSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){stop("Local VNC connection failed");});
#else
    connect(socket,QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),this,[this](QAbstractSocket::SocketError){stop("Local VNC connection failed");});
#endif
    if(state_==Viewing) { transportReady_=true; send(Message::Ping,1); }
}
void SessionPage::pump() {
    if(!socket_ || !transportReady_ || (state_!=Hosting && state_!=Viewing)) return;
    while(socket_->bytesAvailable()>0 && sent_-acked_<DesktopWindow) {
        qint64 limit=qMin<uint64_t>(DesktopChunk,DesktopWindow-(sent_-acked_));
        QByteArray bytes=socket_->read(limit); if(bytes.isEmpty()) break;
        send(Message::Data,sent_,bytes.toStdString()); sent_+=uint64_t(bytes.size());
    }
}
void SessionPage::receive(const DesktopItem& item) {
    if(!peers_ || !peers_->isFriend(item.PeerId()) || !peers_->isOnline(item.PeerId())) return;
    Message kind=Message(item.kind);
    if(kind==Message::Request) {
        if(state_!=Idle || !allowRequests_->isChecked() || !item.payload.empty() || item.value!=0) {
            service_->transmit(item.PeerId(),item.session,Message::Stop); return;
        }
        peer_=item.PeerId(); session_=item.session; state_=Incoming;
        activity_.restart(); ping_.restart();
        RsPeerDetails details; peers_->getPeerDetails(peer_,details); peerName_=QString::fromStdString(details.name);
        status_->setText(peerName_+" requests your desktop — answer in the pop-up window.");
        log("Desktop request from "+peerName_+" ("+QString::fromStdString(peer_.toStdString())+")");
        dialog_=new AcceptDialog(peerName_,QString::fromStdString(peer_.toStdString()),RequestTimeoutSeconds);
        const std::string token=session_;
        connect(dialog_,&AcceptDialog::shareAccepted,this,[this,token](ShareOptions o){ if(session_==token) accept(o); });
        connect(dialog_,&AcceptDialog::shareDeclined,this,[this,token]{ if(session_==token && state_==Incoming) stop("Request declined"); });
        dialog_->show(); dialog_->raise(); dialog_->activateWindow();
        QApplication::alert(dialog_);
        buttons(); return;
    }
    if(state_==Idle || item.PeerId()!=peer_ || item.session!=session_) return;
    if(kind==Message::Stop) { stop("Session ended or declined by "+peerName_,false); return; }
    if(kind==Message::Accept && state_==Outgoing) {
        if(item.payload.size()!=8 || (item.value&~uint64_t(rsvnc::CapAll))) { stop("Invalid session acceptance"); return; }
        state_=Viewing; caps_=uint32_t(item.value); activity_.restart(); startViewer(QByteArray::fromStdString(item.payload)); return;
    }
    if(state_!=Hosting && state_!=Viewing) return;
    if(kind==Message::Data) {
        if(!socket_ || item.value!=received_ || item.payload.empty() || socket_->bytesToWrite()+qint64(item.payload.size())>qint64(DesktopWindow)) {
            stop("Remote desktop stream lost synchronization or exceeded its buffer limit"); return;
        }
        qint64 written=socket_->write(item.payload.data(),qint64(item.payload.size()));
        if(written!=qint64(item.payload.size())) { stop("Local VNC write failed"); return; }
        received_+=item.payload.size();
    } else if(kind==Message::Ack) {
        if(item.value<acked_ || item.value>sent_ || !item.payload.empty()) { stop("Invalid stream acknowledgement"); return; }
        acked_=item.value; pump();
    } else if(kind==Message::Ping && item.value==1 && state_==Hosting) { transportReady_=true; pump(); }
    else if(kind!=Message::Ping) return;
    activity_.restart();
}
void SessionPage::process() {
    for(int count=0;count<128;++count) { auto i=service_->take(); if(!i) break; receive(*i); }
    if(refresh_.elapsed()>3000) { refresh_.restart(); refreshPeers(); }
    if(state_==Idle) return;
    if(!peers_ || !peers_->isOnline(peer_) || !peers_->isFriend(peer_)) { stop("Friend disconnected"); return; }
    const bool active=state_==Hosting || state_==Viewing;
    if(activity_.elapsed()>(active?30000:60000)) { stop("Session timed out"); return; }
    if(active && ping_.elapsed()>5000) { ping_.restart(); send(Message::Ping); }
    pump();
}
void SessionPage::capture() {
    if(state_!=Hosting || !worker_) return;
    if(!screen_ || screen_->geometry()!=geometry_) { stop("Screen layout changed; reconnect to share again"); return; }
    QImage image=grab();
    if(image.isNull() || image.size()!=captureSize_) { stop("Screen capture stopped or resolution changed"); return; }
    worker_->setFrame(image);
}
void SessionPage::stop(QString reason,bool notify) {
    const bool wasActive=state_!=Idle;
    if(notify && state_!=Idle) send(Message::Stop);
    state_=Idle; captureTimer_.stop(); listener_.close();
    input_.releaseAll();
    if(dialog_) { dialog_->disconnect(this); dialog_->close(); }
    if(banner_) { banner_->deleteLater(); banner_=nullptr; }
    if(chat_) { chat_->disconnect(this); chat_->addNotice("Session ended"); chat_.clear(); }
    if(socket_) { socket_->disconnect(this); socket_->abort(); socket_->deleteLater(); socket_=nullptr; }
    if(worker_) { worker_->disconnect(this); worker_->stop(); worker_->wait(); delete worker_; worker_=nullptr; }
    if(viewer_) { ViewerWindow* window=viewer_; viewer_=nullptr; window->disconnect(this); window->endSession(); }
    sent_=acked_=received_=delivered_=0; transportReady_=false; session_.clear(); peer_=RsPeerId();
    caps_=0; lastRemoteClipboard_.clear(); screen_=nullptr;
    if(wasActive) log(reason);
    status_->setText(reason); buttons();
}

void SessionPage::chooseFiles() {
    if(state_!=Hosting && state_!=Viewing) return;
    const QStringList files=QFileDialog::getOpenFileNames(this,"Send files to "+peerName_);
    for(const QString& file:files) sendFile(file);
}
void SessionPage::sendFile(const QString& path) {
    if((state_!=Hosting && state_!=Viewing) || !files_ || !chats_) return;
    QFileInfo info(path);
    if(!info.isFile() || !info.isReadable()) { log("Cannot send "+path+": not a readable file"); return; }
    const QString absolute=info.absoluteFilePath();
    for(const auto& p:pendingFiles_) if(p.path==absolute && p.peer==peer_) return;
    // Same mechanism as attaching a file in RetroShare chat: hash it into the
    // extra-file list for 30 days, then post a RetroShare file link.
    if(!files_->ExtraFileHash(absolute.toUtf8().constData(),30*24*3600,TransferRequestFlags(0u))) {
        log("RetroShare could not share "+info.fileName()); return;
    }
    PendingFile pending{absolute,peer_,peerName_,{}}; pending.started.start();
    pendingFiles_.push_back(pending);
    log("Preparing "+info.fileName()+" for RetroShare transfer…");
    if(!fileTimer_.isActive()) fileTimer_.start(500);
}
void SessionPage::pollFiles() {
    for(auto it=pendingFiles_.begin();it!=pendingFiles_.end();) {
        FileInfo info;
        if(files_->ExtraFileStatus(it->path.toUtf8().constData(),info)) {
            const QString name=QFileInfo(QString::fromUtf8(info.fname.c_str())).fileName();
            const QString html=fileLinkHtml(name,info.size,QString::fromStdString(info.hash.toStdString()));
            if(chats_->sendChat(ChatId(it->peer),html.toUtf8().constData()))
                log("Sent "+name+" to "+it->peerName+" — it appears as a download link in your RetroShare chat");
            else log("Could not post the file link for "+name+" to "+it->peerName);
            it=pendingFiles_.erase(it);
        } else if(it->started.elapsed()>15*60*1000) {
            log("Gave up preparing "+QFileInfo(it->path).fileName()); it=pendingFiles_.erase(it);
        } else ++it;
    }
    if(pendingFiles_.empty()) fileTimer_.stop();
}
