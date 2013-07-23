#!/bin/bash -i

# Overwrite binaries.
adb push src/platforms/ubuntu/libqubuntu.so /usr/lib/arm-linux-gnueabihf/qt5/plugins/platforms
adb push src/platforms/ubuntulegacy/libqubuntulegacy.so /usr/lib/arm-linux-gnueabihf/qt5/plugins/platforms
adb push src/modules/application/libubuntuapplicationplugin.so /usr/lib/arm-linux-gnueabihf/qt5/imports/Ubuntu/Application
adb push src/modules/application/qmldir /usr/lib/arm-linux-gnueabihf/qt5/imports/Ubuntu/Application

# Push test files in /home/phablet/qtubuntu.
adb shell "mkdir -p /home/phablet/qtubuntu/ && chown phablet:phablet /home/phablet/qtubuntu/ && chmod 755 /home/phablet/qtubuntu/"
adb push tests/qmlscene_ubuntu/qmlscene-ubuntu /home/phablet/qtubuntu
adb push tests/clipboard/clipboard /home/phablet/qtubuntu
adb push tests/Logo.qml /home/phablet/qtubuntu
adb push tests/MovingLogo.qml /home/phablet/qtubuntu
adb push tests/WarpingLogo.qml /home/phablet/qtubuntu
adb push tests/Input.qml /home/phablet/qtubuntu
adb push tests/Application.qml /home/phablet/qtubuntu
adb push tests/Fullscreen.qml /home/phablet/qtubuntu
adb push tests/logo.png /home/phablet/qtubuntu
adb push tests/noise.png /home/phablet/qtubuntu
adb push tests/inject_key.py /home/phablet/qtubuntu
