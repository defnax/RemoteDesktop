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

#include "NativeInput.h"
#include <QGuiApplication>
#include <QFileInfo>
#include <rfb/keysym.h>
#include <string>
#ifndef Q_OS_WIN
#include <dlfcn.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#else
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#endif

QString NativeInput::unavailableReason() {
#ifdef Q_OS_MACOS
    if(!AXIsProcessTrusted()) return "Enable Accessibility permission for RetroShare in macOS System Settings.";
#elif !defined(Q_OS_WIN)
    if(QGuiApplication::platformName() != "xcb") return "Desktop sharing requires an X11 session. Wayland capture and control are not implemented.";
#endif
    return {};
}
NativeInput::~NativeInput() { releaseAll(); }
void NativeInput::setTarget(const QRect& geometry,const QString& device,qreal ratio,const QSize& image) {
    geometry_=geometry; device_=device; ratio_=ratio>0?ratio:1; image_=image;
}
#ifdef Q_OS_WIN
namespace {
struct MonitorSearch { std::wstring name; RECT rect{}; bool found=false; };
BOOL CALLBACK findMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM data) {
    auto* search=reinterpret_cast<MonitorSearch*>(data);
    MONITORINFOEXW info{}; info.cbSize=sizeof(info);
    if(GetMonitorInfoW(monitor,&info) && search->name==info.szDevice) { search->rect=info.rcMonitor; search->found=true; return FALSE; }
    return TRUE;
}
}
#endif
QString NativeInput::moduleDirectory() {
#ifdef Q_OS_WIN
    HMODULE module=nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&NativeInput::moduleDirectory),&module)) {
        wchar_t buffer[4096]; DWORD n=GetModuleFileNameW(module,buffer,4096);
        if(n>0 && n<4096) return QFileInfo(QString::fromWCharArray(buffer,int(n))).absolutePath();
    }
#else
    Dl_info info{};
    if(dladdr(reinterpret_cast<void*>(&NativeInput::moduleDirectory),&info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
#endif
    return {};
}
void NativeInput::pointer(int x,int y,int mask) {
    if(geometry_.isEmpty() || image_.isEmpty()) return;
    x = qBound(0,x,image_.width()-1); y = qBound(0,y,image_.height()-1);
    lastX_ = x; lastY_ = y;
#ifdef Q_OS_WIN
    // Map through the monitor's real rectangle and the virtual desktop so that
    // DPI scaling and monitor layout do not shift the pointer.
    MonitorSearch search; search.name=device_.toStdWString();
    EnumDisplayMonitors(nullptr,nullptr,findMonitor,reinterpret_cast<LPARAM>(&search));
    RECT r=search.rect;
    if(!search.found) { r.left=geometry_.x(); r.top=geometry_.y(); r.right=geometry_.x()+geometry_.width(); r.bottom=geometry_.y()+geometry_.height(); }
    const double px=r.left+(x+0.5)*(r.right-r.left)/image_.width();
    const double py=r.top+(y+0.5)*(r.bottom-r.top)/image_.height();
    const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN), vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw=qMax(2,GetSystemMetrics(SM_CXVIRTUALSCREEN)), vh=qMax(2,GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT move{}; move.type=INPUT_MOUSE;
    move.mi.dx=LONG((px-vx)*65535.0/(vw-1)); move.mi.dy=LONG((py-vy)*65535.0/(vh-1));
    move.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    SendInput(1,&move,sizeof(move));
    const DWORD downs[] = {MOUSEEVENTF_LEFTDOWN,MOUSEEVENTF_MIDDLEDOWN,MOUSEEVENTF_RIGHTDOWN};
    const DWORD ups[] = {MOUSEEVENTF_LEFTUP,MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_RIGHTUP};
    for(int b=0;b<3;++b) if((buttons_^mask)&(1<<b)) {
        INPUT i{}; i.type=INPUT_MOUSE; i.mi.dwFlags=(mask&(1<<b))?downs[b]:ups[b]; SendInput(1,&i,sizeof(i));
    }
    if((mask&24) && !(buttons_&24)) {
        INPUT i{}; i.type=INPUT_MOUSE; i.mi.dwFlags=MOUSEEVENTF_WHEEL;
        i.mi.mouseData=(mask&8)?WHEEL_DELTA:DWORD(-WHEEL_DELTA); SendInput(1,&i,sizeof(i));
    }
    if((mask&96) && !(buttons_&96)) {
        INPUT i{}; i.type=INPUT_MOUSE; i.mi.dwFlags=MOUSEEVENTF_HWHEEL;
        i.mi.mouseData=(mask&64)?WHEEL_DELTA:DWORD(-WHEEL_DELTA); SendInput(1,&i,sizeof(i));
    }
#elif defined(Q_OS_MACOS)
    // Quartz uses logical points.
    const double gx=geometry_.x()+(x+0.5)*geometry_.width()/image_.width();
    const double gy=geometry_.y()+(y+0.5)*geometry_.height()/image_.height();
    CGPoint p=CGPointMake(gx,gy);
    CGEventType movement = (mask&1)?kCGEventLeftMouseDragged:(mask&4)?kCGEventRightMouseDragged:kCGEventMouseMoved;
    auto post=[&](CGEventType type,CGMouseButton button) {
        CGEventRef e=CGEventCreateMouseEvent(nullptr,type,p,button); if(e) { CGEventPost(kCGHIDEventTap,e); CFRelease(e); }
    };
    post(movement,(mask&4)?kCGMouseButtonRight:kCGMouseButtonLeft);
    const CGMouseButton bs[]={kCGMouseButtonLeft,kCGMouseButtonCenter,kCGMouseButtonRight};
    const CGEventType ds[]={kCGEventLeftMouseDown,kCGEventOtherMouseDown,kCGEventRightMouseDown};
    const CGEventType us[]={kCGEventLeftMouseUp,kCGEventOtherMouseUp,kCGEventRightMouseUp};
    for(int b=0;b<3;++b) if((buttons_^mask)&(1<<b)) post((mask&(1<<b))?ds[b]:us[b],bs[b]);
    if((mask&24) && !(buttons_&24)) {
        auto e=CGEventCreateScrollWheelEvent(nullptr,kCGScrollEventUnitLine,1,(mask&8)?1:-1);
        if(e) { CGEventPost(kCGHIDEventTap,e); CFRelease(e); }
    }
#else
    // XTest uses physical pixels; the captured image is the screen in physical pixels.
    const int gx=int(geometry_.x()*ratio_)+x, gy=int(geometry_.y()*ratio_)+y;
    if(Display* d=XOpenDisplay(nullptr)) {
        XTestFakeMotionEvent(d,-1,gx,gy,CurrentTime);
        for(int b=0;b<7;++b) if((buttons_^mask)&(1<<b)) XTestFakeButtonEvent(d,b+1,(mask&(1<<b))!=0,CurrentTime);
        XFlush(d); XCloseDisplay(d);
    }
#endif
    buttons_=mask;
}
void NativeInput::key(quint32 sym,bool down) {
    if(down) { if(keys_.size()>=256 && !keys_.contains(sym)) return; keys_.insert(sym); }
    else { if(!keys_.remove(sym)) return; }
#ifdef Q_OS_WIN
    WORD vk=0;
    switch(sym) {
    case XK_BackSpace: vk=VK_BACK; break; case XK_Tab: vk=VK_TAB; break;
    case XK_Return: vk=VK_RETURN; break; case XK_Escape: vk=VK_ESCAPE; break;
    case XK_Delete: vk=VK_DELETE; break; case XK_Insert: vk=VK_INSERT; break;
    case XK_Home: vk=VK_HOME; break; case XK_End: vk=VK_END; break;
    case XK_Page_Up: vk=VK_PRIOR; break; case XK_Page_Down: vk=VK_NEXT; break;
    case XK_Left: vk=VK_LEFT; break; case XK_Right: vk=VK_RIGHT; break;
    case XK_Up: vk=VK_UP; break; case XK_Down: vk=VK_DOWN; break;
    case XK_Shift_L: case XK_Shift_R: vk=VK_SHIFT; break;
    case XK_Control_L: case XK_Control_R: vk=VK_CONTROL; break;
    case XK_Alt_L: case XK_Alt_R: vk=VK_MENU; break;
    case XK_Super_L: vk=VK_LWIN; break; case XK_Super_R: vk=VK_RWIN; break;
    default:
        if(sym>=XK_F1 && sym<=XK_F12) vk=WORD(VK_F1+sym-XK_F1);
        else if(sym>=32 && sym<=126) { SHORT mapped=VkKeyScanW(WCHAR(sym)); if(mapped!=-1) vk=LOBYTE(mapped); }
    }
    INPUT i{}; i.type=INPUT_KEYBOARD; i.ki.dwFlags=down?0:KEYEVENTF_KEYUP;
    if(vk) { i.ki.wVk=vk; SendInput(1,&i,sizeof(i)); }
    else {
        quint32 u=(sym&0xff000000)==0x01000000 ? sym&0xffffff : sym;
        if(u>=32 && u<0xff00) { i.ki.wScan=WORD(u); i.ki.dwFlags|=KEYEVENTF_UNICODE; SendInput(1,&i,sizeof(i)); }
    }
#elif defined(Q_OS_MACOS)
    int code=-1;
    switch(sym) {
    case XK_Return: code=36; break; case XK_Tab: code=48; break; case XK_BackSpace: code=51; break;
    case XK_Escape: code=53; break; case XK_Shift_L: case XK_Shift_R: code=56; break;
    case XK_Control_L: case XK_Control_R: code=59; break; case XK_Alt_L: case XK_Alt_R: code=58; break;
    case XK_Super_L: case XK_Super_R: code=55; break;
    case XK_Left: code=123; break; case XK_Right: code=124; break; case XK_Down: code=125; break; case XK_Up: code=126; break;
    case XK_Delete: code=117; break; case XK_Home: code=115; break; case XK_End: code=119; break;
    case XK_Page_Up: code=116; break; case XK_Page_Down: code=121; break;
    }
    quint32 u=(sym&0xff000000)==0x01000000?sym&0xffffff:sym;
    if(code>=0 || (u>=32 && u<0xff00)) {
        auto e=CGEventCreateKeyboardEvent(nullptr,code>=0?code:0,down);
        if(e) {
            if(code<0) { UniChar ch=UniChar(u); CGEventKeyboardSetUnicodeString(e,1,&ch); }
            CGEventFlags flags=0;
            if(keys_.contains(XK_Shift_L)||keys_.contains(XK_Shift_R)) flags|=kCGEventFlagMaskShift;
            if(keys_.contains(XK_Control_L)||keys_.contains(XK_Control_R)) flags|=kCGEventFlagMaskControl;
            if(keys_.contains(XK_Alt_L)||keys_.contains(XK_Alt_R)) flags|=kCGEventFlagMaskAlternate;
            if(keys_.contains(XK_Super_L)||keys_.contains(XK_Super_R)) flags|=kCGEventFlagMaskCommand;
            CGEventSetFlags(e,flags); CGEventPost(kCGHIDEventTap,e); CFRelease(e);
        }
    }
#else
    if(Display* d=XOpenDisplay(nullptr)) {
        KeyCode code=XKeysymToKeycode(d,sym);
        if(code) XTestFakeKeyEvent(d,code,down,CurrentTime);
        XFlush(d); XCloseDisplay(d);
    }
#endif
}
void NativeInput::releaseAll() {
    const auto keys=keys_; for(auto k:keys) key(k,false);
    if(buttons_) pointer(lastX_,lastY_,0);
}
