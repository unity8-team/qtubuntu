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
#include <QCoreApplication>
#include <QMutexLocker>
#include <QDebug>

bool PluggableInputFilter::filterKeyEvent(QKeyEvent *event)
{
    qDebug() << "Filter!!" << event->key() << event->modifiers();

    const QMutexLocker locker(&m_mutex);
    bool filtered = false;
    auto i = m_filters.constFind(event->key());
    while (i != m_filters.constEnd() && i.key() == event->key()) {
        QCoreApplication::instance()->postEvent(const_cast<QObject*>(i.value()), event); // push event onto event loop of the QObject
        filtered = true;
        ++i;
    }
    return filtered;
}

bool PluggableInputFilter::installKeyEventFilterObject(const Qt::Key key, const QObject *filterObject)
{
//    QObject::connect(filterObject, &QObject::destroyed, this, &PluggableInputFilter::removeKeyEventFilterObject,
//                     Qt::DirectConnection); // if no QThread, this will probably fail
    const QMutexLocker locker(&m_mutex);

    m_filters.insert(key, filterObject);
    return true;
}

bool PluggableInputFilter::removeKeyEventFilterObject(const QObject *filterObject)
{
    const QMutexLocker locker(&m_mutex);
    bool removed = false;
    auto i = m_filters.constBegin();
    while (i != m_filters.constEnd()) {
        if (i.value() == filterObject) {
            m_filters.remove(i.key(), i.value());
            removed = true;
        }
    }

    return removed;
}
