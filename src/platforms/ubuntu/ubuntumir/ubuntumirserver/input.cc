/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Gerry Boland <gerry.boland@canonical.com>
 */

#include "input.h"

#include "base/native_interface.h"
#include "base/pluggableinputfilter.h"
#include "base/logging.h"

#include <private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

QUbuntuMirServerInput::QUbuntuMirServerInput(QUbuntuIntegration* integration)
    :  QUbuntuMirInput(integration)
{
    DLOG("QUbuntuMirServerInput::QUbuntuMirServerInput");
}

void QUbuntuMirServerInput::postEvent(QWindow* window, const void* event) {
    DLOG("QUbuntuMirServerInput::postEvent (this=%p, window=%p, event=%p)", this, window, event);

    PluggableInputFilter *inputFilter
            = static_cast<PluggableInputFilter*>(QGuiApplicationPrivate::platformIntegration()
                                                 ->nativeInterface()->nativeResourceForIntegration("inputFilter"));

    const Event* ev = reinterpret_cast<const Event*>(event);

    if (ev->type == KEY_EVENT_TYPE && inputFilter) {

    #if (LOG_EVENTS != 0)
        // Key event logging.
        DLOG("KEY device_id:%d source_id:%d action:%d flags:%d meta_state:%d key_code:%d "
            "scan_code:%d repeat_count:%d down_time:%lld ev_time:%lld is_system_key:%d",
            ev->device_id, ev->source_id, ev->action, ev->flags, ev->meta_state,
            ev->details.key.key_code, ev->details.key.scan_code,
            ev->details.key.repeat_count, ev->details.key.down_time,
            ev->details.key.event_time, ev->details.key.is_system_key);
    #endif

        ulong timestamp;
        Qt::KeyboardModifiers modifiers;
        QEvent::Type keyType;
        int sym;
        QString text;
        parseEvent(ev, timestamp, keyType, sym, modifiers, text);

        QKeyEvent qKeyEvent(keyType, sym, modifiers, text);
        qKeyEvent.setTimestamp(timestamp);

        // Filter?
        if (inputFilter->filterKeyEvent(&qKeyEvent))
            return;
    }

    // deliver as normal
    QUbuntuMirInput::postEvent(window, event);
}
