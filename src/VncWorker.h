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
#include <QThread>
#include <QImage>
#include <QMutex>
#include <deque>
#include <atomic>

struct _rfbClientRec;
struct HostExtension;

// Loopback VNC server for the sharing side. All LibVNC calls and framebuffer
// ownership stay on this worker thread; the GUI thread only exchanges copies.
class VncWorker : public QThread
{
    Q_OBJECT
public:
    // caps is a combination of rsvnc::Caps chosen by the host user.
    VncWorker(QByteArray password, uint32_t caps, QObject* parent = nullptr);
    ~VncWorker() override;
    void setFrame(const QImage& frame);
    void sendChat(const QString& text);
    void sendClipboard(const QString& text);
    void stop();
    uint32_t caps() const { return caps_; }

    // Internal: called from LibVNC callbacks on the worker thread.
    void handleExtension(_rfbClientRec* client, HostExtension& state, uint8_t op, uint32_t id, uint64_t value, const QByteArray& payload);
    bool checkPassword(_rfbClientRec* client, const char* response, int length);
    bool acceptClient(_rfbClientRec* client);
    void clientGone(_rfbClientRec* client);
    void extensionReady(_rfbClientRec* client, HostExtension* state);
    void extensionClosed(HostExtension* state);
signals:
    void listening(quint16 port);
    void failed(QString reason);
    void remotePointer(int x, int y, int mask);
    void remoteKey(quint32 keysym, bool down);
    void remoteClipboard(QString text);
    void chatReceived(QString text);
protected:
    void run() override;
private:
    struct Command { bool chat; QByteArray utf8; };
    bool write(_rfbClientRec* client, const QByteArray& message);
    QByteArray password_;
    const uint32_t caps_;
    mutable QMutex mutex_;
    QImage frame_;
    std::deque<Command> commands_;
    std::atomic<bool> stopping_{false};
    _rfbClientRec* client_ = nullptr;
    HostExtension* extension_ = nullptr;
    int authFailures_ = 0;
    bool lockoutReported_ = false;
};
