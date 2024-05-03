/***************************************************************************
 *   Copyright (c) 2019 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_COMMAND_T_H
#define GUI_COMMAND_T_H

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Exception.h>
#include <Gui/Command.h>
#include <type_traits>
#include <typeinfo>
#include <boost/format.hpp>


namespace Gui {

class FormatString
{
public:
    static std::string str(const std::string& s) {
        return s;
    }
    static std::string str(const char* s) {
        return s;
    }
    static std::string str(const QString& s) {
        return s.toStdString();
    }
    static std::string str(const std::stringstream& s) {
        return s.str();
    }
    static std::string str(const std::ostringstream& s) {
        return s.str();
    }
    static std::string str(const std::ostream& s) {
        if (typeid(s) == typeid(std::ostringstream))
            return dynamic_cast<const std::ostringstream&>(s).str();
        else if (typeid(s) == typeid(std::stringstream))
            return dynamic_cast<const std::stringstream&>(s).str();
        throw Base::TypeError("Not a std::stringstream or std::ostringstream");
    }
    static std::string toStr(boost::format& f) {
        return f.str();
    }

    template<typename T, typename... Args>
    static std::string toStr(boost::format& f, T&& t, Args&&... args) {
        return toStr(f % std::forward<T>(t), std::forward<Args>(args)...);
    }
};

/** @defgroup CommandFuncs Helper functions for running commands through Python interpreter */
//@{

/** Runs a command for accessing document attribute or method
 * This function is an alternative to _FCMD_DOC_CMD
 * @param doc: pointer to a document
 * @param mod: module name, "Gui" or "App"
 * @param cmd: command string, streamable
 *
 * Example:
 * @code{.cpp}
 *      _cmdDocument(Gui::Command::Gui, doc, "Gui", std::stringstream() << "getObject('" << objName << "')");
 * @endcode
 *
 * Translates to command (assuming doc's name is 'DocName', and
 * and objName contains value 'ObjName'):
 * @code{.py}
 *       Gui.getDocument('DocName').getObject('ObjName')
 * @endcode
 */
template<typename T>
void _cmdDocument(const char *file, int line, Gui::Command::DoCmd_Type cmdType, const App::Document* doc, const std::string& mod, T&& cmd) {
    if (doc && doc->getName()) {
        std::stringstream str;
        str << mod << ".getDocument('" << doc->getName() << "')."
            << FormatString::str(cmd);
        Gui::Command::_runCommand(file, line, cmdType, str.str().c_str());
    } else
        throw Base::RuntimeError("Invalid document");
}

/** Runs a command for accessing document attribute or method
 * This function is an alternative to _FCMD_DOC_CMD
 * @param doc: document name
 * @param mod: module name, "Gui" or "App"
 * @param cmd: command string, streamable
 *
 * Example:
 * @code{.cpp}
 *      _cmdDocument(Gui::Command::Gui, doc, "Gui", std::stringstream() << "getObject('" << objName << "')");
 * @endcode
 *
 * Translates to command (assuming doc's name is 'DocName', and
 * and objName contains value 'ObjName'):
 * @code{.py}
 *       Gui.getDocument('DocName').getObject('ObjName')
 * @endcode
 */
template<typename T>
void _cmdDocument(const char *file, int line, Gui::Command::DoCmd_Type cmdType, const std::string& doc, const std::string& mod, T&& cmd) {
    if (!doc.empty()) {
        std::stringstream str;
        str << mod << ".getDocument('" << doc << "')."
            << FormatString::str(cmd);
        Gui::Command::_runCommand(file, line, cmdType, str.str().c_str());
    }
}

/** Runs a command for accessing App.Document attribute or method
 * This function is an alternative to FCMD_DOC_CMD
 *
 * @param doc : document name or pointer to a document or pointer to object
 * @param ... : command string, streamable
 * @sa _cmdDocument()
 *
 * Example:
 * @code{.cpp}
 *      cmdAppDocument(doc, std::stringstream() << "getObject('" << objName << "')");
 * @endcode
 *
 * Translates to command (assuming doc's name is 'DocName', and
 * and objName contains value 'ObjName'):
 * @code{.py}
 *       App.getDocument('DocName').getObject('ObjName')
 * @endcode
 */
#define cmdAppDocument(doc, ...) \
    _cmdDocument(__FILE__, __LINE__, Gui::Command::Doc, doc, "App", ## __VA_ARGS__)

/** Runs a command for accessing App.Document attribute or method
 *
 * @param doc : document or document name
 * @param ... : command string, streamable
 * @sa _cmdDocument()
 *
 * Example:
 * @code{.cpp}
 *      cmdGuiDocument(doc, std::stringstream() << "getObject('" << objName << "')");
 * @endcode
 *
 * Translates to command (assuming doc's name is 'DocName', and
 * and objName contains value 'ObjName'):
 * @code{.py}
 *       Gui.getDocument('DocName').getObject('ObjName')
 * @endcode
 */
#define cmdGuiDocument(obj, ...) \
    _cmdDocument(__FILE__, __LINE__, Gui::Command::Gui, obj, "Gui", ## __VA_ARGS__)

/** Runs a command for accessing an object's document attribute or method
 * This function is an alternative to _FCMD_OBJ_DOC_CMD
 * @param obj: pointer to a DocumentObject
 * @param mod: module name, "Gui" or "App"
 * @param cmd: command string, streamable
 */
template<typename T>
inline void _cmdDocument(const char *file, int line, Gui::Command::DoCmd_Type cmdType, const App::DocumentObject* obj, const std::string& mod, T&& cmd) {
    if (obj && obj->getDocument())
        _cmdDocument(file, line, cmdType, obj->getDocument(), mod, std::forward<T>(cmd));
    else
        throw Base::RuntimeError("Invalid object");
}

/** Runs a command for accessing a document's attribute or method
 * @param doc: pointer to a Document
 * @param cmd: command string, supporting printf like formatter
 *
 * Example:
 * @code{.cpp}
 *      _cmdAppDocumentArgs(obj, "addObject('%s')", "Part::Feature");
 * @endcode
 *
 * Translates to command (assuming obj's document name is 'DocName':
 * @code{.py}
 *       App.getDocument('DocName').addObject('Part::Feature')
 * @endcode
 */
template<typename...Args>
void _cmdAppDocumentArgs(const char *file, int line, const App::Document* doc, const std::string& cmd, Args&&... args) {
    std::string _cmd;
    try {
        boost::format fmt(cmd);
        _cmd = FormatString::toStr(fmt, std::forward<Args>(args)...);
        Gui::Command::_doCommand(file, line, Gui::Command::Doc,"App.getDocument('%s').%s",
            doc->getName(), _cmd.c_str());
    }
    catch (const std::exception& e) {
        Base::Console().DeveloperError(doc->Label.getStrValue(),"%s: %s\n", e.what(), cmd.c_str());
    }
    catch (const Base::Exception&) {
        Base::Console().DeveloperError(doc->Label.getStrValue(),"App.getDocument('%s').%s\n",
            doc->getName(), _cmd.c_str());
        throw;
    }
}

template<typename...Args>
void _cmdAppDocumentArgs(const char *file, int line, const App::DocumentObject* obj, const std::string& cmd, Args&&... args) {
    if (obj && obj->isAttachedToDocument()) {
        _cmdAppDocumentArgs(file, line, obj->getDocument(), cmd, std::forward<Args>(args)...);
    }
    else {
        throw Base::RuntimeError("Invalid object");
    }
}

#define cmdAppDocumentArgs(doc, cmd, ...) \
    _cmdAppDocumentArgs(__FILE__, __LINE__, doc, cmd, ## __VA_ARGS__)

/** Runs a command for accessing an object's Gui::Document attribute or method
 * This function is an alternative to FCMD_VOBJ_DOC_CMD
 * @param obj : pointer to a DocumentObject
 * @param ... : command string, streamable
 */
#define cmdGuiDocument(obj, ...) \
    _cmdDocument(__FILE__, __LINE__, Gui::Command::Gui, obj, "Gui", ## __VA_ARGS__)

/** Runs a command for accessing a document/view object's attribute or method
 * This function is an alternative to _FCMD_OBJ_CMD
 * @param cmdType: Command type
 * @param obj: pointer to a DocumentObject
 * @param mod: module name, "Gui" or "App"
 * @param cmd: command string, streamable
 *
 * Example:
 * @code{.cpp}
 *      _cmdObject(Command::Gui,obj,"Gui", "Visibility = " << (visible?"True":"False"));
 * @endcode
 *
 * Translates to command (assuming obj's document name is 'DocName', obj's name
 * is 'ObjName', and visible is true):
 * @code{.py}
 *       Gui.getDocument('DocName').getObject('ObjName').Visibility = True
 * @endcode
 */
template<typename T>
void _cmdObject(const char *file, int line, Gui::Command::DoCmd_Type cmdType, const App::DocumentObject* obj, const std::string& mod, T&& cmd) {
    if (obj && obj->isAttachedToDocument()) {
        std::ostringstream str;
        str << mod << ".getDocument('" << obj->getDocument()->getName() << "')"
                      ".getObject('" << obj->getNameInDocument() << "')."
                   << FormatString::str(cmd);
        Gui::Command::_runCommand(file, line, cmdType, str.str().c_str());
    } else
        throw Base::RuntimeError("Invalid object");
}

/** Runs a command for accessing an document object's attribute or method
 * This function is an alternative to FCMD_OBJ_CMD
 * @param obj : pointer to a DocumentObject
 * @param ... : command string, streamable
 * @sa _cmdObject()
 */
#define cmdAppObject(obj, ...) \
    _cmdObject(__FILE__, __LINE__, Gui::Command::Doc, obj, "App", ## __VA_ARGS__)

/** Runs a command for accessing an view object's attribute or method
 * This function is an alternative to FCMD_VOBJ_CMD
 * @param obj : pointer to a DocumentObject
 * @param ... : command string, streamable
 * @sa _cmdObject()
 */
#define cmdGuiObject(obj, ...) \
    _cmdObject(__FILE__, __LINE__, Gui::Command::Gui, obj, "Gui", ## __VA_ARGS__)

/// Hides an object
#define cmdAppObjectHide(obj) \
    cmdAppObject(obj, "Visibility = False")

/// Shows an object
#define cmdAppObjectShow(obj) \
    cmdAppObject(obj, "Visibility = True")

/** Runs a command to start editing a give object
 * This function is an alternative to FCMD_SET_EDIT
 * @param obj: pointer to a DocumentObject
 *
 * Unlike other helper functions, this one editing the object using the current
 * active document, instead of the object's owner document. This allows
 * in-place editing an object, which may be brought in through linking to an
 * external group.
 */
inline void _cmdSetEdit(const char *file, int line, const App::DocumentObject* obj, int mod = 0) {
    if (obj && obj->isAttachedToDocument()) {
        Gui::Command::_doCommand(file, line, Gui::Command::Gui,
            "Gui.ActiveDocument.setEdit(App.getDocument('%s').getObject('%s'), %d)",
            obj->getDocument()->getName(), obj->getNameInDocument(), mod);
    } else
        throw Base::RuntimeError("Invalid object");
}
#define cmdSetEdit(obj, ...) _cmdSetEdit(__FILE__, __LINE__, obj, ## __VA_ARGS__)

/** Runs a command for accessing a document object's attribute or method
 * This function is an alternative to FCMD_OBJ_CMD2
 * @param cmd: command string, supporting printf like formatter
 * @param obj: pointer to a DocumentObject
 *
 * Example:
 * @code{.cpp}
 *      cmdAppObjectArgs(obj, "Visibility = %s", visible ? "True" : "False");
 * @endcode
 *
 * Translates to command (assuming obj's document name is 'DocName', obj's name
 * is 'ObjName', and visible is true):
 * @code{.py}
 *       App.getDocument('DocName').getObject('ObjName').Visibility = True
 * @endcode
 */
template<typename...Args>
void _cmdAppObjectArgs(const char *file, int line, const App::DocumentObject* obj, const std::string& cmd, Args&&... args) {
    std::string _cmd;
    if (!obj || !obj->getNameInDocument())
        throw Base::RuntimeError("Invalid object");
    try {
        boost::format fmt(cmd);
        _cmd = FormatString::toStr(fmt, std::forward<Args>(args)...);
        Gui::Command::_doCommand(file, line, Gui::Command::Doc,"App.getDocument('%s').getObject('%s').%s",
            obj->getDocument()->getName(), obj->getNameInDocument(), _cmd.c_str());
    }
    catch (const std::exception& e) {
        Base::Console().DeveloperError(obj->getFullLabel(),"%s: %s\n", e.what(), cmd.c_str());
    }
    catch (const Base::Exception&) {
        Base::Console().DeveloperError(obj->getFullLabel(),"App.getDocument('%s').getObject('%s').%s\n",
            obj->getDocument()->getName(), obj->getNameInDocument(), _cmd.c_str());
        throw;
    }
}

#define cmdAppObjectArgs(obj, cmd, ...) \
    _cmdAppObjectArgs(__FILE__, __LINE__, obj, cmd, ## __VA_ARGS__)

/** Runs a command for accessing a view object's attribute or method
 * This function is an alternative to FCMD_VOBJ_CMD2
 * @param cmd: command string, supporting printf like formatter
 * @param obj: pointer to a DocumentObject
 * @sa cmdAppObjectArgs()
 */
template<typename...Args>
void _cmdGuiObjectArgs(const char *file, int line, const App::DocumentObject* obj, const std::string& cmd, Args&&... args) {
    std::string _cmd;
    if (!obj || !obj->getNameInDocument())
        throw Base::RuntimeError("Invalid object");
    try {
        boost::format fmt(cmd);
        _cmd = FormatString::toStr(fmt, std::forward<Args>(args)...);
        Gui::Command::_doCommand(file, line, Gui::Command::Gui,"Gui.getDocument('%s').getObject('%s').%s",
            obj->getDocument()->getName(), obj->getNameInDocument(), _cmd.c_str());
    }
    catch (const std::exception& e) {
        Base::Console().DeveloperError(obj->getFullLabel(),"%s: %s\n", e.what(), cmd.c_str());
    }
    catch (const Base::Exception&) {
        Base::Console().DeveloperError(obj->getFullLabel(),"Gui.getDocument('%s').getObject('%s').%s\n",
            obj->getDocument()->getName(), obj->getNameInDocument(), _cmd.c_str());
        throw;
    }
}

#define cmdGuiObjectArgs(obj, cmd, ...) \
    _cmdGuiObjectArgs(__FILE__, __LINE__, obj, cmd, ## __VA_ARGS__)

/** Runs a command
 * @param cmdType: command type
 * @param cmd: command string, supporting printf like formatter
 *
 * Example:
 * @code{.cpp}
 *      doCommandT(Gui::Command::Gui, "Gui.getDocument(%s).getObject(%s).Visibility = %s", "DocName", "ObjName", visible?"True":"False");
 * @endcode
 *
 * Translates to command (assuming obj's document name is 'DocName', obj's name
 * is 'ObjName', and visible is true):
 * @code{.py}
 *       Gui.getDocument('DocName').getObject('ObjName').Visibility = True
 * @endcode
 */
template<typename...Args>
void _doCommandT(const char *file, int line, Gui::Command::DoCmd_Type cmdType, const std::string& cmd, Args&&... args) {
    std::string _cmd;
    try {
        boost::format fmt(cmd);
        _cmd = FormatString::toStr(fmt, std::forward<Args>(args)...);
        Gui::Command::_doCommand(file, line, cmdType,"%s", _cmd.c_str());
    }
    catch (const std::exception& e) {
        Base::Console().DeveloperError("doCommandT","%s: %s\n", e.what(), cmd.c_str());
    }
    catch (const Base::Exception&) {
        Base::Console().DeveloperError("doCommandT","%s\n", _cmd.c_str());
        throw;
    }
}

#define doCommandT(cmdType, cmd, ...) \
    _doCommandT(__FILE__, __LINE__, cmdType, cmd, ## __VA_ARGS__)

/** Copy visual attributes from a source to a target object
 */
#define copyVisualT(...) Command::_copyVisual(__FILE__,__LINE__,__VA_ARGS__)

//@}

};

#endif // GUI_COMMAND_T_H

