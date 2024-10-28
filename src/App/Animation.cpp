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

#include "PreCompiled.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <QEasingCurve>

#include <Base/Tools.h>
#include <Base/Interpreter.h>
#include "Application.h"
#include "Document.h"
#include "Animation.h"

using namespace App;

FC_LOG_LEVEL_INIT("App::Animation", true,true)

/////////////////////////////////////////////////////////////////////////

const char * KeyFrame::EasingCurveTypes[] = {
    "Linear",
    "InQuad",
    "OutQuad",
    "InOutQuad",
    "OutInQuad",
    "InCubic",
    "OutCubic",
    "InOutCubic",
    "OutInCubic",
    "InQuart",
    "OutQuart",
    "InOutQuart",
    "OutInQuart",
    "InQuint",
    "OutQuint",
    "InOutQuint",
    "OutInQuint",
    "InSine",
    "OutSine",
    "InOutSine",
    "OutInSine",
    "InExpo",
    "OutExpo",
    "InOutExpo",
    "OutInExpo",
    "InCirc",
    "OutCirc",
    "InOutCirc",
    "OutInCirc",
    "InElastic",
    "OutElastic",
    "InOutElastic",
    "OutInElastic",
    "InBack",
    "OutBack",
    "InOutBack",
    "OutInBack",
    "InBounce",
    "OutBounce",
    "InOutBounce",
    "OutInBounce",
    nullptr,
};

PROPERTY_SOURCE(App::KeyFrame, App::DocumentObject)

KeyFrame::KeyFrame()
{
    static App::PropertyFloatConstraint::Constraints TimeConstraint = {
        0, std::numeric_limits<double>::max(), 0.1
    };
    static App::PropertyFloatConstraint::Constraints ProgressConstraint = {
        0, 1, 0.1
    };

    const char *grpBase = "Base";
    const char *grpEasing = "Easing";

    ADD_PROPERTY_TYPE(Duration, (1), grpBase, App::Prop_None, "Duration (in unit second) of the key frame");
    Duration.setConstraints(&TimeConstraint);

    ADD_PROPERTY_TYPE(Delay, (0), grpBase, App::Prop_None, "Delay (in unit second) starting of the key frame");
    Delay.setConstraints(&TimeConstraint);

    ADD_PROPERTY_TYPE(AnimationGroup, (nullptr), grpBase, App::Prop_Hidden, "Parent animation group");

    ADD_PROPERTY_TYPE(NextFrame, (nullptr), grpBase, App::Prop_None, "Next frame to jump to");

    ADD_PROPERTY_TYPE(Jump, (false), grpBase, App::Prop_None, "Whether to jump to next frame");

    ADD_PROPERTY_TYPE(Progress, (0), grpBase, App::Prop_None, "Current progress (0 ~ 1) of this key frame animation");
    Progress.setConstraints(&ProgressConstraint);

    ADD_PROPERTY_TYPE(Trigger, ((long)0), grpBase, App::Prop_None, "Key frame triggering type");
    static const char* TriggerEnums[] = {"After previous",
                                         "With previous",
                                         "On click",
                                         nullptr};
    Trigger.setEnums(TriggerEnums);

    ADD_PROPERTY_TYPE(Recording, (false), grpBase, App::Prop_NoPersist, "Recording property changes to this key frame");
    ADD_PROPERTY_TYPE(Playing, (false), grpBase, App::Prop_NoPersist, "Playing animation");

    ADD_PROPERTY_TYPE(EasingCurve, ((long)0), grpEasing, App::Prop_None, "Type of the easing curve");
    EasingCurve.setEnums(EasingCurveTypes);

    ADD_PROPERTY_TYPE(EasingPeriod, (0.3), grpEasing, App::Prop_None, "Easing curve period.\nOnly applicable to some type of easing curve");

    ADD_PROPERTY_TYPE(EasingAmplitude, (1.0), grpEasing, App::Prop_None, "Easing curve amplitude.\nOnly applicable to some type of easing curve");

    ADD_PROPERTY_TYPE(EasingOvershoot, (1.70158), grpEasing, App::Prop_None, "Easing curve overshoot.\nOnly applicable to some type of easing curve");

    ADD_PROPERTY_TYPE(UserEasingCurve, (), grpEasing, App::Prop_None, "User defined easing curve callable that can map input from range [0, 1] to [0, 1]");
}

KeyFrame::~KeyFrame()
{
}

std::string KeyFrame::encodePropertyName(const Document *owner, const Property &prop)
{
    auto container = prop.getContainer();
    if (!container)
        return std::string();
    auto doc = container->getOwnerDocument();
    if (!doc)
        return std::string();

    std::vector<std::string> tokens;
    boost::split(tokens, prop.getFullName(), boost::is_any_of("."));
    auto itToken = tokens.begin();

    std::ostringstream oss;
    if (owner == doc) {
        oss << "_";
        ++itToken;
    }
    for (; itToken != tokens.end(); ++itToken) {
        oss << "_" << itToken->size() << *itToken;
    }
    return oss.str();
}

std::vector<std::string> KeyFrame::_decodePropertyName(const Document *&owner, const char *name)
{
    std::vector<std::string> tokens;
    if (!boost::starts_with(name, "_"))
        return {};
    size_t total = strlen(name);
    if (boost::starts_with(name, "__")) {
        if (!owner)
            return {};
        ++name;
        --total;
    }
    else
        owner = nullptr;
    while (name[0]) {
        if (name[0] != '_')
            return {};
        ++name;
        --total;
        size_t len = 0;
        for (; name[0] >= '0' && name[0] <= '9'; ++name) {
            len = len*10 + (name[0] - '0');
            --total;
        }
        if (len > total)
            return {};
        if (owner == nullptr) {
            owner = GetApplication().getDocument(std::string(name, len).c_str());
            if (!owner)
                return {};
            continue;
        }
        tokens.push_back(std::string(name, len));
    }
    return tokens;
}

Property *KeyFrame::decodePropertyName(const Document *owner, const char *name)
{
    auto tokens = _decodePropertyName(owner, name);
    if (tokens.size() < 2)
        return nullptr;
    DocumentObject *obj = owner->getObject(tokens.front().c_str());
    if (!obj)
        return nullptr;
    return obj->getPropertyByName(tokens[1].c_str());
}

void KeyFrame::onChanged(const Property* prop)
{
    auto animation = Base::freecad_dynamic_cast<Animation>(AnimationGroup.getValue());
    if(!animation
            || isRestoring()
            || !getDocument()
            || getDocument()->isPerformingTransaction()) {
        inherited::onChanged(prop);
        return;
    }

    if (prop == &Duration) {
        double duration = 0;
        for (auto frame : animation->Frames.getValues()) {
            if (auto keyframe = Base::freecad_dynamic_cast<KeyFrame>(frame))
                duration += keyframe->Duration.getValue();
        }
        animation->Duration.setValue(duration);
    }
    else if (prop == &Progress
            && !prop->testStatus(Property::User3)
            && Playing.getValue()) {
        float t = Progress.getValue();
        if (t < 0.f)
            t = 0.f;
        else if (t > 1.f)
            t = 1.f;
        int index = -1;
        animation->Frames.find(getNameInDocument(), &index);
        if (index >= 0) {
            Base::ObjectStatusLocker<Property::Status, Property> guard(Property::User3, &animation->Progress);
            animation->Progress.setValue((static_cast<double>(index) + t)/animation->Frames.getSize());
        }
        
        KeyFrame *prev = index == 0 ? animation
            : Base::freecad_dynamic_cast<KeyFrame>(animation->Frames[index-1]);
        if (prev) {
            if (UserEasingCurve.getPyObject() != Py_None) {
                Base::PyGILStateLocker locker;
                Py::Object pyObj = UserEasingCurve.getValue();
                if (!pyObj.isCallable()) {
                    FC_THROWM(Base::RuntimeError,
                            "Expects UserEasingCurve to be of type callable(Float) -> Float");
                }
                try {
                    Py::Callable callable(pyObj);
                    Py::Object res = callable.apply(Py::TupleN(Py::Float(t)));
                    if (!res.isNumeric())
                        FC_THROWM(Base::TypeError, "Expects UserEasingCurve to return float");
                    t = Py::Float(res);
                } catch (Py::Exception &) {
                    Base::PyException::ThrowException();
                }
            }
            else {
                QEasingCurve curve(static_cast<QEasingCurve::Type>(EasingCurve.getValue()));
                curve.setAmplitude(EasingAmplitude.getValue());
                curve.setPeriod(EasingPeriod.getValue());
                curve.setOvershoot(EasingOvershoot.getValue());
                t = curve.valueForProgress(t);
            }
            std::vector<Property*> props;
            dynamicProps.getPropertyList(props);
            for (auto thisProp : props) {
                auto objProp = decodePropertyName(getDocument(), thisProp->getName());
                if (!objProp)
                    continue;
                auto prevProp = prev->getPropertyByName(thisProp->getName());
                if (!prevProp)
                    continue;
                objProp->interpolate(*prevProp, *thisProp, t);
            }
            if (!animation->NoRecompute.getValue()) {
                getDocument()->recompute({}, true);
            }
        }
    }

    inherited::onChanged(prop);
}

void KeyFrame::beforeChangeProperty(const Property &prop)
{
    auto parent = Base::freecad_dynamic_cast<Animation>(AnimationGroup.getValue());
    if (!parent) {
        FC_ERR("Orphan key frame");
        return;
    }
    std::string name = encodePropertyName(getDocument(), prop);
    if (name.empty())
        return;

    if (parent->getPropertyByName(name.c_str()))
        return;

    auto newProp = parent->addDynamicProperty(
            prop.getTypeId().getName(), name.c_str(), "Original", nullptr, Prop_Hidden);
    if (newProp) {
        FC_ERR("Failed to record property " << prop.getFullName() << " to key frame");
        newProp->Paste(prop);
    }
}

void KeyFrame::onChangeProperty(const Property &prop)
{
    std::string name = encodePropertyName(getDocument(), prop);
    if (name.empty())
        return;

    auto newProp = getPropertyByName(name.c_str());
    if (!newProp) {
        newProp = addDynamicProperty(
                prop.getTypeId().getName(), name.c_str(), "Storage", nullptr, Prop_Hidden);
        if (!newProp) {
            FC_ERR("Failed to record property " << prop.getFullName() << " to key frame");
            return;
        }
    }
    newProp->Paste(prop);
}

bool KeyFrame::next(double elapsed, Options options)
{
    if (!Playing.getValue())
        return false;
    
    auto animation = Base::freecad_dynamic_cast<Animation>(AnimationGroup.getValue());
    if (animation && animation->AutoSkipFrame.getValue()) {
        if (animation->FrameRate.getValue() > 0)
            elapsed = 1000. / animation->FrameRate.getValue();
    }
    
    if (Duration.getValue() > 0) {
        elapsed /= Duration.getValue();
        if (elapsed <= 1.0) {
            Progress.setValue(elapsed);
            return true;
        }
    }

    Playing.setValue(false);

    if (!animation)
        return false;
    return animation->next(Duration.getValue() + elapsed);
}

////////////////////////////////////////////////////////////////////////////////////////

PROPERTY_SOURCE(App::Animation, App::KeyFrame)

Animation::Animation()
{
    static App::PropertyFloatConstraint::Constraints FrameRateConstraint = {
        0, std::numeric_limits<double>::max(), 1
    };
    const char *grpAnimation = "Animation";
    ADD_PROPERTY_TYPE(FrameRate, (10), grpAnimation, App::Prop_None, "Type of the easing curve");
    FrameRate.setConstraints(&FrameRateConstraint);

    ADD_PROPERTY_TYPE(AutoSkipFrame, (false), grpAnimation, App::Prop_None, "Auto skip frame and honour time duration.");

    ADD_PROPERTY_TYPE(NoRecompute, (false), grpAnimation, App::Prop_None, "Do not recompute on each frame.");

    ADD_PROPERTY_TYPE(ExcludeProperties, (), grpAnimation, App::Prop_None, "Name of the properties to be excluded when recording key frame");

    ADD_PROPERTY_TYPE(CacheProperties, (), grpAnimation, App::Prop_None, "Name of the properties to be cached for acceleration");

    ADD_PROPERTY_TYPE(EnableCache, (false), grpAnimation, App::Prop_None, "Enable property cache");

    ADD_PROPERTY_TYPE(Frames, (), grpAnimation, App::Prop_Hidden, "Child key frames");

    Recording.setStatus(Property::Status::Hidden, true);
    Recording.setStatus(Property::Status::Transient, true);
    Duration.setStatus(Property::Status::ReadOnly, true);
}

Animation::~Animation()
{
}

KeyFrame *Animation::getFrameByProgress(double &t, int *pindex)
{
    t = Progress.getValue();
    if (t < 0)
        t = 0;
    else if (t > 1)
        t = 1;
    t *= Frames.getSize();
    int index = static_cast<int>(t);
    if (index == Frames.getSize()) {
        --index;
        t = 1;
    }
    else if (index >= 0) {
        t = (t - index) * Frames.getSize();
    }
    else {
        index = 0;
        t = 0;
    }
    if (pindex)
        *pindex = index;
    return Base::freecad_dynamic_cast<KeyFrame>(Frames[index]);
}

void Animation::onChanged(const Property* prop)
{
    if(isRestoring() || !getDocument()
                     || getDocument()->isPerformingTransaction()) {
        inherited::onChanged(prop);
        return;
    }

    if (prop == &Progress) {
        if (!prop->testStatus(Property::User3) && Playing.getValue()) {
            double t;
            if (auto keyFrame = getFrameByProgress(t)) {
                Base::ObjectStatusLocker<Property::Status,Property>
                    guard(Property::User3, &keyFrame->Progress);
                if (!keyFrame->Playing.getValue()) {
                    for (auto obj : Frames.getValues()) {
                        if (auto frame = Base::freecad_dynamic_cast<KeyFrame>(obj)) {
                            frame->Playing.setValue(false);
                        }
                    }
                    keyFrame->Playing.setValue(true);
                }
                keyFrame->Progress.setValue(t);
            }
        }
    }
    else if (prop == &Recording) {
        if (Recording.getValue()) {
            Recording.setValue(false);
            newFrame("App::KeyFrame", "KeyFrame");
        }
    }
    else {
        inherited::onChanged(prop);
        return;
    }

    // skip KeyFrame::onChanged() handling
    DocumentObject::onChanged(prop);
}

void Animation::newFrame(const char *type, const char *name, bool record)
{
    if (auto frame = Base::freecad_dynamic_cast<KeyFrame>(getDocument()->addObject("App::KeyFrame", "KeyFrame"))) {
        frame->AnimationGroup.setValue(this);
        frame->EasingCurve.setValue(EasingCurve.getValue());
        frame->UserEasingCurve.setValue(UserEasingCurve.getValue());
        frame->EasingAmplitude.setValue(EasingAmplitude.getValue());
        frame->EasingPeriod.setValue(EasingPeriod.getValue());
        frame->EasingOvershoot.setValue(EasingOvershoot.getValue());
        frame->Trigger.setValue(Trigger.getValue());
        Frames.set1Value(-1, frame);
        if (record)
            frame->Recording.setValue(true);
    }
}

bool Animation::next(double elapsed, Options options)
{
    if (!Playing.getValue())
        return false;

    double t;
    while (auto keyFrame = getFrameByProgress(t)) {
        double duration = elapsed + t * keyFrame->Duration.getValue();
        if (duration <= keyFrame->Duration.getValue()) {
            keyFrame->Playing.setValue(true);
            return keyFrame->next(duration/keyFrame->Duration.getValue());
        }
        keyFrame->Playing.setValue(false);
        elapsed -= keyFrame->Duration.getValue() * (1.0 - t);

        t = Progress.getValue();
        t += 1.0/Frames.getSize();

        if (keyFrame->Jump.getValue()) {
            if (auto frame = Base::freecad_dynamic_cast<KeyFrame>(
                        keyFrame->NextFrame.getValue())) {
                int index = -1;
                if (Frames.find(frame->getNameInDocument(), &index)) {
                    t = static_cast<double>(index) / Frames.getSize();
                }
            }
            else
                break;
        }
        Base::ObjectStatusLocker<Property::Status,Property> guard(Property::User3, &Progress);
        Progress.setValue(t);
    }

    Playing.setValue(false);
    if (auto animation = Base::freecad_dynamic_cast<Animation>(AnimationGroup.getValue()))
        return animation->next(elapsed + Duration.getValue());
    return false;
}
