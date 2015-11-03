/*
 * Copyright (C) 2015 Canonical, Ltd.
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

#include "screenobserver.h"
#include "screen.h"
#include "window.h"
#include "logging.h"

// Qt
#include <QMetaObject>
#include <QPointer>
#include <private/qwindow_p.h>

// Mir
#include <mirclient/mir_toolkit/mir_connection.h>

#include <memory>

namespace {
    static void displayConfigurationChangedCallback(MirConnection */*connection*/, void* context)
    {
        DASSERT(context != NULL);
        UbuntuScreenObserver *observer = static_cast<UbuntuScreenObserver *>(context);
        QMetaObject::invokeMethod(observer, "update");
    }
} // anonymous namespace

UbuntuScreenObserver::UbuntuScreenObserver(MirConnection *mirConnection)
    : mMirConnection(mirConnection)
{
    mir_connection_set_display_config_change_callback(mirConnection, ::displayConfigurationChangedCallback, this);
    update();
}

void UbuntuScreenObserver::update()
{
    // Wrap MirDisplayConfiguration to always delete when out of scope
    auto configDeleter = [](MirDisplayConfiguration *config) { mir_display_config_destroy(config); };
    using configUp = std::unique_ptr<MirDisplayConfiguration, decltype(configDeleter)>;
    configUp displayConfig(mir_connection_create_display_config(mMirConnection), configDeleter);

    // Mir only tells us something changed, it is up to us to figure out what.
    QList<UbuntuScreen*> newScreenList;
    QList<UbuntuScreen*> oldScreenList = mScreenList;
    mScreenList.clear();

    for (uint32_t i=0; i<displayConfig->num_outputs; i++) {
        MirDisplayOutput output = displayConfig->outputs[i];
        if (output.used && output.connected) {
            UbuntuScreen *screen = findScreenWithId(oldScreenList, output.output_id);
            if (screen) { // we've already set up this display before, refresh its internals
                screen->setMirDisplayOutput(output);
                oldScreenList.removeAll(screen);
            } else {
                // new display, so create UbuntuScreen for it
                screen = new UbuntuScreen(output);
                newScreenList.append(screen);
                qDebug() << "Added Screen with id" << output.output_id << "and geometry" << screen->geometry();
            }
            mScreenList.append(screen);
        }
    }

    // Delete any old & unused Screens
    foreach (const auto screen, oldScreenList) {
        qDebug() << "Removed Screen with id" << screen->outputId() << "and geometry" << screen->geometry();
        // The screen is automatically removed from Qt's internal list by the QPlatformScreen destructor.
        delete screen;
    }

    foreach (const auto screen, newScreenList) {
        Q_EMIT screenAdded(screen);
    }
}

UbuntuScreen *UbuntuScreenObserver::findScreenWithId(const QList<UbuntuScreen *> &list, const uint32_t id)
{
    foreach (const auto screen, list) {
        if (screen->outputId() == id) {
            return screen;
        }
    }
    return nullptr;
}

void UbuntuScreenObserver::windowScreenDataChanged(const QPointer<UbuntuWindow> &window, int dpi,
                                                   MirFormFactor formFactor, float scale)
{
    // The Mir server has told us that a surface has been moved to a different Screen, or some properties of the
    // Screen has changed. Qt really wants to know what Screen the Window is on, but Mir does not give us that
    // information. The information it does give us has no intersection with Screen data either.
    //
    // So to satisfy Qt requirements, we need to select a Screen and set this information on it. And
    // do so in such a way the other Windows are not impacted.
    if (!window) {
        return;
    }

    bool recreateWindow = false;
    UbuntuScreen *screen = nullptr;
    const int screenCount = mScreenList.length();

    if (screenCount == 0) { // no screens?!
        qDebug() << "No screens to update";
        return;
    } else if (screenCount == 1) { // just 1 screen on the system, no need to guess
        screen = static_cast<UbuntuScreen*>(window->screen());
        if (!qFuzzyCompare(screen->scale(), scale)) { // if scale changes, force window recreation
            recreateWindow = true;
        }
        screen->setMirDisplayProperties(dpi, formFactor, scale);
    } else {

    }

    // Need to poke the window to be recreated (must be done after Screen updated). Use QWindowPrivate
    // methods to avoid deleting/recreating Screens when using QWindowSystemInterface::handleWindowScreenChanged.
    if (recreateWindow) {
        const auto w = static_cast<QWindowPrivate *>(QObjectPrivate::get(window->window()));
        w->disconnectFromScreen(); // sets window has having no screen
        w->setTopLevelScreen(screen->screen(), true); // re-sets window's screen, forcing re-creation
    }
}
