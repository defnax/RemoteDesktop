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

#include <retroshare/rsplugin.h>
#include <QApplication>
#include <QStyle>
#include <QPointer>
#include <QThread>
#include <QTabWidget>
#include <QTabBar>
#include <QSettings>
#include "Service.h"
#include "SessionPage.h"
class RemoteDesktopPlugin : public RsPlugin {
public:
    ~RemoteDesktopPlugin() override { stop(); delete service_; }
    void setInterfaces(RsPlugInInterfaces& interfaces) override { peers_=interfaces.mPeers; files_=interfaces.mFiles; chats_=interfaces.mChats; }
    void setPlugInHandler(RsPluginHandler*) override {}
    std::string getPluginName() const override { return "Remote Desktop"; }
    std::string getShortPluginDescription() const override { return "Share a desktop with a RetroShare friend and view theirs in a pop-up window"; }
    void getPluginVersion(int& major,int& minor,int& build,int& revision) const override { major=0; minor=2; build=0; revision=0; }
    uint16_t rs_service_id() const override { return DesktopServiceId; }
    p3Service* p3_service() const override { if(!service_) service_=new DesktopService; return service_; }
    // RetroShare has no plugin hook for Network tabs. FriendsDialog (the Network page)
    // is created before MainWindow asks plugins for qt_page(), so we insert our page
    // into its tab widget here and return nullptr (no sidebar entry). If the tab
    // widget cannot be found, or the user disabled it, the normal sidebar page is used.
    MainPage* qt_page() const override {
        p3_service();
        if(!page_) {
            page_=new SessionPage(service_,peers_,files_,chats_);
            const bool asTab=QSettings("RetroShare","RemoteDesktop").value("showInNetworkTab",true).toBool();
            if(asTab && attachToNetworkTab(page_)) return nullptr;
        }
        return inNetworkTab_ ? nullptr : page_.data();
    }
    QIcon* qt_icon() const override { if(icon_.isNull()) icon_=QApplication::style()->standardIcon(QStyle::SP_ComputerIcon); return &icon_; }
    void stop() override {
        if(!page_) return;
        auto cleanup=[this]{ if(page_) { page_->shutdown(); delete page_; page_=nullptr; } };
        if(QThread::currentThread()==page_->thread()) cleanup();
        else QMetaObject::invokeMethod(page_,cleanup,Qt::BlockingQueuedConnection);
    }
private:
    bool attachToNetworkTab(QWidget* page) const {
        for(QWidget* top : QApplication::topLevelWidgets()) {
            const auto dialogs=top->findChildren<QWidget*>();
            for(QWidget* dialog : dialogs) {
                if(!dialog->inherits("FriendsDialog")) continue;
                for(QTabWidget* tabs : dialog->findChildren<QTabWidget*>("tabWidget")) {
                    if(!tabs->inherits("ChatTabWidget")) continue;
                    const int index=tabs->addTab(page,*qt_icon(),"Remote Desktop");
                    // The Network tab widget has closable tabs; hide our close button
                    // the same way RSTabWidget::hideCloseButton() does.
                    const auto side=QTabBar::ButtonPosition(tabs->tabBar()->style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition,nullptr,tabs->tabBar()));
                    if(QWidget* button=tabs->tabBar()->tabButton(index,side)) button->hide();
                    inNetworkTab_=true;
                    return true;
                }
            }
        }
        return false;
    }
    mutable bool inNetworkTab_=false;
    RsPeers* peers_=nullptr;
    RsFiles* files_=nullptr;
    RsChats* chats_=nullptr;
    mutable DesktopService* service_=nullptr;
    mutable QPointer<SessionPage> page_;
    mutable QIcon icon_;
};
#ifdef _WIN32
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
extern "C" {
PLUGIN_EXPORT void* RETROSHARE_PLUGIN_provide() { return new RemoteDesktopPlugin; }
PLUGIN_EXPORT uint32_t RETROSHARE_PLUGIN_revision = 0;
PLUGIN_EXPORT uint32_t RETROSHARE_PLUGIN_api = RS_PLUGIN_API_VERSION;
}
