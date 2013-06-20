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

#ifndef APPLICATION_MANAGER_H
#define APPLICATION_MANAGER_H

#include <QtCore/QtCore>

class Application;
class ApplicationListModel;

class DesktopData {
 public:
  DesktopData(QString desktopFile);
  ~DesktopData();

  QString file() const { return file_; }
  QString appId() const { return appId_; }
  QString name() const { return entries_[kNameIndex]; }
  QString comment() const { return entries_[kCommentIndex]; }
  QString icon() const { return entries_[kIconIndex]; }
  QString exec() const { return entries_[kExecIndex]; }
  QString stageHint() const { return entries_[kStageHintIndex]; }
  bool loaded() const { return loaded_; }

 private:
  static const int kNameIndex = 0,
    kCommentIndex = 1,
    kIconIndex = 2,
    kExecIndex = 3,
    kStageHintIndex = 4,
    kNumberOfEntries = 5;

  bool loadDesktopFile(QString desktopFile);

  QString file_;
  QString appId_;
  QVector<QString> entries_;
  bool loaded_;
};

class ApplicationManager : public QObject {
  Q_OBJECT
  Q_ENUMS(Role)
  Q_ENUMS(StageHint)
  Q_ENUMS(FormFactorHint)
  Q_ENUMS(FavoriteApplication)
  Q_FLAGS(ExecFlags)
  
  // FIXME(kaleo, loicm): That keyboard API might need a cleaner design.
  Q_PROPERTY(int keyboardHeight READ keyboardHeight NOTIFY keyboardHeightChanged)
  Q_PROPERTY(bool keyboardVisible READ keyboardVisible NOTIFY keyboardVisibleChanged)

  Q_PROPERTY(int sideStageWidth READ sideStageWidth)
  Q_PROPERTY(ApplicationListModel* mainStageApplications READ mainStageApplications
             NOTIFY mainStageApplicationsChanged)
  Q_PROPERTY(ApplicationListModel* sideStageApplications READ sideStageApplications
             NOTIFY sideStageApplicationsChanged)
  Q_PROPERTY(Application* mainStageFocusedApplication READ mainStageFocusedApplication
             NOTIFY mainStageFocusedApplicationChanged)
  Q_PROPERTY(Application* sideStageFocusedApplication READ sideStageFocusedApplication
             NOTIFY sideStageFocusedApplicationChanged)

 public:
  ApplicationManager();
  ~ApplicationManager();

  // Mapping enums to Ubuntu Platform API enums.
  enum Role {
    Dash, Default, Indicators, Notifications, Greeter,
    Launcher, OnScreenKeyboard, ShutdownDialog
  };
  enum StageHint {
    MainStage, IntegrationStage, ShareStage, ContentPickingStage,
    SideStage, ConfigurationStage
  };
  enum FormFactorHint {
    DesktopFormFactor, PhoneFormFactor, TabletFormFactor
  };
  enum FavoriteApplication {
    CameraApplication, GalleryApplication, BrowserApplication, ShareApplication
  };
  enum Flag { 
    NoFlag = 0x0,
    ForceMainStage = 0x1,
  };
  Q_DECLARE_FLAGS(ExecFlags, Flag)

  // QObject methods.
  void customEvent(QEvent* event);

  int keyboardHeight() const;
  bool keyboardVisible() const;
  int sideStageWidth() const;
  ApplicationListModel* mainStageApplications() const;
  ApplicationListModel* sideStageApplications() const;
  Application* mainStageFocusedApplication() const;
  Application* sideStageFocusedApplication() const;

  Q_INVOKABLE void focusApplication(int handle);
  Q_INVOKABLE void focusFavoriteApplication(FavoriteApplication application);
  Q_INVOKABLE void unfocusCurrentApplication(StageHint stageHint);
  Q_INVOKABLE Application* startProcess(QString desktopFile, ExecFlags flags, QStringList arguments = QStringList());
  Q_INVOKABLE void stopProcess(Application* application);
  Q_INVOKABLE void startWatcher() {}

  QEvent::Type eventType() { return eventType_; }
  QEvent::Type keyboardGeometryEventType() { return keyboardGeometryEventType_; }

 Q_SIGNALS:
  void keyboardHeightChanged();
  void keyboardVisibleChanged();
  void mainStageApplicationsChanged();
  void sideStageApplicationsChanged();
  void mainStageFocusedApplicationChanged();
  void sideStageFocusedApplicationChanged();
  void focusRequested(FavoriteApplication favoriteApplication);

 private:
  int keyboardHeight_;
  bool keyboardVisible_;
  ApplicationListModel* mainStageApplications_;
  ApplicationListModel* sideStageApplications_;
  Application* mainStageFocusedApplication_;
  Application* sideStageFocusedApplication_;
  QEvent::Type eventType_;
  QEvent::Type keyboardGeometryEventType_;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ApplicationManager::ExecFlags)

Q_DECLARE_METATYPE(ApplicationManager*)

#endif  // APPLICATION_MANAGER_H
