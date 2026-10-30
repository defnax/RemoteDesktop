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

#include "Service.h"
#include <serialiser/rstypeserializer.h>
namespace {
class Serializer : public RsServiceSerializer {
public:
    Serializer():RsServiceSerializer(DesktopServiceId) {}
    RsItem* create_item(uint16_t service,uint8_t subtype) const override {
        return service==DesktopServiceId && subtype==1 ? new DesktopItem : nullptr;
    }
};
}
void DesktopItem::serial_process(RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& c) {
    RsTypeSerializer::serial_process(j,c,kind,"kind");
    RsTypeSerializer::serial_process(j,c,session,"session");
    RsTypeSerializer::serial_process(j,c,value,"value");
    RsTypeSerializer::serial_process(j,c,payload,"payload");
}
DesktopService::DesktopService() { addSerialType(new Serializer); }
bool DesktopService::recvItem(RsItem* raw) {
    std::unique_ptr<RsItem> owner(raw);
    auto* i=dynamic_cast<DesktopItem*>(raw);
    if(!i || i->session.size()!=32 || i->payload.size()>DesktopChunk || i->kind<1 || i->kind>6) return false;
    for(char c:i->session) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Budget& budget=budgets_[i->PeerId()];
    if(items_>=DesktopTotalItems || budget.items>=DesktopPeerItems || budget.bytes+i->payload.size()>2*DesktopWindow) {
        if(!budget.items) budgets_.erase(i->PeerId());
        return false;
    }
    ++budget.items; budget.bytes+=i->payload.size(); ++items_;
    owner.release(); queue_.emplace_back(i); return true;
}
std::unique_ptr<DesktopItem> DesktopService::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(queue_.empty()) return {};
    auto i=std::move(queue_.front()); queue_.pop_front(); --items_;
    auto budget=budgets_.find(i->PeerId());
    if(budget!=budgets_.end()) {
        --budget->second.items; budget->second.bytes-=i->payload.size();
        if(!budget->second.items) budgets_.erase(budget);
    }
    return i;
}
void DesktopService::transmit(const RsPeerId& peer,const std::string& session,Message kind,uint64_t value,const std::string& payload) {
    if(payload.size()>DesktopChunk) return;
    auto* i=new DesktopItem; i->PeerId(peer); i->session=session; i->kind=uint8_t(kind); i->value=value; i->payload=payload;
    sendItem(i);
}
