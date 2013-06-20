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

// FIXME(loicm) Desktop file loading should be executed on a dedicated I/O thread.

#include "application_manager.h"
#include "application_list_model.h"
#include "application.h"
#include "logging.h"
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

// Retrieves the size of an array at compile time.
#define ARRAY_SIZE(a) \
    ((sizeof(a) / sizeof(*(a))) / static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))

// Size of the side stage in grid units.
const int kSideStageWidth = 40;

class TaskEvent : public QEvent {
public:
    enum Task { kAddApplication = 0, kRemoveApplication, kUnfocusApplication, kFocusApplication,
                kRequestFocus, kRequestFullscreen };
    TaskEvent(char* desktopFile, int id, int stage, int task, QEvent::Type type)
        : QEvent(type)
        , desktopFile_(desktopFile)
        , id_(id)
        , stage_(stage)
        , task_(task)
    {
        DLOG("TaskEvent::TaskEvent (this=%p, desktopFile='%s', id=%d, stage=%d, task=%d, type=%d)",
             this, desktopFile, id, stage, task, type);
    }
    ~TaskEvent() {
        DLOG("TaskEvent::~TaskEvent");
        delete [] desktopFile_;
    }
    char* desktopFile_;
    int id_;
    int stage_;
    int task_;
};

// FIXME(kaleo, loicm): If we keep that keyboard geometry/visibilty API, we should integrate that
//     event type in the existing task event system.
class KeyboardGeometryEvent : public QEvent {
public:
    KeyboardGeometryEvent(QRect geometry, QEvent::Type type)
        : QEvent(type)
        , geometry_(geometry)
    {
        DLOG("KeyboardGeometryEvent::KeyboardGeometryEvent (this=%p, type=%d)", this, type);
    }

    ~KeyboardGeometryEvent()
    {
        DLOG("KeyboardGeometryEvent::~KeyboardGeometryEvent");
    }

    QRect geometry_;
};


DesktopData::DesktopData(QString desktopFile)
    : file_(desktopFile)
    , entries_(DesktopData::kNumberOfEntries, "")
{
    DLOG("DesktopData::DesktopData (this=%p, desktopFile='%s')", this, desktopFile.toLatin1().data());
    DASSERT(desktopFile != NULL);
    loaded_ = loadDesktopFile(desktopFile);
    appId_ = file_.section('/',-1);
    if (appId_.endsWith(".desktop")) {
        appId_.chop(8);
    }
}

DesktopData::~DesktopData()
{
    DLOG("DesktopData::~DesktopData");
    entries_.clear();
}

bool DesktopData::loadDesktopFile(QString desktopFile)
{
    DLOG("DesktopData::loadDesktopFile (this=%p, desktopFile='%s')",
         this, desktopFile.toLatin1().data());
    DASSERT(desktopFile != NULL);
    const struct { const char* const name; int size; unsigned int flag; } kEntryNames[] = {
        { "Name=", sizeof("Name=") - 1, 1 << DesktopData::kNameIndex },
        { "Comment=", sizeof("Comment=") - 1, 1 << DesktopData::kCommentIndex },
        { "Icon=", sizeof("Icon=") - 1, 1 << DesktopData::kIconIndex },
        { "Exec=", sizeof("Exec=") - 1, 1 << DesktopData::kExecIndex },
        { "X-Ubuntu-StageHint=", sizeof("X-Ubuntu-StageHint=") - 1, 1 << DesktopData::kStageHintIndex }
    };
    const unsigned int kAllEntriesMask =
            (1 << DesktopData::kNameIndex) | (1 << DesktopData::kCommentIndex)
            | (1 << DesktopData::kIconIndex) | (1 << DesktopData::kExecIndex)
            | (1 << DesktopData::kStageHintIndex);
    const unsigned int kMandatoryEntriesMask =
            (1 << DesktopData::kNameIndex) | (1 << DesktopData::kIconIndex)
            | (1 << DesktopData::kExecIndex);
    const int kEntriesCount = ARRAY_SIZE(kEntryNames);
    const int kBufferSize = 256;
    static char buffer[kBufferSize];
    QFile file(desktopFile);

    // Open file.
    if (!file.open(QFile::ReadOnly | QIODevice::Text)) {
        DLOG("can't open file: %s", file.errorString().toLatin1().data());
        return false;
    }

    // Validate "magic key" (standard group header).
    if (file.readLine(buffer, kBufferSize) != -1) {
        if (strncmp(buffer, "[Desktop Entry]", sizeof("[Desktop Entry]" - 1))) {
            DLOG("not a desktop file");
            return false;
        }
    }

    int length;
    unsigned int entryFlags = 0;
    while ((length = file.readLine(buffer, kBufferSize)) != -1) {
        // Skip empty lines.
        if (length > 1) {
            // Stop when reaching unsupported next group header.
            if (buffer[0] == '[') {
                DLOG("reached next group header, leaving loop");
                break;
            }
            // Lookup entries ignoring duplicates if any.
            for (int i = 0; i < kEntriesCount; i++) {
                if (!strncmp(buffer, kEntryNames[i].name, kEntryNames[i].size)) {
                    if (~entryFlags & kEntryNames[i].flag) {
                        buffer[length-1] = '\0';
                        entries_[i] = QString::fromLatin1(&buffer[kEntryNames[i].size]);
                        entryFlags |= kEntryNames[i].flag;
                        break;
                    }
                }
            }
            // Stop when matching the right number of entries.
            if (entryFlags == kAllEntriesMask) {
                break;
            }
        }
    }

    // Check that the mandatory entries are set.
    if ((entryFlags & kMandatoryEntriesMask) == kMandatoryEntriesMask) {
        DLOG("loaded desktop file with name='%s', comment='%s', icon='%s', exec='%s', stagehint='%s'",
             entries_[DesktopData::kNameIndex].toLatin1().data(),
                entries_[DesktopData::kCommentIndex].toLatin1().data(),
                entries_[DesktopData::kIconIndex].toLatin1().data(),
                entries_[DesktopData::kExecIndex].toLatin1().data(),
                entries_[DesktopData::kStageHintIndex].toLatin1().data());
        return true;
    } else {
        DLOG("not a valid desktop file, missing mandatory entries in the standard group header");
        return false;
    }
}

ApplicationManager::ApplicationManager()
    : keyboardHeight_(0)
    , keyboardVisible_(false)
    , mainStageApplications_(new ApplicationListModel())
    , sideStageApplications_(new ApplicationListModel())
    , mainStageFocusedApplication_(NULL)
    , sideStageFocusedApplication_(NULL)
    , eventType_(static_cast<QEvent::Type>(QEvent::registerEventType()))
    , keyboardGeometryEventType_(static_cast<QEvent::Type>(QEvent::registerEventType()))
{
    DLOG("ApplicationManager::ApplicationManager (this=%p)", this);
}

ApplicationManager::~ApplicationManager()
{
    DLOG("ApplicationManager::~ApplicationManager");
    delete mainStageApplications_;
    delete sideStageApplications_;
}

void ApplicationManager::customEvent(QEvent* event)
{
    DLOG("ApplicationManager::customEvent (this=%p, event=%p)", this, event);
    DASSERT(QThread::currentThread() == thread());

    // FIXME(kaleo, loicm) If we keep that keyboard geometry/visibilty API, we should integrate that
    //     event type in the existing task event system. Moreover, Qt code shouldn't use C++ RTTI
    //     (which is slow) but the Qt meta object implementation.
    KeyboardGeometryEvent* keyboardGeometryEvent = dynamic_cast<KeyboardGeometryEvent*>(event);
    if (keyboardGeometryEvent != NULL) {
        bool visible = keyboardGeometryEvent->geometry_.isValid();
        int height = keyboardGeometryEvent->geometry_.height();
        if (height != keyboardHeight_) {
            keyboardHeight_ = height;
            emit keyboardHeightChanged();
        }
        if (visible != keyboardVisible_) {
            keyboardVisible_ = visible;
            emit keyboardVisibleChanged();
        }
        return;
    }

    TaskEvent* taskEvent = static_cast<TaskEvent*>(event);
    switch (taskEvent->task_) {

    case TaskEvent::kAddApplication: {
        DLOG("handling add application task");
        break;
    }

    case TaskEvent::kRemoveApplication: {
        DLOG("handling remove application task");
        break;
    }

    case TaskEvent::kUnfocusApplication: {
        DLOG("handling unfocus application task");
        // Reset the currently focused application.
        break;
    }

    case TaskEvent::kFocusApplication: {
        DLOG("handling focus application task");
        // Update the currently focused application.
        break;
    }

    case TaskEvent::kRequestFullscreen: {
        DLOG("handling request fullscreen task");
        break;
    }

    case TaskEvent::kRequestFocus: {
        DLOG("handling request focus task");
        emit focusRequested(static_cast<FavoriteApplication>(taskEvent->id_));
        break;
    }

    default: {
        DNOT_REACHED();
        break;
    }
    }
}

int ApplicationManager::keyboardHeight() const
{
    DLOG("ApplicationManager::keyboardHeight (this=%p)", this);
    return keyboardHeight_;
}

bool ApplicationManager::keyboardVisible() const
{
    DLOG("ApplicationManager::keyboardVisible (this=%p)", this);
    return keyboardVisible_;
}

int ApplicationManager::sideStageWidth() const
{
    DLOG("ApplicationManager::sideStageWidth (this=%p)", this);
    return kSideStageWidth;
}

ApplicationListModel* ApplicationManager::mainStageApplications() const
{
    DLOG("ApplicationManager::mainStageApplications (this=%p)", this);
    return mainStageApplications_;
}

ApplicationListModel* ApplicationManager::sideStageApplications() const
{
    DLOG("ApplicationManager::sideStageApplications (this=%p)", this);
    return sideStageApplications_;
}

Application* ApplicationManager::mainStageFocusedApplication() const
{
    DLOG("ApplicationManager::mainStageFocusedApplication (this=%p)", this);
    return mainStageFocusedApplication_;
}

Application* ApplicationManager::sideStageFocusedApplication() const
{
    DLOG("ApplicationManager::sideStageFocusedApplication (this=%p)", this);
    return sideStageFocusedApplication_;
}

void ApplicationManager::focusApplication(int handle)
{
    DLOG("ApplicationManager::focusApplication (this=%p, handle=%d)", this, handle);
    // TODO(greyback)
}

void ApplicationManager::focusFavoriteApplication(
        ApplicationManager::FavoriteApplication application)
{
    DLOG("ApplicationManager::focusFavoriteApplication (this=%p, application=%d)",
         this, static_cast<int>(application));
    // TODO(greyback)
}

void ApplicationManager::unfocusCurrentApplication(ApplicationManager::StageHint stageHint)
{
    DLOG("ApplicationManager::unfocusCurrentApplication (this=%p, stageHint=%d)", this,
         static_cast<int>(stageHint));
    // TODO(greyback)
}

Application* ApplicationManager::startProcess(QString desktopFile, ApplicationManager::ExecFlags flags, QStringList arguments)
{
    DLOG("ApplicationManager::startProcess (this=%p, flags=%d)", this, (int) flags);
    // Load desktop file.
    DesktopData* desktopData = new DesktopData(desktopFile);
    if (!desktopData->loaded()) {
        delete desktopData;
        return NULL;
    }

    arguments.prepend("APP_ID=" + desktopData->appId());
    arguments.prepend("application");

    // Start process.
    bool result;

    result = QProcess::startDetached("start", arguments);

    DLOG_IF(result == false, "process 'start application' failed to start");
    if (result == true) {
        DLOG("started process, adding '%s' to application lists",
             desktopData->name().toLatin1().data());
        Application* application = new Application(
                    desktopData, Application::MainStage, Application::Running //FIXME(greyback): assuming running immediately
                    );

        if (flags.testFlag(ApplicationManager::ForceMainStage) || desktopData->stageHint() != "SideStage") {
            mainStageApplications_->add(application);
        } else {
            application->setStage(Application::SideStage);
            sideStageApplications_->add(application);
        }
        return application;
    } else {
        return NULL;
    }
}

void ApplicationManager::stopProcess(Application* application)
{
    DLOG("ApplicationManager::stopProcess (this=%p, application=%p)", this, application);

    if (application != NULL) {
        if (application->stage() == Application::MainStage) {
            mainStageApplications_->remove(application);
        } else if (application->stage() == Application::SideStage) {
            sideStageApplications_->remove(application);
        } else {
            DNOT_REACHED();
        }

        // Start process.
        bool result;

        result = QProcess::startDetached("stop", QStringList() << "application" << ("APP_ID=" + application->appId()));
        DLOG_IF(result == false, "process 'stop application' failed to start");

        delete application;
    }
}
