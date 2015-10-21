/*
 * Copyright (C) 2014 Canonical, Ltd.
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
 */

#ifndef UBUNTU_SCREEN_H
#define UBUNTU_SCREEN_H

#include <qpa/qplatformscreen.h>
#include <QSurfaceFormat>

struct MirDisplayOutput;

class UbuntuScreen : public QObject, public QPlatformScreen
{
    Q_OBJECT
public:
    explicit UbuntuScreen(const MirDisplayOutput &output);
    virtual ~UbuntuScreen();

    // QPlatformScreen methods.
    QImage::Format format() const override { return mFormat; }
    int depth() const override { return mDepth; }
    QRect geometry() const override { return mGeometry; }
    QRect availableGeometry() const override { return mGeometry; }
    QSizeF physicalSize() const override { return mPhysicalSize; }
    qreal refreshRate() const override { return mRefreshRate; }
    Qt::ScreenOrientation nativeOrientation() const override { return mNativeOrientation; }
    Qt::ScreenOrientation orientation() const override { return mNativeOrientation; }

    // New methods.
    void handleWindowSurfaceResize(int width, int height);
    void setMirDisplayOutput(const MirDisplayOutput &output);
    uint32_t outputId() const { return mOutputId; }

    // QObject methods.
    void customEvent(QEvent* event);

private:
    QRect mGeometry;
    Qt::ScreenOrientation mNativeOrientation;
    Qt::ScreenOrientation mCurrentOrientation;
    QImage::Format mFormat;
    int mDepth;
    uint32_t mOutputId;
    QSizeF mPhysicalSize;
    qreal mRefreshRate;
};

#endif // UBUNTU_SCREEN_H
