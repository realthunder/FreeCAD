/***************************************************************************
 *   Copyright (c) 2010 Thomas Anderson <ta@nextgenengineering>            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include <iomanip>
#include <sstream>

#include <QMainWindow>
#include <FCConfig.h>
#include "Application.h"
#include "GuiApplicationNativeEventAware.h"
#include "SpaceballEvent.h"
#include "SpaceballParams.h"


#if defined(_USE_3DCONNEXION_SDK) || defined(SPNAV_FOUND)
#if defined(Q_OS_LINUX)
  #if defined(SPNAV_USE_X11)
    #include "3Dconnexion/GuiNativeEventLinuxX11.h"
  #else
    #include "3Dconnexion/GuiNativeEventLinux.h"
  #endif
#elif defined(Q_OS_WIN)
  #include "3Dconnexion/GuiNativeEventWin32.h"
#elif defined(Q_OS_MACX)
  #if defined(SPNAV_FOUND)
    #include "3Dconnexion/GuiNativeEventLinux.h"
  #else
    #include "3Dconnexion/GuiNativeEventMac.h"
  #endif
#endif // Platform switch
#endif // Spacemice

Gui::GUIApplicationNativeEventAware::GUIApplicationNativeEventAware(int &argc, char *argv[]) :
        QApplication (argc, argv), spaceballPresent(false)
{
#if defined(_USE_3DCONNEXION_SDK) || defined(SPNAV_FOUND)
    nativeEvent = new Gui::GuiNativeEvent(this);
#endif
}

Gui::GUIApplicationNativeEventAware::~GUIApplicationNativeEventAware() = default;

void Gui::GUIApplicationNativeEventAware::initSpaceball(QMainWindow *window)
{
#if defined(_USE_3DCONNEXION_SDK) || defined(SPNAV_FOUND)
    nativeEvent->initSpaceball(window);
#else
    Q_UNUSED(window);
#endif
    Spaceball::MotionEvent::MotionEventType = QEvent::registerEventType();
    Spaceball::ButtonEvent::ButtonEventType = QEvent::registerEventType();
}

bool Gui::GUIApplicationNativeEventAware::processSpaceballEvent(QObject *object, QEvent *event)
{
    if (!activeWindow()) {
        qDebug("No active window\n");
        return true;
    }

    QApplication::notify(object, event);
    if (event->type() == Spaceball::MotionEvent::MotionEventType)
    {
        auto motionEvent = dynamic_cast<Spaceball::MotionEvent*>(event);
        if (!motionEvent)
            return true;
        if (!motionEvent->isHandled())
        {
            //make a new event and post to parent.
            auto newEvent = new Spaceball::MotionEvent(*motionEvent);
            postEvent(object->parent(), newEvent);
        }
    }

    if (event->type() == Spaceball::ButtonEvent::ButtonEventType)
    {
        auto buttonEvent = dynamic_cast<Spaceball::ButtonEvent*>(event);
        if (!buttonEvent)
            return true;
        if (!buttonEvent->isHandled())
        {
            //make a new event and post to parent.
            auto newEvent = new Spaceball::ButtonEvent(*buttonEvent);
            postEvent(object->parent(), newEvent);
        }
    }
    return true;
}

void Gui::GUIApplicationNativeEventAware::postMotionEvent(std::vector<int> motionDataArray)
{
    auto currentWidget(focusWidget());
    if (!currentWidget) {
        return;
    }
    importSettings(motionDataArray);

    auto motionEvent = new Spaceball::MotionEvent();
    motionEvent->setTranslations(motionDataArray[0], motionDataArray[1], motionDataArray[2]);
    motionEvent->setRotations(motionDataArray[3], motionDataArray[4], motionDataArray[5]);
    this->postEvent(currentWidget, motionEvent);
}

void Gui::GUIApplicationNativeEventAware::postButtonEvent(int buttonNumber, int buttonPress)
{
    auto currentWidget(focusWidget());
    if (!currentWidget) {
        return;
    }

    auto buttonEvent = new Spaceball::ButtonEvent();
    buttonEvent->setButtonNumber(buttonNumber);
    if (buttonPress)
    {
      buttonEvent->setButtonStatus(Spaceball::BUTTON_PRESSED);
    }
    else
    {
      buttonEvent->setButtonStatus(Spaceball::BUTTON_RELEASED);
    }
    this->postEvent(currentWidget, buttonEvent);
}

float Gui::GUIApplicationNativeEventAware::convertPrefToSensitivity(int value)
{
    if (value < 0)
    {
        return ((0.9/50)*float(value) + 1);
    }
    else
    {
        return ((2.5/50)*float(value) + 1);
    }
}

void Gui::GUIApplicationNativeEventAware::importSettings(std::vector<int>& motionDataArray)
{
    // Remapping of motion data
    long remap = SpaceballParams::getRemapping();
    if (remap != 12345) {
        std::stringstream s;
        s << std::setfill('0') << std::setw(6) << remap;

        std::string str;
        s >> str;

        // the string must have a length of 6 and it must contain all digits 0,...,5
        std::string::size_type pos1 = str.find_first_not_of("012345");
        std::string::size_type pos2 = std::string("012345").find_first_not_of(str);
        if (pos1 == std::string::npos && pos2 == std::string::npos) {
            std::vector<int> vec(str.size());
            std::transform(str.begin(), str.end(), vec.begin(), [](char c) -> int { return c - '0';});

            std::vector<int> copy = motionDataArray;
            for (int i=0; i<6; i++) {
                motionDataArray[i] = copy[vec[i]];
            }
        }
    }

    // The settings of the Spaceball Motion page (SpaceballParams)
    bool  dominant           = SpaceballParams::getDominant();
    bool  flipXY             = SpaceballParams::getFlipYZ();
    float generalSensitivity = convertPrefToSensitivity(SpaceballParams::getGlobalSensitivity());

    // array that has stored info about "Enabled" checkboxes of all axes
    bool translations = SpaceballParams::getTranslations();
    bool rotations = SpaceballParams::getRotations();
    bool enabled[6];
    enabled[0] = translations && SpaceballParams::getPanLREnable();
    enabled[1] = translations && SpaceballParams::getPanUDEnable();
    enabled[2] = translations && SpaceballParams::getZoomEnable();
    enabled[3] = rotations && SpaceballParams::getTiltEnable();
    enabled[4] = rotations && SpaceballParams::getRollEnable();
    enabled[5] = rotations && SpaceballParams::getSpinEnable();

    // array that has stored info about "Reversed" checkboxes of all axes
    bool  reversed[6];
    reversed[0] = SpaceballParams::getPanLRReverse();
    reversed[1] = SpaceballParams::getPanUDReverse();
    reversed[2] = SpaceballParams::getZoomReverse();
    reversed[3] = SpaceballParams::getTiltReverse();
    reversed[4] = SpaceballParams::getRollReverse();
    reversed[5] = SpaceballParams::getSpinReverse();

    // array that has stored info about sliders - on each slider you need to use method DlgSpaceballSettings::GetValuefromSlider
    // which will convert <-50, 50> linear integers from slider to <0.1, 10> exponential floating values
    float sensitivity[6];
    sensitivity[0] = convertPrefToSensitivity(SpaceballParams::getPanLRSensitivity());
    sensitivity[1] = convertPrefToSensitivity(SpaceballParams::getPanUDSensitivity());
    sensitivity[2] = convertPrefToSensitivity(SpaceballParams::getZoomSensitivity());
    sensitivity[3] = convertPrefToSensitivity(SpaceballParams::getTiltSensitivity());
    sensitivity[4] = convertPrefToSensitivity(SpaceballParams::getRollSensitivity());
    sensitivity[5] = convertPrefToSensitivity(SpaceballParams::getSpinSensitivity());

    if (SpaceballParams::getCalibrate())
    {
        SpaceballParams::setCalibrationX(motionDataArray[0]);
        SpaceballParams::setCalibrationY(motionDataArray[1]);
        SpaceballParams::setCalibrationZ(motionDataArray[2]);
        SpaceballParams::setCalibrationXr(motionDataArray[3]);
        SpaceballParams::setCalibrationYr(motionDataArray[4]);
        SpaceballParams::setCalibrationZr(motionDataArray[5]);

        SpaceballParams::removeCalibrate();

        return;
    }
    else
    {
        motionDataArray[0] = motionDataArray[0] - SpaceballParams::getCalibrationX();
        motionDataArray[1] = motionDataArray[1] - SpaceballParams::getCalibrationY();
        motionDataArray[2] = motionDataArray[2] - SpaceballParams::getCalibrationZ();
        motionDataArray[3] = motionDataArray[3] - SpaceballParams::getCalibrationXr();
        motionDataArray[4] = motionDataArray[4] - SpaceballParams::getCalibrationYr();
        motionDataArray[5] = motionDataArray[5] - SpaceballParams::getCalibrationZr();
    }

    int i;

    if (flipXY) {
        bool  tempBool;
        float tempFloat;

        tempBool   = enabled[1];
        enabled[1] = enabled[2];
        enabled[2] = tempBool;

        tempBool   = enabled[4];
        enabled[4] = enabled[5];
        enabled[5] = tempBool;


        tempBool    = reversed[1];
        reversed[1] = reversed[2];
        reversed[2] = tempBool;

        tempBool    = reversed[4];
        reversed[4] = reversed[5];
        reversed[5] = tempBool;


        tempFloat      = sensitivity[1];
        sensitivity[1] = sensitivity[2];
        sensitivity[2] = tempFloat;

        tempFloat      = sensitivity[4];
        sensitivity[4] = sensitivity[5];
        sensitivity[5] = tempFloat;


        i = motionDataArray[1];
        motionDataArray[1] = motionDataArray[2];
        motionDataArray[2] = - i;

        i = motionDataArray[4];
        motionDataArray[4] = motionDataArray[5];
        motionDataArray[5] = - i;
    }

    if (dominant) { // if dominant is checked
        int max = 0;
        bool flag = false;
        for (i = 0; i < 6; ++i) {
            if (abs(motionDataArray[i]) > abs(max)) max = motionDataArray[i];
        }
        for (i = 0; i < 6; ++i) {
            if ((motionDataArray[i] != max) || (flag)) {
                motionDataArray[i] = 0;
            } else if (motionDataArray[i] == max) {
                flag = true;
            }
        }
    }

    for (i = 0; i < 6; ++i) {
        if (motionDataArray[i] != 0) {
            if (!enabled[i])
                motionDataArray[i] = 0;
            else {
                if (reversed[i])
                    motionDataArray[i] = - motionDataArray[i];
                motionDataArray[i] = (int)((float)(motionDataArray[i]) * sensitivity[i] * generalSensitivity);
            }
        }
    }
}

#include "moc_GuiApplicationNativeEventAware.cpp"
