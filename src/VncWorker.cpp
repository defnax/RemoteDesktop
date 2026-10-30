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

#include "VncWorker.h"
#include "RsVncProtocol.h"
#include <rfb/rfb.h>
#include <QMutexLocker>
#include <cstring>
#include <memory>
#include <mutex>
#ifndef Q_OS_WIN
#include <arpa/inet.h>
#endif

struct HostExtension { VncWorker* worker = nullptr; };

namespace {
constexpr int MaxDimension = 8192;
constexpr qint64 MaxPixels = 32 * 1024 * 1024;
constexpr int MaxAuthFailures = 3;
constexpr int Tile = 64;
bool validSize(int w, int h) { return w > 0 && h > 0 && w <= MaxDimension && h <= MaxDimension && qint64(w)*h <= MaxPixels; }
VncWorker* worker(rfbClientPtr c) { return static_cast<VncWorker*>(c->screen->screenData); }

int pseudoEncodings[] = { rsvnc::PseudoEncoding, 0 };
rfbBool extEnable(rfbClientPtr c, void** data, int encoding) {
    if(encoding != rsvnc::PseudoEncoding) return FALSE;
    if(!*data) {
        auto* state = new HostExtension; state->worker = worker(c); *data = state;
        worker(c)->extensionReady(c, state);
    }
    return TRUE;
}
rfbBool extMessage(rfbClientPtr c, void* data, const rfbClientToServerMsg* msg) {
    if(msg->type != rsvnc::MessageType || !data) return FALSE;
    char rest[rsvnc::HeaderSize - 1];
    if(rfbReadExact(c, rest, sizeof(rest)) <= 0) { rfbCloseClient(c); return TRUE; }
    const rsvnc::Header h = rsvnc::decode(rest);
    if(h.length > rsvnc::limitFor(h.op)) { rfbLog("RetroShare extension: oversized message\n"); rfbCloseClient(c); return TRUE; }
    QByteArray payload(int(h.length), '\0');
    if(h.length && rfbReadExact(c, payload.data(), int(h.length)) <= 0) { rfbCloseClient(c); return TRUE; }
    auto* state = static_cast<HostExtension*>(data);
    state->worker->handleExtension(c, *state, h.op, h.id, h.value, payload);
    return TRUE;
}
void extClose(rfbClientPtr, void* data) {
    auto* state = static_cast<HostExtension*>(data);
    if(!state) return;
    state->worker->extensionClosed(state);
    delete state;
}
rfbProtocolExtension hostExtension = {
    nullptr, nullptr, pseudoEncodings, extEnable, extMessage, extClose, nullptr, nullptr, nullptr
};
std::once_flag registerOnce;

void markChanges(rfbScreenInfoPtr s, const QImage& before, const QImage& after) {
    const int w = after.width(), h = after.height();
    for(int ty = 0; ty < h; ty += Tile) {
        const int th = qMin(Tile, h - ty);
        for(int tx = 0; tx < w; tx += Tile) {
            const int tw = qMin(Tile, w - tx);
            bool changed = false;
            for(int y = ty; y < ty + th && !changed; ++y)
                changed = std::memcmp(before.constScanLine(y) + tx*4, after.constScanLine(y) + tx*4, size_t(tw)*4) != 0;
            if(changed) rfbMarkRectAsModified(s, tx, ty, tx + tw, ty + th);
        }
    }
}
}

VncWorker::VncWorker(QByteArray p, uint32_t caps, QObject* parent)
    : QThread(parent), password_(std::move(p)), caps_(caps & rsvnc::CapAll) {}
VncWorker::~VncWorker() { stop(); wait(); }
void VncWorker::stop() { stopping_ = true; }
void VncWorker::setFrame(const QImage& image) { QMutexLocker lock(&mutex_); frame_ = image; }
void VncWorker::sendChat(const QString& text) {
    QByteArray utf8 = text.toUtf8().left(int(rsvnc::MaxChat));
    QMutexLocker lock(&mutex_); if(commands_.size() < 64) commands_.push_back({true, utf8});
}
void VncWorker::sendClipboard(const QString& text) {
    QByteArray utf8 = text.toUtf8();
    if(utf8.size() > int(rsvnc::MaxClipboard)) return;
    QMutexLocker lock(&mutex_); if(commands_.size() < 64) commands_.push_back({false, utf8});
}
bool VncWorker::write(rfbClientPtr c, const QByteArray& message) {
    if(rfbWriteExact(c, message.constData(), message.size()) > 0) return true;
    rfbCloseClient(c); return false;
}
bool VncWorker::checkPassword(rfbClientPtr c, const char* response, int length) {
    if(authFailures_ >= MaxAuthFailures) return false;
    if(rfbCheckPasswordByList(c, response, length)) return true;
    ++authFailures_;
    return false;
}
bool VncWorker::acceptClient(rfbClientPtr c) {
    // Exactly one authenticated local bridge; never evict or share with a second client.
    if(client_) return false;
    client_ = c;
    c->viewOnly = (caps_ & rsvnc::CapControl) ? FALSE : TRUE;
    return true;
}
void VncWorker::clientGone(rfbClientPtr c) { if(client_ == c) client_ = nullptr; }
void VncWorker::extensionReady(rfbClientPtr c, HostExtension* state) {
    if(c != client_) return;
    extension_ = state;
    write(c, rsvnc::encode(rsvnc::Hello, 0, caps_, QByteArray("RetroShare")));
}
void VncWorker::extensionClosed(HostExtension* state) {
    if(extension_ == state) extension_ = nullptr;
}

void VncWorker::handleExtension(rfbClientPtr, HostExtension&, uint8_t op, uint32_t, uint64_t, const QByteArray& payload) {
    switch(op) {
    case rsvnc::Chat:
        if(caps_ & rsvnc::CapChat) emit chatReceived(QString::fromUtf8(payload));
        break;
    case rsvnc::Clipboard:
        // Writing the host clipboard is input: it needs both clipboard and control.
        if((caps_ & rsvnc::CapClipboard) && (caps_ & rsvnc::CapControl)) emit remoteClipboard(QString::fromUtf8(payload));
        break;
    default:
        break;   // unknown messages without payload are ignored for forward compatibility
    }
}

void VncWorker::run() {
    std::call_once(registerOnce, [] { rfbRegisterProtocolExtension(&hostExtension); });
    QImage current = [this]{ QMutexLocker lock(&mutex_); return frame_; }().convertToFormat(QImage::Format_RGB32);
    qint64 lastFrameKey = current.cacheKey();
    if(!validSize(current.width(), current.height())) { emit failed("Screen capture is unavailable or too large."); return; }
    auto* s = rfbGetScreen(nullptr, nullptr, current.width(), current.height(), 8, 3, 4);
    if(!s) { emit failed("Cannot create VNC server."); return; }
    s->screenData = this;
    s->desktopName = "RetroShare Remote Desktop";
    s->frameBuffer = reinterpret_cast<char*>(current.bits());
    s->serverFormat.redShift = 16; s->serverFormat.greenShift = 8; s->serverFormat.blueShift = 0;
    s->serverFormat.redMax = s->serverFormat.greenMax = s->serverFormat.blueMax = 255;
    s->serverFormat.trueColour = TRUE;
    s->listenInterface = htonl(INADDR_LOOPBACK);
    s->port = 5900; s->autoPort = TRUE; s->ipv6port = 0;   // ipv6port 0 disables the IPv6 listener
    s->httpPort = 0; s->httpDir = nullptr;
    // The bridge may be slow (RetroShare link); allow time before declaring the client dead.
    s->maxClientWait = 20000;
    s->permitFileTransfer = FALSE;      // files go through RetroShare, never through VNC
    s->cursor = nullptr;                // the host cursor is drawn into captured frames
    s->alwaysShared = FALSE; s->neverShared = TRUE;
    char* passwords[] = { password_.data(), nullptr };
    s->authPasswdData = passwords;
    s->passwordCheck = [](rfbClientPtr c, const char* response, int len) -> rfbBool {
        return worker(c)->checkPassword(c, response, len) ? TRUE : FALSE;
    };
    s->kbdAddEvent = [](rfbBool down, rfbKeySym sym, rfbClientPtr c) {
        emit worker(c)->remoteKey(sym, down);
    };
    s->ptrAddEvent = [](int mask, int x, int y, rfbClientPtr c) {
        emit worker(c)->remotePointer(x, y, mask);
    };
    s->setXCutText = [](char*, int, rfbClientPtr) {};   // clipboard only through the gated extension
    s->newClientHook = [](rfbClientPtr c) -> rfbNewClientAction {
        if(!worker(c)->acceptClient(c)) return RFB_CLIENT_REFUSE;
        c->clientGoneHook = [](rfbClientPtr gone) { worker(gone)->clientGone(gone); };
        return RFB_CLIENT_ACCEPT;
    };
    rfbInitServer(s);
    auto shutdown = [&] { rfbShutdownServer(s, TRUE); rfbScreenCleanup(s); };
    if(s->listenSock == RFB_INVALID_SOCKET) { shutdown(); emit failed("Cannot open loopback VNC endpoint."); return; }
    if(s->listen6Sock != RFB_INVALID_SOCKET) { shutdown(); emit failed("Refusing to share: VNC server opened an IPv6 listener."); return; }
    emit listening(quint16(s->port));
    while(!stopping_) {
        QImage next;
        std::deque<Command> commands;
        { QMutexLocker lock(&mutex_); next = frame_; commands.swap(commands_); }
        // A session uses a fixed framebuffer; the page stops on screen geometry changes.
        if(next.size() == current.size() && next.cacheKey() != lastFrameKey) {
            lastFrameKey = next.cacheKey();
            QImage previous = current;
            current = next.convertToFormat(QImage::Format_RGB32);
            s->frameBuffer = reinterpret_cast<char*>(current.bits());
            markChanges(s, previous, current);
        }
        if(client_ && extension_) {
            for(const auto& cmd : commands) {
                if(cmd.chat && !(caps_ & rsvnc::CapChat)) continue;
                if(!cmd.chat && !(caps_ & rsvnc::CapClipboard)) continue;
                if(!write(client_, rsvnc::encode(cmd.chat ? rsvnc::Chat : rsvnc::Clipboard, 0, 0, cmd.utf8))) break;
            }
        }
        if(authFailures_ >= MaxAuthFailures && !lockoutReported_) {
            lockoutReported_ = true;
            emit failed("Too many wrong VNC passwords on the local bridge. Session stopped for safety.");
        }
        rfbProcessEvents(s, 10000);
    }
    shutdown();
}
