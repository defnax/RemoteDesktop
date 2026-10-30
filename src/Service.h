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
#include <services/p3service.h>
#include <serialiser/rsserializer.h>
#include <retroshare/rsservicecontrol.h>
#include <mutex>
#include <deque>
#include <memory>
#include <map>
#include <retroshare/rsids.h>
// Experimental ID: reserve an upstream ID before distributing alongside other plugins.
constexpr uint16_t DesktopServiceId = 0xa174;
constexpr size_t DesktopChunk = 16384;
constexpr uint64_t DesktopWindow = 256 * 1024;
constexpr size_t DesktopPeerItems = 512;
constexpr size_t DesktopTotalItems = 2048;
enum class Message : uint8_t { Request=1, Accept, Data, Ack, Stop, Ping };
class DesktopItem : public RsItem {
public:
    DesktopItem():RsItem(RS_PKT_VERSION_SERVICE,DesktopServiceId,1) { setPriorityLevel(3); }
    void clear() override { payload.clear(); }
    void serial_process(RsGenericSerializer::SerializeJob,RsGenericSerializer::SerializeContext&) override;
    uint8_t kind=0;
    std::string session;
    uint64_t value=0;
    std::string payload;
};
class DesktopService : public p3Service {
public:
    DesktopService();
    RsServiceInfo getServiceInfo() override { return RsServiceInfo(DesktopServiceId,"RemoteDesktop",2,0,2,0); }
    bool recvItem(RsItem* item) override;
    std::unique_ptr<DesktopItem> take();
    void transmit(const RsPeerId&, const std::string&, Message, uint64_t=0, const std::string& = {});
private:
    std::mutex mutex_;
    std::deque<std::unique_ptr<DesktopItem>> queue_;
    struct Budget { size_t items=0, bytes=0; };
    std::map<RsPeerId,Budget> budgets_;   // per friend, so one friend cannot starve another
    size_t items_=0;
};
