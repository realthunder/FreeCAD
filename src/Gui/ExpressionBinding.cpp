/***************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>             *
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
 *   write to the Free Software Foundation, Inc., 51 Franklin Street,      *
 *   Fifth Floor, Boston, MA  02110-1301, USA                              *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QLineEdit>
# include <QPixmapCache>
# include <QStyle>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ExpressionParser.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyGeo.h>
#include <Base/Tools.h>

#include "BitmapFactory.h"
#include "Command.h"
#include "ExpressionBinding.h"
#include "QuantitySpinBox_p.h"



FC_LOG_LEVEL_INIT("Expression",true,true)

using namespace Gui;
using namespace App;
namespace sp = std::placeholders;

ExpressionBinding::ExpressionBinding() = default;

ExpressionBinding::~ExpressionBinding() = default;

bool ExpressionBinding::isBound() const
{
    return path.getDocumentObject() != nullptr;
}

void ExpressionBinding::unbind()
{
    expressionchanged.disconnect();
    objectdeleted.disconnect();
    path = App::ObjectIdentifier();
}

void Gui::ExpressionBinding::setExpression(std::shared_ptr<Expression> expr)
{
    DocumentObject * docObj = path.getDocumentObject();
    if (!docObj)
        return;

    try {
        if (expr) {
            const std::string error = docObj->ExpressionEngine.validateExpression(path, expr);

            if (!error.empty())
                throw Base::RuntimeError(error.c_str());

        }

        lastExpression = getExpression();

        bool transaction = !App::GetApplication().getActiveTransaction();
        if(transaction) {
            std::ostringstream ss;
            ss << (expr?"Set":"Discard") << " expression " << docObj->Label.getValue();
            App::GetApplication().setActiveTransaction(ss.str().c_str());
        }

        docObj->ExpressionEngine.setValue(path, expr);

        if(m_autoApply)
            apply();
        
        if(transaction)
            App::GetApplication().closeActiveTransaction();

        if (auto label = qobject_cast<ExpressionLabel*>(iconLabel)) {
            if (expr) {
                std::string txt = expr->toString();
                if (txt.size() > 1024) {
                    txt.resize(1024);
                    txt += "\n\n...";
                }
                label->setToolTip(Base::Tools::fromStdString(txt));
            } else 
                label->setToolTip(QString());
            label->setState(expr ? ExpressionLabel::Bound : ExpressionLabel::Binding);
        }
    } catch (Base::Exception &e) {
        if (iconLabel)
            iconLabel->setToolTip(QString::fromUtf8(e.what()));
    }
}

void ExpressionBinding::bind(const App::ObjectIdentifier &_path)
{
    const Property * prop = _path.getProperty();

    Q_ASSERT(prop != nullptr);

    path = _path.canonicalPath();

    //connect to be informed about changes
    DocumentObject * docObj = path.getDocumentObject();
    if (docObj) {
        auto label = qobject_cast<ExpressionLabel*>(iconLabel);
        if (label) {
            auto expr = getExpression();
            if (expr)
                iconLabel->setToolTip(Base::Tools::fromStdString(expr->toString()));
            else 
                iconLabel->setToolTip(QString());
            label->setState(expr ? ExpressionLabel::Bound : ExpressionLabel::Binding);
        }

        //NOLINTBEGIN
        expressionchanged = docObj->ExpressionEngine.expressionChanged.connect(std::bind(&ExpressionBinding::expressionChange, this, sp::_1));
        App::Document* doc = docObj->getDocument();
        objectdeleted = doc->signalDeletedObject.connect(std::bind(&ExpressionBinding::objectDeleted, this, sp::_1));
        //NOLINTEND
    }
}

void ExpressionBinding::bind(const Property &prop)
{
    bind(App::ObjectIdentifier(prop));
}

bool ExpressionBinding::hasExpression(bool checkDoubleBinding) const
{
    if(!isBound())
        return false;
    auto expr = getExpression();
    return expr && (!checkDoubleBinding
                    || !App::VariableExpression::isDoubleBinding(getExpression().get()));
}

std::shared_ptr<App::Expression> ExpressionBinding::getExpression() const
{
    DocumentObject * docObj = path.getDocumentObject();

    Q_ASSERT(isBound() && docObj != nullptr);

    return docObj->getExpression(path).expression;
}

std::string ExpressionBinding::getExpressionString(bool no_throw) const
{
    try {
        if (!getExpression())
            throw Base::RuntimeError("No expression found.");
        return getExpression()->toString();
    } catch (Base::Exception &e) {
        if(no_throw)
            FC_ERR("failed to get expression string: " << e.what());
        else
            throw;
    } catch (std::exception &e) {
        if(no_throw)
            FC_ERR("failed to get expression string: " << e.what());
        else
            throw;
    } catch (...) {
        if(no_throw)
            FC_ERR("failed to get expression string: unknown exception");
        else
            throw;
    }
    return {};
}

std::string ExpressionBinding::getEscapedExpressionString() const
{
    return Base::Tools::escapeEncodeString(getExpressionString(false));
}

bool ExpressionBinding::assignToProperty(const std::string & propName, double value)
{
    if (isBound()) {
        const App::ObjectIdentifier & path = getPath();
        const Property * prop = path.getProperty();

        /* Skip update if property is bound and we know it is read-only */
        if (prop && prop->isReadOnly())
            return true;

        if (prop && prop->isDerivedFrom<App::PropertyPlacement>()) {
            std::string p = path.getSubPathStr();
            if (p == ".Rotation.Angle") {
                value = Base::toRadians(value);
            }
        }
    }

    Gui::Command::doCommand(Gui::Command::Doc, "%s = %f", propName.c_str(), value);
    return true;
}

QPixmap ExpressionBinding::getIcon(const char* name, const QSize& size) const
{
    QString key = QStringLiteral("%1_%2x%3")
        .arg(QString::fromUtf8(name))
        .arg(size.width())
        .arg(size.height());
    QPixmap icon;
    if (QPixmapCache::find(key, &icon))
        return icon;

    icon = BitmapFactory().pixmapFromSvg(name, size);
    if (!icon.isNull())
        QPixmapCache::insert(key, icon);
    return icon;
}

bool ExpressionBinding::apply(const std::string & propName)
{
    Q_UNUSED(propName);
    // Expression is set/cleared immediately upon calling of setExpression()
    // (done by various widgets that are derived from ExpressionBinding, e.g.
    // Gui::IntSpinBox), so no need to do anything else.
    return hasExpression(false);
}

bool ExpressionBinding::apply()
{
    Property * prop(path.getProperty());

    assert(prop);
    Q_UNUSED(prop);

    DocumentObject * docObj(path.getDocumentObject());

    if (!docObj)
        throw Base::RuntimeError("Document object not found.");

    /* Skip updating read-only properties */
    if (prop->isReadOnly())
        return true;

    std::string _path = getPath().toEscapedString();
    const char *path = _path.c_str();
    if(path[0] == '.')
        ++path;
    return apply(Gui::Command::getObjectCmd(docObj) + "." + path);
}

void ExpressionBinding::expressionChange(const ObjectIdentifier& id) {
    if(id==path)
        onChange();
}

bool ExpressionBinding::setExpressionString(const char *str, bool no_throw)
{
    auto obj = path.getDocumentObject();
    if (!obj) {
        if (!no_throw)
            throw Base::RuntimeError("Object not bound");
        return false;
    }
    try {
        std::shared_ptr<Expression> expr;
        if (str && str[0])
            expr = Expression::parse(obj, str);
        setExpression(expr);
    } catch (Base::Exception &e) {
        if (no_throw)
            return false;
        throw;
    } catch (...) {
        if (no_throw)
            return false;
        throw;
    }
    return true;
}

void ExpressionBinding::objectDeleted(const App::DocumentObject& obj)
{
    DocumentObject * docObj = path.getDocumentObject();
    if (docObj == &obj || path.getOwner() == &obj) {
        unbind();
    }
}

void ExpressionBinding::makeLabel(QLineEdit* le)
{
    defaultPalette = le->palette();
    iconLabel = new ExpressionLabel(le);
    LineEditStyle::setup(le);
}
