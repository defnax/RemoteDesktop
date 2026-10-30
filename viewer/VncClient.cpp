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

#include "VncClient.h"
#include "RsVncProtocol.h"
#include <rfb/rfbclient.h>
#include <QMutexLocker>
#include <cstring>
#include <mutex>

namespace {
constexpr int MaxDimension = 8192;
constexpr qint64 MaxPixels = 32 * 1024 * 1024;
bool validSize(int w, int h) { return w > 0 && h > 0 && w <= MaxDimension && h <= MaxDimension && qint64(w)*h <= MaxPixels; }
int clientTag;
VncClient* self(rfbClient* c) { return static_cast<VncClient*>(rfbClientGetClientData(c, &clientTag)); }

int extensionEncodings[] = { rsvnc::PseudoEncoding, 0 };
rfbBool extensionMessage(rfbClient* c, rfbServerToClientMsg* msg) {
    if(msg->type != rsvnc::MessageType) return FALSE;
    VncClient* client = self(c);
    return client && client->handleExtension(c) ? TRUE : FALSE;
}
rfbClientProtocolExtension clientExtension = {
    extensionEncodings, nullptr, extensionMessage, nullptr, nullptr, nullptr
};
std::once_flag registerOnce;
}

VncClient::VncClient(QString host, quint16 port, QByteArray password, QObject* parent)
    : QThread(parent), host_(std::move(host)), port_(port), password_(std::move(password)) {}
VncClient::~VncClient() { stop(); wait(); password_.fill('\0'); }
void VncClient::stop() { stopping_ = true; }
QImage VncClient::frame() { QMutexLocker lock(&mutex_); frameQueued_ = false; return frame_; }
QString VncClient::desktopName() const { QMutexLocker lock(&mutex_); return name_; }
QByteArray VncClient::takePassword() { QByteArray p = password_; password_.fill('\0'); password_.clear(); return p; }
void VncClient::enqueue(Command c) {
    QMutexLocker lock(&mutex_);
    // Coalesce pointer moves without button changes so a slow link cannot build a backlog.
    if(c.kind == Command::Pointer && !commands_.empty() && commands_.back().kind == Command::Pointer && commands_.back().c == c.c) {
        commands_.back() = c; return;
    }
    if(commands_.size() < 4096) commands_.push_back(std::move(c));
}
void VncClient::pointer(int x, int y, int mask) { Command c{Command::Pointer}; c.a = x; c.b = y; c.c = mask; enqueue(c); }
void VncClient::key(quint32 sym, bool down) { Command c{Command::Key}; c.sym = sym; c.a = down; enqueue(c); }
void VncClient::sendClipboard(const QString& text) {
    Command c{Command::Clipboard}; c.data = text.toUtf8();
    if(c.data.size() > int(rsvnc::MaxClipboard)) return;
    enqueue(c);
}
void VncClient::sendChat(const QString& text) { Command c{Command::Chat}; c.data = text.toUtf8().left(int(rsvnc::MaxChat)); enqueue(c); }
void VncClient::setQuality(Quality q) { Command c{Command::Quality}; c.a = q; enqueue(c); }
void VncClient::setServerScale(int d) { Command c{Command::Scale}; c.a = qBound(1, d, 4); enqueue(c); }
void VncClient::refresh() { enqueue(Command{Command::Refresh}); }

bool VncClient::allocateFramebuffer(rfbClient* c) {
    if(!validSize(c->width, c->height)) return false;
    buffer_.reset(static_cast<uint8_t*>(calloc(size_t(c->width) * size_t(c->height), 4)));
    c->frameBuffer = buffer_.get();
    if(!c->frameBuffer) return false;
    if(c->width != lastWidth_ || c->height != lastHeight_) {
        lastWidth_ = c->width; lastHeight_ = c->height;
        emit resized(QSize(c->width, c->height));
    }
    return true;
}
void VncClient::frameChanged(rfbClient* c) {
    if(!c->frameBuffer) return;
    QImage copy = QImage(c->frameBuffer, c->width, c->height, c->width * 4, QImage::Format_RGB32).copy();
    { QMutexLocker lock(&mutex_); frame_ = std::move(copy); }
    ++frames_;
    if(!frameQueued_.exchange(true)) emit frameReady();
}
void VncClient::legacyClipboard(const char* text, int length) {
    // Standard cut text from third-party servers (Latin-1). RetroShare hosts use the UTF-8 extension.
    if(extended_ || !text || length <= 0 || length > int(rsvnc::MaxClipboard)) return;
    emit clipboardReceived(QString::fromLatin1(text, length));
}
void VncClient::legacyChat(int value, const char* text) {
    if(text && value > 0 && value < int(rsvnc::MaxChat)) emit chatReceived(QString::fromLocal8Bit(text, value));
}
bool VncClient::handleExtension(rfbClient* c) {
    char rest[rsvnc::HeaderSize - 1];
    if(!ReadFromRFBServer(c, rest, sizeof(rest))) return false;
    const rsvnc::Header h = rsvnc::decode(rest);
    if(h.length > rsvnc::limitFor(h.op)) { rfbClientLog("RetroShare extension: oversized message\n"); return false; }
    QByteArray payload(int(h.length), '\0');
    if(h.length && !ReadFromRFBServer(c, payload.data(), h.length)) return false;
    switch(h.op) {
    case rsvnc::Hello:
        caps_ = uint32_t(h.value) & rsvnc::CapAll; extended_ = true;
        emit hello(caps_);
        break;
    case rsvnc::Chat: emit chatReceived(QString::fromUtf8(payload)); break;
    case rsvnc::Clipboard: emit clipboardReceived(QString::fromUtf8(payload)); break;
    default: break;
    }
    return true;
}

bool VncClient::execute(rfbClient* c, const Command& cmd) {
    switch(cmd.kind) {
    case Command::Pointer: return SendPointerEvent(c, cmd.a, cmd.b, cmd.c);
    case Command::Key: return SendKeyEvent(c, cmd.sym, cmd.a ? TRUE : FALSE);
    case Command::Clipboard:
        if(extended_) {
            if(!(caps_ & rsvnc::CapClipboard)) return true;
            const QByteArray m = rsvnc::encode(rsvnc::Clipboard, 0, 0, cmd.data);
            return WriteToRFBServer(c, m.constData(), unsigned(m.size()));
        } else {
            QByteArray latin1 = QString::fromUtf8(cmd.data).toLatin1();
            return SendClientCutText(c, latin1.data(), latin1.size());
        }
    case Command::Chat:
        if(extended_) {
            if(!(caps_ & rsvnc::CapChat)) return true;
            const QByteArray m = rsvnc::encode(rsvnc::Chat, 0, 0, cmd.data);
            return WriteToRFBServer(c, m.constData(), unsigned(m.size()));
        }
        if(!SupportsClient2Server(c, rfbTextChat)) return true;
        if(!chatOpen_) { chatOpen_ = true; if(!TextChatOpen(c)) return false; }
        { QByteArray text = cmd.data; text.append('\0'); return TextChatSend(c, text.data()); }
    case Command::Quality:
        switch(cmd.a) {
        case BestQuality:
            c->appData.encodingsString = "zrle tight ultra copyrect hextile zlib corre rre raw";
            c->appData.enableJPEG = FALSE; c->appData.compressLevel = 6; c->appData.qualityLevel = 9; break;
        case LowBandwidth:
            c->appData.encodingsString = "tight zrle ultra copyrect hextile zlib corre rre raw";
            c->appData.enableJPEG = TRUE; c->appData.compressLevel = 9; c->appData.qualityLevel = 2; break;
        default:
            c->appData.encodingsString = "tight zrle ultra copyrect hextile zlib corre rre raw";
            c->appData.enableJPEG = TRUE; c->appData.compressLevel = 6; c->appData.qualityLevel = 6; break;
        }
        return SetFormatAndEncodings(c) && SendFramebufferUpdateRequest(c, 0, 0, c->width, c->height, FALSE);
    case Command::Scale: {
        if(!extended_) return true;   // only RetroShare hosts are known to support server-side scaling
        rfbSetScaleMsg m; m.type = rfbSetScale; m.scale = uint8_t(cmd.a); m.pad = 0;
        return WriteToRFBServer(c, reinterpret_cast<char*>(&m), sz_rfbSetScaleMsg);
    }
    case Command::Refresh: return SendFramebufferUpdateRequest(c, 0, 0, c->width, c->height, FALSE);
    }
    return true;
}

void VncClient::run() {
    std::call_once(registerOnce, [] { rfbClientRegisterExtension(&clientExtension); });
    rfbClient* c = rfbGetClient(8, 3, 4);
    if(!c) { emit failed("Cannot create VNC client."); return; }
    rfbClientSetClientData(c, &clientTag, this);
    c->serverHost = strdup(host_.toUtf8().constData()); c->serverPort = port_;
    c->connectTimeout = 15; c->readTimeout = 120;
    c->format.redShift = 16; c->format.greenShift = 8; c->format.blueShift = 0;
    c->format.redMax = c->format.greenMax = c->format.blueMax = 255;
    c->format.trueColour = TRUE;
    c->appData.encodingsString = "tight zrle ultra copyrect hextile zlib corre rre raw";
    c->appData.compressLevel = 6; c->appData.qualityLevel = 6; c->appData.enableJPEG = TRUE;
    c->appData.useRemoteCursor = FALSE;
    c->canHandleNewFBSize = TRUE;
    c->GetPassword = [](rfbClient* c) -> char* {
        QByteArray p = self(c)->takePassword();
        char* copy = strdup(p.constData()); p.fill('\0'); return copy;
    };
    c->MallocFrameBuffer = [](rfbClient* c) -> rfbBool { return self(c)->allocateFramebuffer(c) ? TRUE : FALSE; };
    c->GotFrameBufferUpdate = [](rfbClient*, int, int, int, int) {};
    c->FinishedFrameBufferUpdate = [](rfbClient* c) { self(c)->frameChanged(c); };
    c->GotXCutText = [](rfbClient* c, const char* text, int len) { self(c)->legacyClipboard(text, len); };
    c->HandleTextChat = [](rfbClient* c, int value, char* text) { self(c)->legacyChat(value, text); };
    // On failure LibVNC frees c itself; buffer_ stays ours.
    if(!rfbInitClient(c, nullptr, nullptr)) {
        password_.fill('\0');
        if(!stopping_) emit failed("Could not connect: wrong password, or the connection was refused or timed out.");
        return;
    }
    password_.fill('\0');
    { QMutexLocker lock(&mutex_); name_ = QString::fromUtf8(c->desktopName ? c->desktopName : ""); }
    emit connected();
    bool ok = true;
    while(!stopping_ && ok) {
        std::deque<Command> pending;
        { QMutexLocker lock(&mutex_); pending.swap(commands_); }
        for(const auto& cmd : pending) if(!(ok = execute(c, cmd))) break;
        if(!ok) break;
        int ready = WaitForMessage(c, 10000);
        if(ready < 0 || (ready > 0 && !HandleRFBServerMessage(c))) ok = false;
    }
    c->frameBuffer = nullptr;
    rfbClientCleanup(c);
    if(!stopping_) emit failed("The remote desktop connection was closed.");
}
