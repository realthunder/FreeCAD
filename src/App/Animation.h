/****************************************************************************
 *   Copyright (c) 2024 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef APP_ANIMATION_H
#define APP_ANIMATION_H

#include <boost_signals2.hpp>

#include <Base/Bitmask.h>
#include "DocumentObject.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"

namespace App
{

class AppExport KeyFrame: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::KeyFrame);
    typedef App::DocumentObject inherited;

public:
    static const char *EasingCurveTypes[];
    static constexpr const char *ActiveKey = "KeyFrame";

    App::PropertyFloatConstraint Duration;
    App::PropertyFloatConstraint Delay;
    App::PropertyLinkHidden AnimationGroup;
    App::PropertyLink NextFrame;
    App::PropertyBool Jump;
    App::PropertyFloatConstraint Progress;
    App::PropertyEnumeration EasingCurve;
    App::PropertyFloat EasingPeriod;
    App::PropertyFloat EasingOvershoot;
    App::PropertyFloat EasingAmplitude;
    App::PropertyPythonObject UserEasingCurve;
    App::PropertyEnumeration Trigger;
    App::PropertyBool Recording;
    App::PropertyBool Playing;

    KeyFrame();
    ~KeyFrame() override;

    static std::string encodePropertyName(const Document *owner, const Property &prop);
    static Property *decodePropertyName(const Document *owner, const char *name);
    static std::vector<std::string> _decodePropertyName(const Document *&owner, const char *name);

    enum class Option {
        NextFrame = 1,
        NextKeyFrame = 2,
        PrevFrame = 4,
        PrevKeyFrame = 8,
    };
    typedef Base::Flags<Option> Options;

    virtual bool next(double elapsed, Options options = Options());

    void onChanged(const Property* prop) override;
    void beforeChangeProperty(const Property &);
    void onChangeProperty(const Property &);

    const char* getViewProviderName(void) const override{
        return "Gui::ViewProviderKeyFrame";
    }
};

class AppExport Animation : public App::KeyFrame
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Animation);
    typedef App::KeyFrame inherited;

public:
    App::PropertyFloatConstraint FrameRate;
    App::PropertyBool AutoSkipFrame;
    App::PropertyBool NoRecompute;
    App::PropertyStringList ExcludeProperties;
    App::PropertyStringList CacheProperties;
    App::PropertyBool EnableCache;
    App::PropertyLinkList Frames;

    Animation();
    ~Animation() override;

    bool next(double elapsed, Options options = Options()) override;

    void onChanged(const Property* prop) override;

    KeyFrame *getFrameByProgress(double &t, int *pindex = nullptr);

    const char* getViewProviderName(void) const override{
        return "Gui::ViewProviderAnimation";
    }

protected:
    void newFrame(const char *type, const char *name, bool record=true);
};

}

#endif //APP_ANIMATION_H
