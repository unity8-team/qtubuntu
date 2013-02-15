// This file is part of QtUbuntu, a set of Qt components for Ubuntu.
// Copyright © 2013 Canonical Ltd.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// Unit conversion code has been extracted from the Ubuntu UI toolkit.

#include "screen.h"
#include "base/logging.h"
#include <QtCore/qmath.h>
#include <ubuntu/application/ui/ubuntu_application_ui.h>

// Grid unit used if GRID_UNIT_PX is not in the environment.
const int kDefaultGridUnit = 8;

// Size of the side stage in grid units.
// FIXME(loicm) Hard-coded to 40 grid units for now.
const int kSideStageWidth = 40;

QUbuntuScreen::QUbuntuScreen() {
  // Retrieve units from the environment.
  int gridUnit = kDefaultGridUnit;
  QByteArray gridUnitString = qgetenv("GRID_UNIT_PX");
  if (!gridUnitString.isEmpty()) {
    bool ok;
    gridUnit = gridUnitString.toInt(&ok);
    if (!ok) {
      gridUnit = kDefaultGridUnit;
    }
  }
  gridUnit_ = gridUnit;
  densityPixelRatio_ = static_cast<float>(gridUnit) / kDefaultGridUnit;
  DLOG("grid unit is %d", gridUnit);
  DLOG("density pixel ratio is %.2f", densityPixelRatio_);

  // Compute menubar strut.
  // FIXME(loicm) Hard-coded to 3 grid units plus 2 density independent pixels for now.
  struct { int left; int right; int top; int bottom; } strut = {
    0, 0, gridUnitToPixel(3) + densityPixelToPixel(2), 0
  };
  DLOG("menu bar height is %d pixels", strut.top);

  // Get screen resolution.
  ubuntu_application_ui_physical_display_info info;
  ubuntu_application_ui_create_display_info(&info, 0);
  const int kScreenWidth = ubuntu_application_ui_query_horizontal_resolution(info);
  const int kScreenHeight = ubuntu_application_ui_query_vertical_resolution(info);
  ASSERT(kScreenWidth > 0 && kScreenHeight > 0);
  DLOG("screen resolution: %dx%d", kScreenWidth, kScreenHeight);
  ubuntu_application_ui_destroy_display_info(info);

  // Store geometries depending on the stage hint.
  const StageHint kStageHint = ubuntu_application_ui_setup_get_stage_hint();
  DASSERT(kStageHint == MAIN_STAGE_HINT || kStageHint == SIDE_STAGE_HINT);
  if (kStageHint != SIDE_STAGE_HINT) {
    geometry_ = QRect(0, 0, kScreenWidth, kScreenHeight);
    availableGeometry_ = QRect(
        strut.left, strut.top, kScreenWidth - strut.left - strut.right,
        kScreenHeight - strut.top - strut.bottom);
  } else {
    const int kSideStageWidthPixels = gridUnitToPixel(kSideStageWidth);
    geometry_ = QRect(kScreenWidth - kSideStageWidthPixels, 0, kSideStageWidthPixels,
                      kScreenHeight);
    availableGeometry_ = QRect(
        kScreenWidth - kSideStageWidthPixels + strut.left, strut.top,
        kSideStageWidthPixels - strut.left - strut.right, kScreenHeight - strut.top - strut.bottom);
  }

  DLOG("QUbuntuScreen::QUbuntuScreen (this=%p)", this);

  // Set the default orientation based on the initial screen dimmensions.
  nativeOrientation_ = kScreenWidth >= kScreenHeight ? Qt::LandscapeOrientation : Qt::PortraitOrientation;
}

QUbuntuScreen::~QUbuntuScreen() {
  DLOG("QUbuntuScreen::~QUbuntuScreen");
}

int QUbuntuScreen::gridUnitToPixel(int value) const {
  DLOG("QUbuntuScreen::gridUnitToPixel (this=%p, value=%d)", this, value);
  return value * gridUnit_;
}

int QUbuntuScreen::densityPixelToPixel(int value) const {
  DLOG("QUbuntuScreen::densityPixelToPixel (this=%p, value=%d)", this, value);
  if (value <= 2) {
    // For values under 2dp, return only multiples of the value.
    return static_cast<int>(value * qFloor(densityPixelRatio_));
  } else {
    return static_cast<int>(qRound(value * densityPixelRatio_));
  }
}
