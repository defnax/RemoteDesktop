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
#include <memory>
#include <cstdlib>
#include <cstdint>

struct _rfbClient;

// LibVNC client running on its own thread. The GUI thread queues commands and
// reads frame copies; LibVNC objects never leave this thread.
class VncClient : public QThread
{
    Q_OBJECT
public:
    enum Quality { Balanced, BestQuality, LowBandwidth };
    VncClient(QString host, quint16 port, QByteArray password, QObject* parent = nullptr);
    ~VncClient() override;
    QImage frame();                       // latest complete frame (copy)
    QString desktopName() const;
    uint32_t caps() const { return caps_; }
    bool extended() const { return extended_; }
    int framesReceived() const { return frames_; }

    void pointer(int x, int y, int mask);
    void key(quint32 keysym, bool down);
    void sendClipboard(const QString& text);
    void sendChat(const QString& text);
    void setQuality(Quality quality);
    void setServerScale(int divisor);    // 1 = full size; RetroShare hosts only
    void refresh();
    void stop();

    // Internal, called on the client thread by LibVNC callbacks.
    bool handleExtension(_rfbClient* client);
    void frameChanged(_rfbClient* client);
    bool allocateFramebuffer(_rfbClient* client);
    void legacyClipboard(const char* text, int length);
    void legacyChat(int value, const char* text);
    QByteArray takePassword();
signals:
    void connected();
    void resized(QSize size);
    void frameReady();
    void failed(QString reason);
    void hello(uint32_t caps);
    void clipboardReceived(QString text);
    void chatReceived(QString text);
protected:
    void run() override;
private:
    struct Command {
        enum Kind { Pointer, Key, Clipboard, Chat, Quality, Scale, Refresh } kind;
        explicit Command(Kind k) : kind(k) {}
        int a = 0, b = 0, c = 0;
        quint32 sym = 0;
        QByteArray data;
    };
    void enqueue(Command c);
    bool execute(_rfbClient* client, const Command& c);

    QString host_;
    quint16 port_;
    QByteArray password_;
    mutable QMutex mutex_;
    QImage frame_;
    QString name_;
    std::deque<Command> commands_;
    std::atomic<bool> stopping_{false};
    std::atomic<uint32_t> caps_{0};
    std::atomic<bool> extended_{false};
    std::atomic<bool> frameQueued_{false};
    std::atomic<int> frames_{0};
    // Client-thread state
    std::unique_ptr<uint8_t, void(*)(void*)> buffer_{nullptr, &std::free};
    bool chatOpen_ = false;
    int lastWidth_ = 0, lastHeight_ = 0;
};
