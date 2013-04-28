// This file is part of QtUbuntu, a set of Qt components for Ubuntu.
// Copyright © 2013 Canonical Ltd.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 3, as published by
// the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
// SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "input.h"
#include "integration.h"
#include "native_interface.h"
#include "logging.h"
#if !defined(QT_NO_DEBUG)
#include <QtCore/QThread>
#endif
#include <QTextCodec>
#include <QtCore/qglobal.h>
#include <QtCore/QCoreApplication>
#include <private/qguiapplication_p.h>
#include <qpa/qplatforminputcontext.h>
#include <ubuntu/application/ui/ubuntu_application_ui.h>
#include <input/input_stack_compatibility_layer_flags.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define LOG_EVENTS 0

// Lookup table for the key types.
// FIXME(loicm) Not sure what to do with that multiple thing.
static const QEvent::Type kEventType[] = {
  QEvent::KeyPress,    // ISCL_KEY_EVENT_ACTION_DOWN     = 0
  QEvent::KeyRelease,  // ISCL_KEY_EVENT_ACTION_UP       = 1
  QEvent::KeyPress     // ISCL_KEY_EVENT_ACTION_MULTIPLE = 2
};

class QUbuntuBaseEvent : public QEvent {
 public:
  QUbuntuBaseEvent(QWindow* window, const Event* event, QEvent::Type type)
      : QEvent(type)
      , window_(window) {
    memcpy(&nativeEvent_, event, sizeof(Event));
  }
  QWindow* window_;
  Event nativeEvent_;
};

QUbuntuBaseInput::QUbuntuBaseInput(QUbuntuBaseIntegration* integration, int maxPointCount)
    : integration_(integration)
    , eventFilterType_(static_cast<QUbuntuBaseNativeInterface*>(
        integration->nativeInterface())->genericEventFilterType())
    , eventType_(static_cast<QEvent::Type>(QEvent::registerEventType())) {
  DASSERT(maxPointCount > 0);

  // Initialize touch device.
  touchDevice_ = new QTouchDevice();
  touchDevice_->setType(QTouchDevice::TouchScreen);
  touchDevice_->setCapabilities(
      QTouchDevice::Position | QTouchDevice::Area | QTouchDevice::Pressure |
      QTouchDevice::NormalizedPosition);
  QWindowSystemInterface::registerTouchDevice(touchDevice_);

  // Initialize touch points.
  touchPoints_.reserve(maxPointCount);
  for (int i = 0; i < maxPointCount; i++) {
    QWindowSystemInterface::TouchPoint tp;
    tp.id = i;
    tp.state = Qt::TouchPointReleased;
    touchPoints_ << tp;
  }

  DLOG("QUbuntuBaseInput::QUbuntuBaseInput (this=%p, integration=%p, maxPointCount=%d)", this,
       integration, maxPointCount);

  xkb_rule_names names;
  names.rules = strdup("evdev");
  names.model = strdup("pc105");
  names.layout = strdup("us");
  names.variant = strdup("");
  names.options = strdup("");

  xkbcontext = xkb_context_new(xkb_context_flags(0));
  if (xkbcontext) {
      xkbmap = xkb_map_new_from_names(xkbcontext, &names, xkb_map_compile_flags(0));
      if (xkbmap) {
          xkbstate = xkb_state_new(xkbmap);
      }
  }

}

QUbuntuBaseInput::~QUbuntuBaseInput() {
  DLOG("QUbuntuBaseInput::~QUbuntuBaseInput");
  // touchDevice_ isn't cleaned up on purpose as it crashes or asserts on "Bus Error".
  touchPoints_.clear();
}

void QUbuntuBaseInput::customEvent(QEvent* event) {
  DLOG("QUbuntuBaseInput::customEvent (this=%p, event=%p)", this, event);
  DASSERT(QThread::currentThread() == thread());
  QUbuntuBaseEvent* ubuntuEvent = static_cast<QUbuntuBaseEvent*>(event);

  // Event filtering.
  long result;
  if (QWindowSystemInterface::handleNativeEvent(
          ubuntuEvent->window_, eventFilterType_, &ubuntuEvent->nativeEvent_, &result) == true) {
    DLOG("event filtered out by native interface");
    return;
  }

  // Event dispatching.
  switch (ubuntuEvent->nativeEvent_.type) {
    case MOTION_EVENT_TYPE: {
      dispatchMotionEvent(ubuntuEvent->window_, &ubuntuEvent->nativeEvent_);
      break;
    }
    case KEY_EVENT_TYPE: {
      dispatchKeyEvent(ubuntuEvent->window_, &ubuntuEvent->nativeEvent_);
      break;
    }
    case HW_SWITCH_EVENT_TYPE: {
      dispatchHWSwitchEvent(ubuntuEvent->window_, &ubuntuEvent->nativeEvent_);
      break;
    }
    default: {
      DLOG("unhandled event type %d", ubuntuEvent->nativeEvent_.type);
    }
  }
}

void QUbuntuBaseInput::postEvent(QWindow* window, const void* event) {
  DLOG("QUbuntuBaseInput::postEvent (this=%p, window=%p, event=%p)", this, window, event);
  QCoreApplication::postEvent(this, new QUbuntuBaseEvent(
      window, reinterpret_cast<const Event*>(event), eventType_));
}

void QUbuntuBaseInput::dispatchMotionEvent(QWindow* window, const void* ev) {
  DLOG("QUbuntuBaseInput::dispatchMotionEvent (this=%p, window=%p, event=%p)", this, window, ev);
  const Event* event = reinterpret_cast<const Event*>(ev);

#if (LOG_EVENTS != 0)
  // Motion event logging.
  LOG("MOTION device_id:%d source_id:%d action:%d flags:%d meta_state:%d edge_flags:%d "
      "button_state:%d x_offset:%.2f y_offset:%.2f x_precision:%.2f y_precision:%.2f "
      "down_time:%lld event_time:%lld pointer_count:%d {", event->device_id,
      event->source_id, event->action, event->flags, event->meta_state,
      event->details.motion.edge_flags, event->details.motion.button_state,
      event->details.motion.x_offset, event->details.motion.y_offset,
      event->details.motion.x_precision, event->details.motion.y_precision,
      event->details.motion.down_time, event->details.motion.event_time,
      event->details.motion.pointer_count);
  for (size_t i = 0; i < event->details.motion.pointer_count; i++) {
    LOG("  id:%d x:%.2f y:%.2f rx:%.2f ry:%.2f maj:%.2f min:%.2f sz:%.2f press:%.2f",
        event->details.motion.pointer_coordinates[i].id,
        event->details.motion.pointer_coordinates[i].x,
        event->details.motion.pointer_coordinates[i].y,
        event->details.motion.pointer_coordinates[i].raw_x,
        event->details.motion.pointer_coordinates[i].raw_y,
        event->details.motion.pointer_coordinates[i].touch_major,
        event->details.motion.pointer_coordinates[i].touch_minor,
        event->details.motion.pointer_coordinates[i].size,
        event->details.motion.pointer_coordinates[i].pressure
        // event->details.motion.pointer_coordinates[i].orientation  -> Always 0.0.
        );
  }
  LOG("}");
#endif

  // FIXME(loicm) Max pressure is device specific. That one is for the Samsung Galaxy Nexus. That
  //     needs to be fixed as soon as the compat input lib adds query support.
  const float kMaxPressure = 1.28;
  const QRect kWindowGeometry = window->geometry();

  switch (event->action & ISCL_MOTION_EVENT_ACTION_MASK) {
    case ISCL_MOTION_EVENT_ACTION_MOVE: {
      int eventIndex = 0;
      const int kPointerCount = event->details.motion.pointer_count;
      for (int touchIndex = 0; eventIndex < kPointerCount; touchIndex++) {
        if (touchPoints_[touchIndex].state != Qt::TouchPointReleased) {
          const float kX = event->details.motion.pointer_coordinates[eventIndex].raw_x;
          const float kY = event->details.motion.pointer_coordinates[eventIndex].raw_y;
          const float kW = event->details.motion.pointer_coordinates[eventIndex].touch_major;
          const float kH = event->details.motion.pointer_coordinates[eventIndex].touch_minor;
          const float kP = event->details.motion.pointer_coordinates[eventIndex].pressure;
          touchPoints_[touchIndex].area = QRectF(kX - (kW / 2.0), kY - (kH / 2.0), kW, kH);
          touchPoints_[touchIndex].normalPosition = QPointF(
              kX / kWindowGeometry.width(), kY / kWindowGeometry.height());
          touchPoints_[touchIndex].pressure = kP / kMaxPressure;
          touchPoints_[touchIndex].state = Qt::TouchPointMoved;
          eventIndex++;
        }
      }
      break;
    }

    case ISCL_MOTION_EVENT_ACTION_DOWN: {
      const int kTouchIndex = event->details.motion.pointer_coordinates[0].id;
      const float kX = event->details.motion.pointer_coordinates[0].raw_x;
      const float kY = event->details.motion.pointer_coordinates[0].raw_y;
      const float kW = event->details.motion.pointer_coordinates[0].touch_major;
      const float kH = event->details.motion.pointer_coordinates[0].touch_minor;
      const float kP = event->details.motion.pointer_coordinates[0].pressure;
      touchPoints_[kTouchIndex].state = Qt::TouchPointPressed;
      touchPoints_[kTouchIndex].area = QRectF(kX - (kW / 2.0), kY - (kH / 2.0), kW, kH);
      touchPoints_[kTouchIndex].normalPosition = QPointF(
          kX / kWindowGeometry.width(), kY / kWindowGeometry.height());
      touchPoints_[kTouchIndex].pressure = kP / kMaxPressure;
      break;
    }

    case ISCL_MOTION_EVENT_ACTION_UP: {
      const int kTouchIndex = event->details.motion.pointer_coordinates[0].id;
      touchPoints_[kTouchIndex].state = Qt::TouchPointReleased;
      break;
    }

    case ISCL_MOTION_EVENT_ACTION_POINTER_DOWN: {
      const int eventIndex = (event->action & ISCL_MOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
          ISCL_MOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
      const int kTouchIndex = event->details.motion.pointer_coordinates[eventIndex].id;
      const float kX = event->details.motion.pointer_coordinates[eventIndex].raw_x;
      const float kY = event->details.motion.pointer_coordinates[eventIndex].raw_y;
      const float kW = event->details.motion.pointer_coordinates[eventIndex].touch_major;
      const float kH = event->details.motion.pointer_coordinates[eventIndex].touch_minor;
      const float kP = event->details.motion.pointer_coordinates[eventIndex].pressure;
      touchPoints_[kTouchIndex].state = Qt::TouchPointPressed;
      touchPoints_[kTouchIndex].area = QRectF(kX - (kW / 2.0), kY - (kH / 2.0), kW, kH);
      touchPoints_[kTouchIndex].normalPosition = QPointF(
          kX / kWindowGeometry.width(), kY / kWindowGeometry.height());
      touchPoints_[kTouchIndex].pressure = kP / kMaxPressure;
      break;
    }

    case ISCL_MOTION_EVENT_ACTION_CANCEL:
    case ISCL_MOTION_EVENT_ACTION_POINTER_UP: {
      const int kEventIndex = (event->action & ISCL_MOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
          ISCL_MOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
      const int kTouchIndex = event->details.motion.pointer_coordinates[kEventIndex].id;
      touchPoints_[kTouchIndex].state = Qt::TouchPointReleased;
      break;
    }

    case ISCL_MOTION_EVENT_ACTION_OUTSIDE:
    case ISCL_MOTION_EVENT_ACTION_HOVER_MOVE:
    case ISCL_MOTION_EVENT_ACTION_SCROLL:
    case ISCL_MOTION_EVENT_ACTION_HOVER_ENTER:
    case ISCL_MOTION_EVENT_ACTION_HOVER_EXIT:
    default: {
      DLOG("unhandled motion event action %d", event->action & ISCL_MOTION_EVENT_ACTION_MASK);
    }
  }

  // Touch event propagation.
  handleTouchEvent(window, event->details.motion.event_time / 1000000, touchDevice_, touchPoints_);
}

void QUbuntuBaseInput::handleTouchEvent(
    QWindow* window, ulong timestamp, QTouchDevice* device,
    const QList<struct QWindowSystemInterface::TouchPoint> &points) {
  DLOG("QUbuntuBaseInput::handleTouchEvent (this=%p, window=%p, timestamp=%lu, device=%p)",
       this, window, timestamp, device);
  QWindowSystemInterface::handleTouchEvent(window, timestamp, device, points);
}


static const uint32_t KeyTbl[] = {
    XKB_KEY_Escape,                  Qt::Key_Escape,
    XKB_KEY_Tab,                     Qt::Key_Tab,
    XKB_KEY_ISO_Left_Tab,            Qt::Key_Backtab,
    XKB_KEY_BackSpace,               Qt::Key_Backspace,
    XKB_KEY_Return,                  Qt::Key_Return,
    XKB_KEY_Insert,                  Qt::Key_Insert,
    XKB_KEY_Delete,                  Qt::Key_Delete,
    XKB_KEY_Clear,                   Qt::Key_Delete,
    XKB_KEY_Pause,                   Qt::Key_Pause,
    XKB_KEY_Print,                   Qt::Key_Print,

    XKB_KEY_Home,                    Qt::Key_Home,
    XKB_KEY_End,                     Qt::Key_End,
    XKB_KEY_Left,                    Qt::Key_Left,
    XKB_KEY_Up,                      Qt::Key_Up,
    XKB_KEY_Right,                   Qt::Key_Right,
    XKB_KEY_Down,                    Qt::Key_Down,
    XKB_KEY_Prior,                   Qt::Key_PageUp,
    XKB_KEY_Next,                    Qt::Key_PageDown,

    XKB_KEY_Shift_L,                 Qt::Key_Shift,
    XKB_KEY_Shift_R,                 Qt::Key_Shift,
    XKB_KEY_Shift_Lock,              Qt::Key_Shift,
    XKB_KEY_Control_L,               Qt::Key_Control,
    XKB_KEY_Control_R,               Qt::Key_Control,
    XKB_KEY_Meta_L,                  Qt::Key_Meta,
    XKB_KEY_Meta_R,                  Qt::Key_Meta,
    XKB_KEY_Alt_L,                   Qt::Key_Alt,
    XKB_KEY_Alt_R,                   Qt::Key_Alt,
    XKB_KEY_Caps_Lock,               Qt::Key_CapsLock,
    XKB_KEY_Num_Lock,                Qt::Key_NumLock,
    XKB_KEY_Scroll_Lock,             Qt::Key_ScrollLock,
    XKB_KEY_Super_L,                 Qt::Key_Super_L,
    XKB_KEY_Super_R,                 Qt::Key_Super_R,
    XKB_KEY_Menu,                    Qt::Key_Menu,
    XKB_KEY_Hyper_L,                 Qt::Key_Hyper_L,
    XKB_KEY_Hyper_R,                 Qt::Key_Hyper_R,
    XKB_KEY_Help,                    Qt::Key_Help,

    XKB_KEY_KP_Space,                Qt::Key_Space,
    XKB_KEY_KP_Tab,                  Qt::Key_Tab,
    XKB_KEY_KP_Enter,                Qt::Key_Enter,
    XKB_KEY_KP_Home,                 Qt::Key_Home,
    XKB_KEY_KP_Left,                 Qt::Key_Left,
    XKB_KEY_KP_Up,                   Qt::Key_Up,
    XKB_KEY_KP_Right,                Qt::Key_Right,
    XKB_KEY_KP_Down,                 Qt::Key_Down,
    XKB_KEY_KP_Prior,                Qt::Key_PageUp,
    XKB_KEY_KP_Next,                 Qt::Key_PageDown,
    XKB_KEY_KP_End,                  Qt::Key_End,
    XKB_KEY_KP_Begin,                Qt::Key_Clear,
    XKB_KEY_KP_Insert,               Qt::Key_Insert,
    XKB_KEY_KP_Delete,               Qt::Key_Delete,
    XKB_KEY_KP_Equal,                Qt::Key_Equal,
    XKB_KEY_KP_Multiply,             Qt::Key_Asterisk,
    XKB_KEY_KP_Add,                  Qt::Key_Plus,
    XKB_KEY_KP_Separator,            Qt::Key_Comma,
    XKB_KEY_KP_Subtract,             Qt::Key_Minus,
    XKB_KEY_KP_Decimal,              Qt::Key_Period,
    XKB_KEY_KP_Divide,               Qt::Key_Slash,

    XKB_KEY_ISO_Level3_Shift,        Qt::Key_AltGr,
    XKB_KEY_Multi_key,               Qt::Key_Multi_key,
    XKB_KEY_Codeinput,               Qt::Key_Codeinput,
    XKB_KEY_SingleCandidate,         Qt::Key_SingleCandidate,
    XKB_KEY_MultipleCandidate,       Qt::Key_MultipleCandidate,
    XKB_KEY_PreviousCandidate,       Qt::Key_PreviousCandidate,

    XKB_KEY_Mode_switch,             Qt::Key_Mode_switch,
    XKB_KEY_script_switch,           Qt::Key_Mode_switch,

    0,                          0
};

static const unsigned short katakanaKeysymsToUnicode[] = {
    0x0000, 0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1,
    0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3,
    0x30FC, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD,
    0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD,
    0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC,
    0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE,
    0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
    0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3, 0x309B, 0x309C
};

static const unsigned short cyrillicKeysymsToUnicode[] = {
    0x0000, 0x0452, 0x0453, 0x0451, 0x0454, 0x0455, 0x0456, 0x0457,
    0x0458, 0x0459, 0x045a, 0x045b, 0x045c, 0x0000, 0x045e, 0x045f,
    0x2116, 0x0402, 0x0403, 0x0401, 0x0404, 0x0405, 0x0406, 0x0407,
    0x0408, 0x0409, 0x040a, 0x040b, 0x040c, 0x0000, 0x040e, 0x040f,
    0x044e, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
    0x0445, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
    0x043f, 0x044f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
    0x044c, 0x044b, 0x0437, 0x0448, 0x044d, 0x0449, 0x0447, 0x044a,
    0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
    0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
    0x042c, 0x042b, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427, 0x042a
};

static const unsigned short greekKeysymsToUnicode[] = {
    0x0000, 0x0386, 0x0388, 0x0389, 0x038a, 0x03aa, 0x0000, 0x038c,
    0x038e, 0x03ab, 0x0000, 0x038f, 0x0000, 0x0000, 0x0385, 0x2015,
    0x0000, 0x03ac, 0x03ad, 0x03ae, 0x03af, 0x03ca, 0x0390, 0x03cc,
    0x03cd, 0x03cb, 0x03b0, 0x03ce, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
    0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f,
    0x03a0, 0x03a1, 0x03a3, 0x0000, 0x03a4, 0x03a5, 0x03a6, 0x03a7,
    0x03a8, 0x03a9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7,
    0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf,
    0x03c0, 0x03c1, 0x03c3, 0x03c2, 0x03c4, 0x03c5, 0x03c6, 0x03c7,
    0x03c8, 0x03c9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const unsigned short technicalKeysymsToUnicode[] = {
    0x0000, 0x23B7, 0x250C, 0x2500, 0x2320, 0x2321, 0x2502, 0x23A1,
    0x23A3, 0x23A4, 0x23A6, 0x239B, 0x239D, 0x239E, 0x23A0, 0x23A8,
    0x23AC, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x2264, 0x2260, 0x2265, 0x222B,
    0x2234, 0x221D, 0x221E, 0x0000, 0x0000, 0x2207, 0x0000, 0x0000,
    0x223C, 0x2243, 0x0000, 0x0000, 0x0000, 0x21D4, 0x21D2, 0x2261,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x221A, 0x0000,
    0x0000, 0x0000, 0x2282, 0x2283, 0x2229, 0x222A, 0x2227, 0x2228,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2202,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0192, 0x0000,
    0x0000, 0x0000, 0x0000, 0x2190, 0x2191, 0x2192, 0x2193, 0x0000
};

static const unsigned short specialKeysymsToUnicode[] = {
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x0000, 0x0000,
    0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA,
    0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,
    0x2502, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const unsigned short publishingKeysymsToUnicode[] = {
    0x0000, 0x2003, 0x2002, 0x2004, 0x2005, 0x2007, 0x2008, 0x2009,
    0x200a, 0x2014, 0x2013, 0x0000, 0x0000, 0x0000, 0x2026, 0x2025,
    0x2153, 0x2154, 0x2155, 0x2156, 0x2157, 0x2158, 0x2159, 0x215a,
    0x2105, 0x0000, 0x0000, 0x2012, 0x2329, 0x0000, 0x232a, 0x0000,
    0x0000, 0x0000, 0x0000, 0x215b, 0x215c, 0x215d, 0x215e, 0x0000,
    0x0000, 0x2122, 0x2613, 0x0000, 0x25c1, 0x25b7, 0x25cb, 0x25af,
    0x2018, 0x2019, 0x201c, 0x201d, 0x211e, 0x0000, 0x2032, 0x2033,
    0x0000, 0x271d, 0x0000, 0x25ac, 0x25c0, 0x25b6, 0x25cf, 0x25ae,
    0x25e6, 0x25ab, 0x25ad, 0x25b3, 0x25bd, 0x2606, 0x2022, 0x25aa,
    0x25b2, 0x25bc, 0x261c, 0x261e, 0x2663, 0x2666, 0x2665, 0x0000,
    0x2720, 0x2020, 0x2021, 0x2713, 0x2717, 0x266f, 0x266d, 0x2642,
    0x2640, 0x260e, 0x2315, 0x2117, 0x2038, 0x201a, 0x201e, 0x0000
};

static const unsigned short aplKeysymsToUnicode[] = {
    0x0000, 0x0000, 0x0000, 0x003c, 0x0000, 0x0000, 0x003e, 0x0000,
    0x2228, 0x2227, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x00af, 0x0000, 0x22a5, 0x2229, 0x230a, 0x0000, 0x005f, 0x0000,
    0x0000, 0x0000, 0x2218, 0x0000, 0x2395, 0x0000, 0x22a4, 0x25cb,
    0x0000, 0x0000, 0x0000, 0x2308, 0x0000, 0x0000, 0x222a, 0x0000,
    0x2283, 0x0000, 0x2282, 0x0000, 0x22a2, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x22a3, 0x0000, 0x0000, 0x0000
};

static const unsigned short koreanKeysymsToUnicode[] = {
    0x0000, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137,
    0x3138, 0x3139, 0x313a, 0x313b, 0x313c, 0x313d, 0x313e, 0x313f,
    0x3140, 0x3141, 0x3142, 0x3143, 0x3144, 0x3145, 0x3146, 0x3147,
    0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e, 0x314f,
    0x3150, 0x3151, 0x3152, 0x3153, 0x3154, 0x3155, 0x3156, 0x3157,
    0x3158, 0x3159, 0x315a, 0x315b, 0x315c, 0x315d, 0x315e, 0x315f,
    0x3160, 0x3161, 0x3162, 0x3163, 0x11a8, 0x11a9, 0x11aa, 0x11ab,
    0x11ac, 0x11ad, 0x11ae, 0x11af, 0x11b0, 0x11b1, 0x11b2, 0x11b3,
    0x11b4, 0x11b5, 0x11b6, 0x11b7, 0x11b8, 0x11b9, 0x11ba, 0x11bb,
    0x11bc, 0x11bd, 0x11be, 0x11bf, 0x11c0, 0x11c1, 0x11c2, 0x316d,
    0x3171, 0x3178, 0x317f, 0x3181, 0x3184, 0x3186, 0x318d, 0x318e,
    0x11eb, 0x11f0, 0x11f9, 0x0000, 0x0000, 0x0000, 0x0000, 0x20a9
};

static QChar keysymToUnicode(unsigned char byte3, unsigned char byte4)
{
    switch (byte3) {
    case 0x04:
        // katakana
        if (byte4 > 0xa0 && byte4 < 0xe0)
            return QChar(katakanaKeysymsToUnicode[byte4 - 0xa0]);
        else if (byte4 == 0x7e)
            return QChar(0x203e); // Overline
        break;
    case 0x06:
        // russian, use lookup table
        if (byte4 > 0xa0)
            return QChar(cyrillicKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x07:
        // greek
        if (byte4 > 0xa0)
            return QChar(greekKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x08:
        // technical
        if (byte4 > 0xa0)
            return QChar(technicalKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x09:
        // special
        if (byte4 >= 0xe0)
            return QChar(specialKeysymsToUnicode[byte4 - 0xe0]);
        break;
    case 0x0a:
        // publishing
        if (byte4 > 0xa0)
            return QChar(publishingKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x0b:
        // APL
        if (byte4 > 0xa0)
            return QChar(aplKeysymsToUnicode[byte4 - 0xa0]);
        break;
    case 0x0e:
        // Korean
        if (byte4 > 0xa0)
            return QChar(koreanKeysymsToUnicode[byte4 - 0xa0]);
        break;
    default:
        break;
    }
    return QChar(0x0);
}

static uint32_t translateKey(uint32_t sym)
{
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F35)
        return Qt::Key_F1 + (int(sym) - XKB_KEY_F1);

    for (int i = 0; KeyTbl[i]; i += 2)
        if (sym == KeyTbl[i])
            return KeyTbl[i + 1];

    return sym;
}

static QString translateKeysym(xkb_keysym_t keysym,
                                     int &code, 
                                     QByteArray &chars, int &count)
{
    // all keysyms smaller than 0xff00 are actally keys that can be mapped to unicode chars

    QTextCodec *mapper = QTextCodec::codecForLocale();
    QChar converted;

    if (/*count == 0 &&*/ keysym < 0xff00) {
        unsigned char byte3 = (unsigned char)(keysym >> 8);
        int mib = -1;
        switch(byte3) {
        case 0: // Latin 1
        case 1: // Latin 2
        case 2: //latin 3
        case 3: // latin4
            mib = byte3 + 4; break;
        case 5: // arabic
            mib = 82; break;
        case 12: // Hebrew
            mib = 85; break;
        case 13: // Thai
            mib = 2259; break;
        case 4: // kana
        case 6: // cyrillic
        case 7: // greek
        case 8: // technical, no mapping here at the moment
        case 9: // Special
        case 10: // Publishing
        case 11: // APL
        case 14: // Korean, no mapping
            mib = -1; // manual conversion
            mapper= 0;
#if !defined(QT_NO_XIM)
            converted = keysymToUnicode(byte3, keysym & 0xff);
#endif
        case 0x20:
            // currency symbols
            if (keysym >= 0x20a0 && keysym <= 0x20ac) {
                mib = -1; // manual conversion
                mapper = 0;
                converted = (uint)keysym;
            }
            break;
        default:
            break;
        }
        if (mib != -1) {
            mapper = QTextCodec::codecForMib(mib);
            if (chars.isEmpty())
                chars.resize(1);
            chars[0] = (unsigned char) (keysym & 0xff); // get only the fourth bit for conversion later
            count = 1;
        }
    } else if (keysym >= 0x1000000 && keysym <= 0x100ffff) {
        converted = (ushort) (keysym - 0x1000000);
        mapper = 0;
    }
    if (count < (int)chars.size()-1)
        chars[count] = '\0';

    QString text;
    if (!mapper && converted.unicode() != 0x0) {
        text = converted;
    } else if (!chars.isEmpty()) {
        // convert chars (8bit) to text (unicode).
        if (mapper)
            text = mapper->toUnicode(chars.data(), count, 0);
        if (text.isEmpty()) {
            // no mapper, or codec couldn't convert to unicode (this
            // can happen when running in the C locale or with no LANG
            // set). try converting from latin-1
            text = QString::fromLatin1(chars);
        }
    }

    return text;
}

static uint32_t translateKey(uint32_t sym, char *string, size_t size)
{
    Q_UNUSED(size);
    string[0] = '\0';

    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F35)
        return Qt::Key_F1 + (int(sym) - XKB_KEY_F1);

    for (int i = 0; KeyTbl[i]; i += 2)
        if (sym == KeyTbl[i])
            return KeyTbl[i + 1];

    string[0] = sym;
    string[1] = '\0';
    return toupper(sym);
}

void QUbuntuBaseInput::dispatchKeyEvent(QWindow* window, const void* ev) {
  DLOG("QUbuntuBaseInput::dispatchKeyEvent (this=%p, window=%p, event=%p)", this, window, ev);
  const Event* event = reinterpret_cast<const Event*>(ev);

#if (LOG_EVENTS != 0)
  // Key event logging.
  LOG("KEY device_id:%d source_id:%d action:%d flags:%d meta_state:%d key_code:%d "
      "scan_code:%d repeat_count:%d down_time:%lld event_time:%lld is_system_key:%d",
      event->device_id, event->source_id, event->action, event->flags, event->meta_state,
      event->details.key.key_code, event->details.key.scan_code,
      event->details.key.repeat_count, event->details.key.down_time,
      event->details.key.event_time, event->details.key.is_system_key);
#endif

  // Key event propagation.
  ulong timestamp = event->details.key.event_time / 1000000;
  xkb_keysym_t xk_sym = (xkb_keysym_t)event->details.key.key_code;

  const int kMetaState = event->meta_state;
  Qt::KeyboardModifiers modifiers = Qt::NoModifier;
  if (kMetaState & ISCL_META_SHIFT_ON) {
    modifiers |= Qt::ShiftModifier;
  }
  if (kMetaState & ISCL_META_CTRL_ON) {
    modifiers |= Qt::ControlModifier;
  }
  if (kMetaState & ISCL_META_ALT_ON) {
    modifiers |= Qt::AltModifier;
  }
  if (kMetaState & ISCL_META_META_ON) {
    modifiers |= Qt::MetaModifier;
  }

  QEvent::Type type = event->action == 1 ? QEvent::KeyRelease : QEvent::KeyPress;
  int sym = translateKey(xk_sym);

  char s[2];
  translateKey(xk_sym, s, sizeof(s));
  s[0] = xk_sym;
  s[1] = '\0';
  
  QWindowSystemInterface::handleExtendedKeyEvent(window,
                                                 timestamp, type, sym,
                                                 modifiers,
                                                 event->details.key.scan_code, 0, 0,
                                                 QString::fromLatin1(s));
}

void QUbuntuBaseInput::handleKeyEvent(
    QWindow* window, ulong timestamp, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
    const QString& text) {
  DLOG("QUbuntuBaseInput::handleKeyEvent (this=%p window=%p, timestamp=%lu, type=%d, key=%d, "
       "modifiers=%d, text='%s')", this, window, timestamp, static_cast<int>(type), key,
       static_cast<int>(modifiers), text.toUtf8().data());
  QWindowSystemInterface::handleKeyEvent(window, timestamp, type, key, modifiers, text);
}

void QUbuntuBaseInput::dispatchHWSwitchEvent(QWindow* window, const void* ev) {
  Q_UNUSED(window);
  Q_UNUSED(ev);
  DLOG("QUbuntuBaseInput::dispatchSwitchEvent (this=%p, window=%p, event=%p)", this, window, ev);

#if (LOG_EVENTS != 0)
  // HW switch event logging.
  const Event* event = reinterpret_cast<const Event*>(ev);
  LOG("HWSWITCH device_id:%d source_id:%d action:%d flags:%d meta_state:%d event_time:%lld "
      "policy_flags:%u switch_values:%d switch_mask:%d", event->device_id, event->source_id,
      event->action, event->flags, event->meta_state, event->details.hw_switch.event_time,
      event->details.hw_switch.policy_flags, event->details.hw_switch.switch_values,
      event->details.hw_switch.switch_mask);
#endif

  // FIXME(loicm) Not sure how to interpret that kind of event.
  DLOG("hw switch events are not handled");
}
