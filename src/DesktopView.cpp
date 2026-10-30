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

#include "DesktopView.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QBitmap>
#include <rfb/keysym.h>

namespace {
quint32 keysym(QKeyEvent* e) {
    const bool keypad = e->modifiers() & Qt::KeypadModifier;
    if(keypad) {
        switch(e->key()) {
        case Qt::Key_0: return XK_KP_0; case Qt::Key_1: return XK_KP_1; case Qt::Key_2: return XK_KP_2;
        case Qt::Key_3: return XK_KP_3; case Qt::Key_4: return XK_KP_4; case Qt::Key_5: return XK_KP_5;
        case Qt::Key_6: return XK_KP_6; case Qt::Key_7: return XK_KP_7; case Qt::Key_8: return XK_KP_8;
        case Qt::Key_9: return XK_KP_9; case Qt::Key_Asterisk: return XK_KP_Multiply; case Qt::Key_Plus: return XK_KP_Add;
        case Qt::Key_Minus: return XK_KP_Subtract; case Qt::Key_Period: return XK_KP_Decimal; case Qt::Key_Slash: return XK_KP_Divide;
        case Qt::Key_Enter: return XK_KP_Enter;
        default: break;
        }
    }
    switch(e->key()) {
    case Qt::Key_Backspace: return XK_BackSpace; case Qt::Key_Tab: case Qt::Key_Backtab: return XK_Tab;
    case Qt::Key_Return: case Qt::Key_Enter: return XK_Return; case Qt::Key_Escape: return XK_Escape;
    case Qt::Key_Delete: return XK_Delete; case Qt::Key_Insert: return XK_Insert;
    case Qt::Key_Home: return XK_Home; case Qt::Key_End: return XK_End;
    case Qt::Key_PageUp: return XK_Page_Up; case Qt::Key_PageDown: return XK_Page_Down;
    case Qt::Key_Left: return XK_Left; case Qt::Key_Right: return XK_Right;
    case Qt::Key_Up: return XK_Up; case Qt::Key_Down: return XK_Down;
    case Qt::Key_Shift: return XK_Shift_L; case Qt::Key_Control: return XK_Control_L;
    case Qt::Key_Alt: return XK_Alt_L; case Qt::Key_AltGr: return XK_ISO_Level3_Shift;
    case Qt::Key_Meta: case Qt::Key_Super_L: return XK_Super_L; case Qt::Key_Super_R: return XK_Super_R;
    case Qt::Key_Menu: return XK_Menu; case Qt::Key_CapsLock: return XK_Caps_Lock;
    case Qt::Key_NumLock: return XK_Num_Lock; case Qt::Key_ScrollLock: return XK_Scroll_Lock;
    case Qt::Key_Print: return XK_Print; case Qt::Key_Pause: return XK_Pause;
    case Qt::Key_Space: return XK_space;
    default: break;
    }
    if(e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F24) return XK_F1 + quint32(e->key() - Qt::Key_F1);
    // Letters by key code so that Ctrl/Alt shortcuts still send the letter.
    if(e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z)
        return quint32(((e->modifiers() & Qt::ShiftModifier) ? 'A' : 'a') + e->key() - Qt::Key_A);
    const auto text = e->text().toUcs4();
    if(text.size() == 1 && text[0] >= 32 && text[0] != 127) return text[0] <= 255 ? text[0] : (0x01000000 | text[0]);
    // With Ctrl held the text may be empty or a control character: fall back to the ASCII key code.
    if(e->key() >= 0x20 && e->key() <= 0x7e) return quint32(e->key());
    return 0;
}
QCursor dotCursor() {
    QPixmap pixmap(7, 7); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::black, 1)); p.setBrush(Qt::white); p.drawEllipse(QRectF(1, 1, 5, 5));
    return QCursor(pixmap, 3, 3);
}
}

DesktopView::DesktopView(QWidget* p) : QWidget(p) {
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent); setAttribute(Qt::WA_InputMethodEnabled, false);
    setCursor(dotCursor());   // the host draws its real cursor into the picture
}
QSize DesktopView::sizeHint() const { return frame_.isNull() ? QSize(1024, 640) : frame_.size(); }
void DesktopView::setFrame(const QImage& f) {
    const bool sizeChanged = f.size() != frame_.size();
    frame_ = f;
    if(sizeChanged && !fit_) setFixedSize(frame_.isNull() ? QSize(320, 200) : frame_.size());
    update();
}
void DesktopView::setFit(bool fit) {
    fit_ = fit;
    if(fit) { setMinimumSize(160, 100); setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX); }
    else setFixedSize(frame_.isNull() ? QSize(320, 200) : frame_.size());
    update();
}
void DesktopView::setControl(bool e) { if(!e) releaseAll(); control_ = e; }
QRect DesktopView::imageRect() const {
    if(!fit_) return QRect(QPoint(0, 0), frame_.size());
    QSize size = frame_.size(); size.scale(this->size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}
void DesktopView::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), QColor(24, 27, 31));
    if(frame_.isNull()) { p.setPen(Qt::white); p.drawText(rect(), Qt::AlignCenter, "Connecting…"); return; }
    const QRect r = imageRect();
    if(r.size() != frame_.size()) p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, frame_);
}
void DesktopView::mouse(QMouseEvent* e) {
    if(!control_ || frame_.isNull()) return;
    QRect r = imageRect();
    if(r.isEmpty()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = e->position().toPoint();
#else
    const QPoint pos = e->pos();
#endif
    last_ = QPoint(qBound(0, int(qint64(pos.x() - r.x()) * frame_.width() / r.width()), frame_.width() - 1),
                   qBound(0, int(qint64(pos.y() - r.y()) * frame_.height() / r.height()), frame_.height() - 1));
    mask_ = ((e->buttons() & Qt::LeftButton) ? 1 : 0) | ((e->buttons() & Qt::MiddleButton) ? 2 : 0) | ((e->buttons() & Qt::RightButton) ? 4 : 0);
    emit pointer(last_.x(), last_.y(), mask_);
}
void DesktopView::mouseMoveEvent(QMouseEvent* e) { mouse(e); }
void DesktopView::mousePressEvent(QMouseEvent* e) { setFocus(); mouse(e); }
void DesktopView::mouseReleaseEvent(QMouseEvent* e) { mouse(e); }
void DesktopView::wheelEvent(QWheelEvent* e) {
    if(!control_ || frame_.isNull()) return;
    const QPoint d = e->angleDelta();
    int bit = 0;
    if(d.y()) bit = d.y() > 0 ? 8 : 16;
    else if(d.x()) bit = d.x() > 0 ? 32 : 64;
    if(!bit) return;
    emit pointer(last_.x(), last_.y(), mask_ | bit);
    emit pointer(last_.x(), last_.y(), mask_);
    e->accept();
}
bool DesktopView::event(QEvent* e) {
    // Keep Tab, Alt and application shortcuts for the remote desktop while controlling it,
    // except the viewer's own Ctrl+Alt+Shift shortcuts.
    if(e->type() == QEvent::ShortcutOverride && control_) {
        auto* k = static_cast<QKeyEvent*>(e);
        const auto mods = k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        if(mods != (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) { e->accept(); return true; }
    }
    return QWidget::event(e);
}
void DesktopView::keyPressEvent(QKeyEvent* e) {
    if(!control_) return;
    const quint32 sym = keysym(e);
    if(!sym) return;
    const int id = e->nativeScanCode() ? int(e->nativeScanCode()) | 0x40000000 : e->key();
    if(e->isAutoRepeat() && keys_.contains(id)) { emit key(keys_.value(id), true); e->accept(); return; }
    keys_.insert(id, sym); emit key(sym, true); e->accept();
}
void DesktopView::keyReleaseEvent(QKeyEvent* e) {
    if(!control_ || e->isAutoRepeat()) return;
    const int id = e->nativeScanCode() ? int(e->nativeScanCode()) | 0x40000000 : e->key();
    auto it = keys_.find(id);
    if(it != keys_.end()) { emit key(it.value(), false); keys_.erase(it); e->accept(); }
}
bool DesktopView::forwardSystemKey(quint32 sym, bool down) {
    if(!control_ || !hasFocus()) return false;
    if(down) { systemKeys_[sym]++; emit key(sym, true); }
    else if(systemKeys_.value(sym)) { systemKeys_.remove(sym); emit key(sym, false); }
    return true;
}
void DesktopView::releaseAll() {
    for(auto k : keys_) emit key(k, false);
    keys_.clear();
    for(auto it = systemKeys_.begin(); it != systemKeys_.end(); ++it) emit key(it.key(), false);
    systemKeys_.clear();
    if(mask_) emit pointer(last_.x(), last_.y(), 0);
    mask_ = 0;
}
void DesktopView::focusOutEvent(QFocusEvent* e) { releaseAll(); QWidget::focusOutEvent(e); }
