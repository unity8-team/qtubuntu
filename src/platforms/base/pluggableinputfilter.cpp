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

#include "pluggableinputfilter.h"
#include <QKeyEvent>
#include <QDebug>

PluggableInputFilter::PluggableInputFilter()
{
}

bool PluggableInputFilter::filterEvent(const QEvent *event)
{
    const QKeyEvent *ev = reinterpret_cast<const QKeyEvent*>(event);
    qDebug() << "Filter!!" << ev->key() << ev->modifiers();
    return true;
}
