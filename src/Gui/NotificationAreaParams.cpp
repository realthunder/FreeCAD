/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

/*[[[cog
import NotificationAreaParams
NotificationAreaParams.define()
]]]*/

// Auto generated code (Tools/params_utils.py:198)
#include <unordered_map>
#include <App/Application.h>
#include <App/DynamicProperty.h>
#include <App/ParamRegistry.h>
#include "NotificationAreaParams.h"
using namespace Gui;

// Auto generated code (Tools/params_utils.py:210)
namespace {
class NotificationAreaParamsP: public ParameterGrp::ObserverType {
public:
    ParameterGrp::handle handle;
    std::unordered_map<const char *,void(*)(NotificationAreaParamsP*),App::CStringHasher,App::CStringHasher> funcs;
    // Auto generated code (Tools/params_utils.py:228)
    fastsignals::signal<void (const char*)> signalParamChanged;
    void signalAll()
    {
        signalParamChanged("NotificationAreaEnabled");
        signalParamChanged("NonIntrusiveNotificationsEnabled");
        signalParamChanged("NotificationTime");
        signalParamChanged("MaxOpenNotifications");
        signalParamChanged("NotificiationWidth");
        signalParamChanged("HideNonIntrusiveNotificationsWhenWindowDeactivated");
        signalParamChanged("PreventNonIntrusiveNotificationsWhenWindowNotActive");
        signalParamChanged("MaxWidgetMessages");
        signalParamChanged("AutoRemoveUserNotifications");
        signalParamChanged("DeveloperErrorSubscriptionEnabled");
        signalParamChanged("DeveloperWarningSubscriptionEnabled");

    // Auto generated code (Tools/params_utils.py:241)
    }
    bool NotificationAreaEnabled;
    bool NonIntrusiveNotificationsEnabled;
    long NotificationTime;
    long MaxOpenNotifications;
    long NotificiationWidth;
    bool HideNonIntrusiveNotificationsWhenWindowDeactivated;
    bool PreventNonIntrusiveNotificationsWhenWindowNotActive;
    long MaxWidgetMessages;
    bool AutoRemoveUserNotifications;
    bool DeveloperErrorSubscriptionEnabled;
    bool DeveloperWarningSubscriptionEnabled;

    // Auto generated code (Tools/params_utils.py:254)
    NotificationAreaParamsP() {
        handle = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/NotificationArea");
        handle->Attach(this);

        NotificationAreaEnabled = this->handle->GetBool("NotificationAreaEnabled", true);
        funcs["NotificationAreaEnabled"] = &NotificationAreaParamsP::updateNotificationAreaEnabled;
        NonIntrusiveNotificationsEnabled = this->handle->GetBool("NonIntrusiveNotificationsEnabled", true);
        funcs["NonIntrusiveNotificationsEnabled"] = &NotificationAreaParamsP::updateNonIntrusiveNotificationsEnabled;
        NotificationTime = this->handle->GetInt("NotificationTime", 20);
        funcs["NotificationTime"] = &NotificationAreaParamsP::updateNotificationTime;
        MaxOpenNotifications = this->handle->GetInt("MaxOpenNotifications", 15);
        funcs["MaxOpenNotifications"] = &NotificationAreaParamsP::updateMaxOpenNotifications;
        NotificiationWidth = this->handle->GetInt("NotificiationWidth", 800);
        funcs["NotificiationWidth"] = &NotificationAreaParamsP::updateNotificiationWidth;
        HideNonIntrusiveNotificationsWhenWindowDeactivated = this->handle->GetBool("HideNonIntrusiveNotificationsWhenWindowDeactivated", true);
        funcs["HideNonIntrusiveNotificationsWhenWindowDeactivated"] = &NotificationAreaParamsP::updateHideNonIntrusiveNotificationsWhenWindowDeactivated;
        PreventNonIntrusiveNotificationsWhenWindowNotActive = this->handle->GetBool("PreventNonIntrusiveNotificationsWhenWindowNotActive", true);
        funcs["PreventNonIntrusiveNotificationsWhenWindowNotActive"] = &NotificationAreaParamsP::updatePreventNonIntrusiveNotificationsWhenWindowNotActive;
        MaxWidgetMessages = this->handle->GetInt("MaxWidgetMessages", 1000);
        funcs["MaxWidgetMessages"] = &NotificationAreaParamsP::updateMaxWidgetMessages;
        AutoRemoveUserNotifications = this->handle->GetBool("AutoRemoveUserNotifications", true);
        funcs["AutoRemoveUserNotifications"] = &NotificationAreaParamsP::updateAutoRemoveUserNotifications;
        DeveloperErrorSubscriptionEnabled = this->handle->GetBool("DeveloperErrorSubscriptionEnabled", false);
        funcs["DeveloperErrorSubscriptionEnabled"] = &NotificationAreaParamsP::updateDeveloperErrorSubscriptionEnabled;
        DeveloperWarningSubscriptionEnabled = this->handle->GetBool("DeveloperWarningSubscriptionEnabled", false);
        funcs["DeveloperWarningSubscriptionEnabled"] = &NotificationAreaParamsP::updateDeveloperWarningSubscriptionEnabled;
    }

    // Auto generated code (Tools/params_utils.py:284)
    ~NotificationAreaParamsP() override = default;

    // Auto generated code (Tools/params_utils.py:297)
    void OnChange(Base::Subject<const char*> &, const char* sReason) override {
        if(!sReason)
            return;
        auto it = funcs.find(sReason);
        if(it == funcs.end())
            return;
        it->second(this);
        signalParamChanged(sReason);
    }


    // Auto generated code (Tools/params_utils.py:314)
    static void updateNotificationAreaEnabled(NotificationAreaParamsP *self) {
        self->NotificationAreaEnabled = self->handle->GetBool("NotificationAreaEnabled", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNonIntrusiveNotificationsEnabled(NotificationAreaParamsP *self) {
        self->NonIntrusiveNotificationsEnabled = self->handle->GetBool("NonIntrusiveNotificationsEnabled", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNotificationTime(NotificationAreaParamsP *self) {
        self->NotificationTime = self->handle->GetInt("NotificationTime", 20);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxOpenNotifications(NotificationAreaParamsP *self) {
        self->MaxOpenNotifications = self->handle->GetInt("MaxOpenNotifications", 15);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateNotificiationWidth(NotificationAreaParamsP *self) {
        self->NotificiationWidth = self->handle->GetInt("NotificiationWidth", 800);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateHideNonIntrusiveNotificationsWhenWindowDeactivated(NotificationAreaParamsP *self) {
        self->HideNonIntrusiveNotificationsWhenWindowDeactivated = self->handle->GetBool("HideNonIntrusiveNotificationsWhenWindowDeactivated", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updatePreventNonIntrusiveNotificationsWhenWindowNotActive(NotificationAreaParamsP *self) {
        self->PreventNonIntrusiveNotificationsWhenWindowNotActive = self->handle->GetBool("PreventNonIntrusiveNotificationsWhenWindowNotActive", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateMaxWidgetMessages(NotificationAreaParamsP *self) {
        self->MaxWidgetMessages = self->handle->GetInt("MaxWidgetMessages", 1000);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateAutoRemoveUserNotifications(NotificationAreaParamsP *self) {
        self->AutoRemoveUserNotifications = self->handle->GetBool("AutoRemoveUserNotifications", true);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDeveloperErrorSubscriptionEnabled(NotificationAreaParamsP *self) {
        self->DeveloperErrorSubscriptionEnabled = self->handle->GetBool("DeveloperErrorSubscriptionEnabled", false);
    }
    // Auto generated code (Tools/params_utils.py:314)
    static void updateDeveloperWarningSubscriptionEnabled(NotificationAreaParamsP *self) {
        self->DeveloperWarningSubscriptionEnabled = self->handle->GetBool("DeveloperWarningSubscriptionEnabled", false);
    }
};

// Auto generated code (Tools/params_utils.py:336)
NotificationAreaParamsP *instance() {
    static NotificationAreaParamsP *inst = new NotificationAreaParamsP;
    return inst;
}

} // Anonymous namespace

// Auto generated code (Tools/params_utils.py:352)
static const App::ParamRegistry::Registrar _NotificationAreaParamsRegistrar({
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "NotificationAreaEnabled", "NotificationAreaEnabled", App::ParamInfo::Bool, true)
        .setTitle("Enable notification area")
        .setDoc("Show the notification area in the status bar and collect\n"
"notifications in it. Takes effect at once."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "NonIntrusiveNotificationsEnabled", "NonIntrusiveNotificationsEnabled", App::ParamInfo::Bool, true)
        .setTitle("Enable non-intrusive notifications")
        .setDoc("Show a notification in a bubble next to the notification area\n"
"instead of a dialog box that has to be answered. When off, a\n"
"notification that asks for it is shown as a dialog box."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "NotificationTime", "NotificationTime", App::ParamInfo::Int, 20)
        .setTitle("Notification duration")
        .setDoc("Seconds a non-intrusive notification stays on screen, unless a\n"
"mouse button is clicked first. 0 to 120."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "MaxOpenNotifications", "MaxOpenNotifications", App::ParamInfo::Int, 15)
        .setTitle("Maximum number of notifications")
        .setDoc("Largest number of non-intrusive notifications on screen at the\n"
"same time; older ones give way."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "NotificiationWidth", "NotificiationWidth", App::ParamInfo::Int, 800)
        .setTitle("Notification width")
        .setDoc("Width of a non-intrusive notification in pixels. At least 300."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "HideNonIntrusiveNotificationsWhenWindowDeactivated", "HideNonIntrusiveNotificationsWhenWindowDeactivated", App::ParamInfo::Bool, true)
        .setTitle("Hide when other window is activated")
        .setDoc("Open non-intrusive notifications disappear when another window\n"
"is activated."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "PreventNonIntrusiveNotificationsWhenWindowNotActive", "PreventNonIntrusiveNotificationsWhenWindowNotActive", App::ParamInfo::Bool, true)
        .setTitle("Do not show when inactive")
        .setDoc("Show no non-intrusive notification while the main window is not\n"
"the active window. They are still collected in the list."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "MaxWidgetMessages", "MaxWidgetMessages", App::ParamInfo::Int, 1000)
        .setTitle("Maximum messages in the list")
        .setDoc("Largest number of messages the notification area's list keeps;\n"
"older ones are dropped. 0 means no limit."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "AutoRemoveUserNotifications", "AutoRemoveUserNotifications", App::ParamInfo::Bool, true)
        .setTitle("Auto-remove user notifications")
        .setDoc("Remove a user notification from the list once the notification\n"
"duration has passed."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "DeveloperErrorSubscriptionEnabled", "DeveloperErrorSubscriptionEnabled", App::ParamInfo::Bool, false)
        .setTitle("Debug errors")
        .setDoc("Show errors meant for developers in the notification area too."),
    App::ParamInfo("Gui", "NotificationAreaParams", "User parameter:BaseApp/Preferences/NotificationArea", "DeveloperWarningSubscriptionEnabled", "DeveloperWarningSubscriptionEnabled", App::ParamInfo::Bool, false)
        .setTitle("Debug warnings")
        .setDoc("Show warnings meant for developers in the notification area too."),
});

// Auto generated code (Tools/params_utils.py:368)
ParameterGrp::handle NotificationAreaParams::getHandle() {
    return instance()->handle;
}

// Auto generated code (Tools/params_utils.py:378)
fastsignals::signal<void (const char*)> &
NotificationAreaParams::signalParamChanged() {
    return instance()->signalParamChanged;
}

// Auto generated code (Tools/params_utils.py:387)
void NotificationAreaParams::signalAll() {
    instance()->signalAll();
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docNotificationAreaEnabled() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Show the notification area in the status bar and collect\n"
"notifications in it. Takes effect at once.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getNotificationAreaEnabled() {
    return instance()->NotificationAreaEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultNotificationAreaEnabled() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setNotificationAreaEnabled(const bool &v) {
    instance()->handle->SetBool("NotificationAreaEnabled",v);
    instance()->NotificationAreaEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeNotificationAreaEnabled() {
    instance()->handle->RemoveBool("NotificationAreaEnabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docNonIntrusiveNotificationsEnabled() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Show a notification in a bubble next to the notification area\n"
"instead of a dialog box that has to be answered. When off, a\n"
"notification that asks for it is shown as a dialog box.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getNonIntrusiveNotificationsEnabled() {
    return instance()->NonIntrusiveNotificationsEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultNonIntrusiveNotificationsEnabled() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setNonIntrusiveNotificationsEnabled(const bool &v) {
    instance()->handle->SetBool("NonIntrusiveNotificationsEnabled",v);
    instance()->NonIntrusiveNotificationsEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeNonIntrusiveNotificationsEnabled() {
    instance()->handle->RemoveBool("NonIntrusiveNotificationsEnabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docNotificationTime() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Seconds a non-intrusive notification stays on screen, unless a\n"
"mouse button is clicked first. 0 to 120.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NotificationAreaParams::getNotificationTime() {
    return instance()->NotificationTime;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NotificationAreaParams::defaultNotificationTime() {
    const static long def = 20;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setNotificationTime(const long &v) {
    instance()->handle->SetInt("NotificationTime",v);
    instance()->NotificationTime = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeNotificationTime() {
    instance()->handle->RemoveInt("NotificationTime");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docMaxOpenNotifications() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Largest number of non-intrusive notifications on screen at the\n"
"same time; older ones give way.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NotificationAreaParams::getMaxOpenNotifications() {
    return instance()->MaxOpenNotifications;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NotificationAreaParams::defaultMaxOpenNotifications() {
    const static long def = 15;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setMaxOpenNotifications(const long &v) {
    instance()->handle->SetInt("MaxOpenNotifications",v);
    instance()->MaxOpenNotifications = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeMaxOpenNotifications() {
    instance()->handle->RemoveInt("MaxOpenNotifications");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docNotificiationWidth() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Width of a non-intrusive notification in pixels. At least 300.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NotificationAreaParams::getNotificiationWidth() {
    return instance()->NotificiationWidth;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NotificationAreaParams::defaultNotificiationWidth() {
    const static long def = 800;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setNotificiationWidth(const long &v) {
    instance()->handle->SetInt("NotificiationWidth",v);
    instance()->NotificiationWidth = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeNotificiationWidth() {
    instance()->handle->RemoveInt("NotificiationWidth");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docHideNonIntrusiveNotificationsWhenWindowDeactivated() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Open non-intrusive notifications disappear when another window\n"
"is activated.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getHideNonIntrusiveNotificationsWhenWindowDeactivated() {
    return instance()->HideNonIntrusiveNotificationsWhenWindowDeactivated;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultHideNonIntrusiveNotificationsWhenWindowDeactivated() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setHideNonIntrusiveNotificationsWhenWindowDeactivated(const bool &v) {
    instance()->handle->SetBool("HideNonIntrusiveNotificationsWhenWindowDeactivated",v);
    instance()->HideNonIntrusiveNotificationsWhenWindowDeactivated = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeHideNonIntrusiveNotificationsWhenWindowDeactivated() {
    instance()->handle->RemoveBool("HideNonIntrusiveNotificationsWhenWindowDeactivated");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docPreventNonIntrusiveNotificationsWhenWindowNotActive() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Show no non-intrusive notification while the main window is not\n"
"the active window. They are still collected in the list.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getPreventNonIntrusiveNotificationsWhenWindowNotActive() {
    return instance()->PreventNonIntrusiveNotificationsWhenWindowNotActive;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultPreventNonIntrusiveNotificationsWhenWindowNotActive() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setPreventNonIntrusiveNotificationsWhenWindowNotActive(const bool &v) {
    instance()->handle->SetBool("PreventNonIntrusiveNotificationsWhenWindowNotActive",v);
    instance()->PreventNonIntrusiveNotificationsWhenWindowNotActive = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removePreventNonIntrusiveNotificationsWhenWindowNotActive() {
    instance()->handle->RemoveBool("PreventNonIntrusiveNotificationsWhenWindowNotActive");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docMaxWidgetMessages() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Largest number of messages the notification area's list keeps;\n"
"older ones are dropped. 0 means no limit.");
}

// Auto generated code (Tools/params_utils.py:405)
const long & NotificationAreaParams::getMaxWidgetMessages() {
    return instance()->MaxWidgetMessages;
}

// Auto generated code (Tools/params_utils.py:413)
const long & NotificationAreaParams::defaultMaxWidgetMessages() {
    const static long def = 1000;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setMaxWidgetMessages(const long &v) {
    instance()->handle->SetInt("MaxWidgetMessages",v);
    instance()->MaxWidgetMessages = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeMaxWidgetMessages() {
    instance()->handle->RemoveInt("MaxWidgetMessages");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docAutoRemoveUserNotifications() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Remove a user notification from the list once the notification\n"
"duration has passed.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getAutoRemoveUserNotifications() {
    return instance()->AutoRemoveUserNotifications;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultAutoRemoveUserNotifications() {
    const static bool def = true;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setAutoRemoveUserNotifications(const bool &v) {
    instance()->handle->SetBool("AutoRemoveUserNotifications",v);
    instance()->AutoRemoveUserNotifications = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeAutoRemoveUserNotifications() {
    instance()->handle->RemoveBool("AutoRemoveUserNotifications");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docDeveloperErrorSubscriptionEnabled() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Show errors meant for developers in the notification area too.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getDeveloperErrorSubscriptionEnabled() {
    return instance()->DeveloperErrorSubscriptionEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultDeveloperErrorSubscriptionEnabled() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setDeveloperErrorSubscriptionEnabled(const bool &v) {
    instance()->handle->SetBool("DeveloperErrorSubscriptionEnabled",v);
    instance()->DeveloperErrorSubscriptionEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeDeveloperErrorSubscriptionEnabled() {
    instance()->handle->RemoveBool("DeveloperErrorSubscriptionEnabled");
}

// Auto generated code (Tools/params_utils.py:397)
const char *NotificationAreaParams::docDeveloperWarningSubscriptionEnabled() {
    return QT_TRANSLATE_NOOP("NotificationAreaParams",
"Show warnings meant for developers in the notification area too.");
}

// Auto generated code (Tools/params_utils.py:405)
const bool & NotificationAreaParams::getDeveloperWarningSubscriptionEnabled() {
    return instance()->DeveloperWarningSubscriptionEnabled;
}

// Auto generated code (Tools/params_utils.py:413)
const bool & NotificationAreaParams::defaultDeveloperWarningSubscriptionEnabled() {
    const static bool def = false;
    return def;
}

// Auto generated code (Tools/params_utils.py:422)
void NotificationAreaParams::setDeveloperWarningSubscriptionEnabled(const bool &v) {
    instance()->handle->SetBool("DeveloperWarningSubscriptionEnabled",v);
    instance()->DeveloperWarningSubscriptionEnabled = v;
}

// Auto generated code (Tools/params_utils.py:431)
void NotificationAreaParams::removeDeveloperWarningSubscriptionEnabled() {
    instance()->handle->RemoveBool("DeveloperWarningSubscriptionEnabled");
}
//[[[end]]]
