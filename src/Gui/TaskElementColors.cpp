/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef _PreComp_
# include <boost/algorithm/string/predicate.hpp>
# include <QColorDialog>
# include <sstream>
#endif

#include <array>
#include <App/ElementNamingUtils.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "TaskElementColors.h"
#include "ui_TaskElementColors.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "CommandT.h"
#include "Control.h"
#include "Document.h"
#include "FileDialog.h"
#include "Selection.h"
#include "ViewParams.h"

#include "ViewProviderLink.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
namespace sp = std::placeholders;

class ElementColors::Private
{
public:
    using Connection = boost::signals2::connection;
    std::unique_ptr<Ui_TaskElementColors> ui;
    ViewProviderDocumentObject *vp;
    ViewProviderDocumentObject *vpParent;
    Document *vpDoc;
    std::map<std::string,QListWidgetItem*> elements;
    std::vector<QListWidgetItem*> items;
    Connection connectDelDoc;
    Connection connectDelObj;
    Connection connParam;
    QPixmap px;
    bool busy;
    bool touched;
    bool restoreOnTop;
    std::string editView;
    bool restoreParam;

    std::string editDoc;
    std::string editObj;
    std::string editSub;
    std::string editElement;

    explicit Private(ViewProviderDocumentObject* vp, const char *element="")
        : ui(new Ui_TaskElementColors()), vp(vp),editElement(element)
    {
        restoreParam = !App::DocumentParams::getViewObjectTransaction();
        if (restoreParam) {
            App::DocumentParams::setViewObjectTransaction(true);
            connParam = App::DocumentParams::signalParamChanged().connect(
                [this](const char *name) {
                    if (boost::equals(name, "ViewObjectTransaction"))
                        restoreParam = false;
                });
        }

        vpDoc = vp->getDocument();
        vpParent = vp;
        auto doc = Application::Instance->editDocument();
        if(doc) {
            auto editVp = doc->getInEdit(&vpParent,&editSub);
            if(editVp == vp) {
                auto obj = vpParent->getObject();
                editDoc = obj->getDocument()->getName();
                editObj = obj->getNameInDocument();
                editSub = Data::noElementName(editSub.c_str());
                try {
                    std::ostringstream ss;
                    ss << "_taskElementColorView" << this;
                    doCommandT(Command::Gui, "%s = FreeCADGui.getDocument('%s').EditingView",
                                            ss.str(), editDoc);
                    editView = ss.str();
                } catch (Base::Exception &e) {
                    e.ReportException();
                } catch (...)
                {}

            }
        }
        if(editDoc.empty()) {
            vpParent = vp;
            editDoc = vp->getObject()->getDocument()->getName();
            editObj = vp->getObject()->getNameInDocument();
            editSub.clear();
        }
        restoreOnTop = false;
        setOnTop();

        busy = false;
        touched = false;
        int w = QApplication::style()->standardPixmap(QStyle::SP_DirClosedIcon).width();
        px = QPixmap(w,w);
    }

    ~Private() {
        deinit();
    }

    void deinit() {
        setOnTop(true, true);
        if (restoreParam) {
            App::DocumentParams::setViewObjectTransaction(false);
            restoreParam = false;
        }
        vp = nullptr;
    }

    void setOnTop(bool restore = false, bool del = false)
    {
        if (editView.empty())
            return;
        bool unset = false;
        if (restore && restoreOnTop)
            unset = true;
        else if (restore && !restoreOnTop)
            return;
        else if (!restore) {
            if (ViewParams::getColorOnTop()) {
                if (restoreOnTop)
                    return;
            } else if (!restoreOnTop)
                return;
            else
                unset = true;
        }
        restoreOnTop = !unset;
        try {
            doCommandT(Command::Gui, "%s.%s(FreeCAD.getDocument('%s').getObject('%s'), '%s')",
                        editView,
                        unset ? "removeObjectOnTop" : "addObjectOnTop",
                        editDoc, editObj, editSub);
            if (restore && del) {
                doCommandT(Command::Gui, "del(%s)", editView);
                editView.clear();
            }
        } catch (Base::Exception &e) {
            e.ReportException();
        } catch (...)
        {}
    }

    bool allow(App::Document *doc, App::DocumentObject *obj, const char *subname) {
        if (!vp)
            return true;
        if(editDoc!=doc->getName() ||
           editObj!=obj->getNameInDocument() ||
           !boost::starts_with(subname,editSub))
            return false;
        if(editElement.empty())
            return true;
        const char *dot = strrchr(subname,'.');
        if(!dot)
            dot = subname;
        else
            ++dot;
        return *dot==0 || boost::starts_with(dot,editElement);
    }

    void populate() {
        if (!vp)
            return;
        int i=0;
        for(auto &v : vp->getElementColors())
            addItem(i++,v.first.c_str());
    }

    void edit(QWidget *parent, bool elementOnly);

    void addItem(int i,const char *sub, bool push=false) {
        if (!vp)
            return;
        auto itE = elements.find(sub);
        if(i<0 && itE!=elements.end()) {
            if(push && !ViewProvider::hasHiddenMarker(sub))
                items.push_back(itE->second);
            return;
        }

        const char *marker = ViewProvider::hasHiddenMarker(sub);
        if(marker) {
            auto icon = BitmapFactory().pixmap("Invisible");
            auto item = new QListWidgetItem(icon,
                    QString::fromUtf8(std::string(sub,marker-sub).c_str()), ui->elementList);
            item->setData(Qt::UserRole,QColor());
            item->setData(Qt::UserRole+1,QString::fromUtf8(sub));
            elements.emplace(sub,item);
            return;
        }

        for(auto &v : vp->getElementColors(sub)) {
            auto it = elements.find(v.first.c_str());
            if(it!=elements.end()) {
                if(push)
                    items.push_back(it->second);
                continue;
            }
            auto color = v.second;
            QColor c;
            c.setRgbF(color.r,color.g,color.b,1.0-color.a);
            px.fill(c);
            auto item = new QListWidgetItem(QIcon(px),
                    QString::fromUtf8(Data::oldElementName(v.first.c_str()).c_str()),
                    ui->elementList);
            item->setData(Qt::UserRole,c);
            item->setData(Qt::UserRole+1,QString::fromUtf8(v.first.c_str()));
            if(push)
                items.push_back(item);
            elements.emplace(v.first,item);
        }
    }

    void apply() {
        if (!vp)
            return;
        std::map<std::string,App::Color> info;
        int count = ui->elementList->count();
        for(int i=0;i<count;++i) {
            auto item = ui->elementList->item(i);
            auto color = item->data(Qt::UserRole).value<QColor>();
            std::string sub = qPrintable(item->data(Qt::UserRole+1).value<QString>());
            info.emplace(qPrintable(item->data(Qt::UserRole+1).value<QString>()),
                    App::Color(color.redF(),color.greenF(),color.blueF(),1.0-color.alphaF()));
        }
        if(!App::GetApplication().getActiveTransaction())
            App::GetApplication().setActiveTransaction("Set colors");
        vp->setElementColors(info);
        touched = true;
        Selection().clearSelection();
    }

    void reset() {
        if (!vp)
            return;
        touched = false;
        App::GetApplication().closeActiveTransaction(true);
        Selection().clearSelection();
    }

    void accept() {
        App::GetApplication().closeActiveTransaction();
    }

    void removeAll() {
        if(!vp || elements.empty())
            return;

        auto itFace = elements.find("Face");
        auto itEdge = elements.find("Edge");
        auto itVertex = elements.find("Vertex");
        bool revert = false;
        for (auto & v : elements) {
            if (itFace != elements.end() && v.first != "Face" && boost::starts_with(v.first, "Face")) {
                revert = true;
                v.second->setData(Qt::UserRole, itFace->second->data(Qt::UserRole));
            }
            if (itEdge != elements.end() && v.first != "Edge" && boost::starts_with(v.first, "Edge")) {
                revert = true;
                v.second->setData(Qt::UserRole, itEdge->second->data(Qt::UserRole));
            }
            if (itVertex != elements.end() && v.first != "Vertex" && boost::starts_with(v.first, "Vertex")) {
                revert = true;
                v.second->setData(Qt::UserRole, itVertex->second->data(Qt::UserRole));
            }
        }
        if (revert)
            apply();
        ui->elementList->clear();
        elements.clear();
        apply();
    }

    void removeItems() {
        std::vector<QListWidgetItem*> faces;
        std::vector<QListWidgetItem*> edges;
        std::vector<QListWidgetItem*> vertexes;
        std::vector<std::string> subs;
        for(auto item : ui->elementList->selectedItems()) {
            subs.push_back(qPrintable(item->data(Qt::UserRole+1).value<QString>()));
            if (subs.back() != "Face" && boost::starts_with(subs.back(), "Face"))
                faces.push_back(item);
            else if (subs.back() != "Edge" && boost::starts_with(subs.back(), "Edge"))
                edges.push_back(item);
            else if (subs.back() != "Vertex" && boost::starts_with(subs.back(), "Vertex"))
                vertexes.push_back(item);
        }

        // In order to better support backward compatibility, Part view provider
        // still allow user to set color through DiffuseColor. So when doing
        // color mapping, it will not touch any element color that are not
        // explicitly set through ColoredElements property. Therefore, when the
        // user removes colored elements here, we must explicitly revert only
        // those element too. Same for the removeAll() above.
        if (faces.size()) {
            auto it = elements.find("Face");
            if (it != elements.end()) {
                for (auto item : faces)
                    item->setData(Qt::UserRole, it->second->data(Qt::UserRole));
            }
        }
        if (edges.size()) {
            auto it = elements.find("Edge");
            if (it != elements.end()) {
                for (auto item : edges)
                    item->setData(Qt::UserRole, it->second->data(Qt::UserRole));
            }
        }
        if (vertexes.size()) {
            auto it = elements.find("Vertex");
            if (it != elements.end()) {
                for (auto item : vertexes)
                    item->setData(Qt::UserRole, it->second->data(Qt::UserRole));
            }
        }
        apply();

        // now remove the items
        for (auto & sub : subs) {
            auto it = elements.find(sub);
            if (it != elements.end()) {
                delete it->second;
                elements.erase(it);
            }
        }
        apply();
    }

    void editItem(QWidget *parent, QListWidgetItem *item) {
        std::string sub = qPrintable(item->data(Qt::UserRole+1).value<QString>());
        if(ViewProvider::hasHiddenMarker(sub.c_str()))
            return;
        auto color = item->data(Qt::UserRole).value<QColor>();
        QColorDialog cd(color, parent);
        cd.setOptions(QColorDialog::ShowAlphaChannel|QColorDialog::DontUseNativeDialog);
        // Seems some version of Qt has to explicitly set the current color
        cd.setCurrentColor(color);
        if (cd.exec()!=QDialog::Accepted || color==cd.selectedColor())
            return;
        color = cd.selectedColor();
        item->setData(Qt::UserRole,color);
        px.fill(color);
        item->setIcon(QIcon(px));
        apply();
    }

    void onSelectionChanged(const SelectionChanges &msg) {
        // no object selected in the combobox or no sub-element was selected
        if (!vp || busy)
            return;
        busy = true;
        switch(msg.Type) {
        case SelectionChanges::ClrSelection:
            ui->elementList->clearSelection();
            break;
        case SelectionChanges::AddSelection:
        case SelectionChanges::RmvSelection:
            if(msg.pDocName && msg.pObjectName && msg.pSubName && msg.pSubName[0]) {
                if(editDoc == msg.pDocName &&
                   editObj == msg.pObjectName &&
                   boost::starts_with(msg.pSubName,editSub))
                {
                    const auto items = ui->elementList->findItems(QString::fromUtf8(msg.pSubName-editSub.size()), Qt::MatchExactly);
                    for(auto item : items)
                        item->setSelected(msg.Type==SelectionChanges::AddSelection);
                }
            }
        default:
            break;
        }
        busy = false;
    }

    void onSelectionChanged() {
        if(!vp || busy)
            return;
        busy = true;
        std::map<std::string,int> sels;
        for(auto &sel : Selection().getSelectionEx(
                    editDoc.c_str(),App::DocumentObject::getClassTypeId(), ResolveMode::NoResolve))
        {
            if(sel.getFeatName()!=editObj) continue;
            for(auto &sub : sel.getSubNames()) {
                if(boost::starts_with(sub,editSub))
                    sels[sub.c_str()+editSub.size()] = 1;
            }
            break;
        }
        const auto items = ui->elementList->selectedItems();
        for(auto item : items) {
            std::string name(qPrintable(item->data(Qt::UserRole+1).value<QString>()));
            auto &v = sels[name];
            if(!v)
                Selection().addSelection(editDoc.c_str(),
                        editObj.c_str(), (editSub+name).c_str());
            v = 2;
        }
        for(auto &v : sels) {
            if(v.second!=2) {
                Selection().rmvSelection(editDoc.c_str(),
                        editObj.c_str(), (editSub+v.first).c_str());
            }
        }
        busy = false;
    }
};

namespace {
class ElementColorsSelectionGate : public Gui::SelectionGate
{
public:
    ElementColorsSelectionGate(const std::shared_ptr<ElementColors::Private> d)
        :d(d)
    {}

    virtual bool allow(App::Document *doc, App::DocumentObject *obj, const char *subname) override {
        return d->allow(doc, obj, subname);
    }

    std::shared_ptr<ElementColors::Private> d;
};
} // anonymous namespace

/* TRANSLATOR Gui::TaskElementColors */

ElementColors::ElementColors(ViewProviderDocumentObject* vp, bool noHide)
    :d(new Private(vp))
{
    d->ui->setupUi(this);

    setupConnections();

    d->ui->objectLabel->setText(QString::fromUtf8(vp->getObject()->Label.getValue()));
    d->ui->elementList->setMouseTracking(true); // needed for itemEntered() to work

    if (noHide) {
        d->ui->hideSelection->setVisible(false);
    }

    d->ui->onTop->setChecked(ViewParams::getColorOnTop());

    Selection().addSelectionGate(new ElementColorsSelectionGate(d),ResolveMode::NoResolve);

    //NOLINTBEGIN
    d->connectDelDoc = Application::Instance->signalDeleteDocument.connect(std::bind
        (&ElementColors::slotDeleteDocument, this, sp::_1));
    d->connectDelObj = Application::Instance->signalDeletedObject.connect(std::bind
        (&ElementColors::slotDeleteObject, this, sp::_1));
    //NOLINTEND

    d->populate();
    QMetaObject::invokeMethod(this, "checkEdit", Qt::QueuedConnection);
}

ElementColors::~ElementColors()
{
    Selection().rmvSelectionGate();
}

void ElementColors::checkEdit() {
    d->edit(this, true);
}

void ElementColors::setupConnections()
{
    std::array<const char *, 4> names = {"MapFaceColor", "MapLineColor", "MapPointColor", "ForceMapColors"};
    for (auto name : names) {
        auto checkbox = findChild<QCheckBox*>(QString::fromUtf8(name));
        if (checkbox) {
            if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(d->vp->getPropertyByName(name))) {
                checkbox->setChecked(prop->getValue());
                QObject::connect(checkbox, &QCheckBox::toggled, [this, name](bool checked) {
                    auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(d->vp->getPropertyByName(name));
                    if (prop) {
                        prop->setValue(checked);
                        d->touched = true;
                    }
                });
            } else
                checkbox->hide();
        }
    }

    connect(d->ui->removeSelection, &QPushButton::clicked,
            this, &ElementColors::onRemoveSelectionClicked);
    connect(d->ui->addSelection, &QPushButton::clicked,
            this, &ElementColors::onAddSelectionClicked);
    connect(d->ui->removeAll, &QPushButton::clicked,
            this, &ElementColors::onRemoveAllClicked);
    connect(d->ui->elementList, &QListWidget::itemDoubleClicked,
            this, &ElementColors::onElementListItemDoubleClicked);
    connect(d->ui->elementList, &QListWidget::itemSelectionChanged,
            this, &ElementColors::onElementListItemSelectionChanged);
    connect(d->ui->elementList, &QListWidget::itemEntered,
            this, &ElementColors::onElementListItemEntered);
    connect(d->ui->onTop, &QCheckBox::clicked,
            this, &ElementColors::onTopClicked);
    connect(d->ui->hideSelection, &QPushButton::clicked,
            this, &ElementColors::onHideSelectionClicked);
    connect(d->ui->boxSelect, &QPushButton::clicked,
            this, &ElementColors::onBoxSelectClicked);
}

void ElementColors::onTopClicked(bool checked) {
    ViewParams::setColorOnTop(checked);
    d->setOnTop();
}

void ElementColors::slotDeleteDocument(const Document& Doc)
{
    if (d->vpDoc==&Doc || d->editDoc==Doc.getDocument()->getName()) {
        d->deinit();
        Control().closeDialog();
    }
}

void ElementColors::slotDeleteObject(const ViewProvider& obj)
{
    if (d->vp==&obj) {
        d->deinit();
        Control().closeDialog();
    }
}

void ElementColors::onRemoveSelectionClicked()
{
    d->removeItems();
}

void ElementColors::onBoxSelectClicked()
{
    auto cmd = Application::Instance->commandManager().getCommandByName("Std_BoxElementSelection");
    if(cmd)
        cmd->invoke(0);
}

void ElementColors::onHideSelectionClicked() {
    auto sels = Selection().getSelectionEx(d->editDoc.c_str(), App::DocumentObject::getClassTypeId(), ResolveMode::NoResolve);
    for(auto &sel : sels) {
        if(d->editObj!=sel.getFeatName())
            continue;
        const auto &subs = sel.getSubNames();
        if(!subs.empty()) {
            for(auto &sub : subs) {
                if(boost::starts_with(sub,d->editSub)) {
                    auto name = Data::noElementName(sub.c_str()+d->editSub.size());
                    name += ViewProvider::hiddenMarker();
                    d->addItem(-1,name.c_str());
                }
            }
            d->apply();
        }
        return;
    }
}

void ElementColors::onAddSelectionClicked()
{
    d->edit(this, false);
}

void ElementColors::Private::edit(QWidget *parent, bool elementOnly)
{
    auto sels = Selection().getSelectionEx(editDoc.c_str(), App::DocumentObject::getClassTypeId(), ResolveMode::NoResolve);
    int count = ui->elementList->count();
    items.clear();
    if(sels.empty()) {
        if (elementOnly)
            return;
        addItem(-1,"Face",true);
    } else {
        for(auto &sel : sels) {
            if(editObj!=sel.getFeatName())
                continue;
            const auto &subs = sel.getSubNames();
            if(subs.empty()) {
                if (elementOnly)
                    continue;
                addItem(-1,"Face",true);
                break;
            }
            for(auto &sub : subs) {
                if(!boost::starts_with(sub,editSub))
                    continue;
                std::string s(sub.c_str()+editSub.size());
                if(s.empty() || s.back() == '.')
                    s += "Face";
                addItem(-1,s.c_str(),true);
            }
            break;
        }
    }
    if(items.size()) {
        auto color = items.front()->data(Qt::UserRole).value<QColor>();
        QColorDialog cd(color, parent);
        cd.setOptions(QColorDialog::ShowAlphaChannel|QColorDialog::DontUseNativeDialog);
        // Seems some version of Qt has to explicitly set the current color
        cd.setCurrentColor(color);
        if (cd.exec()!=QDialog::Accepted) {
            while(ui->elementList->count() > count) {
                auto item = ui->elementList->takeItem(ui->elementList->count()-1);
                elements.erase(item->data(Qt::UserRole+1).toString().toUtf8().constData());
            }
            touched = true;
            return;
        }
        color = cd.selectedColor();
        for(auto item : items) {
            item->setData(Qt::UserRole,color);
            px.fill(color);
            item->setIcon(QIcon(px));
        }
        apply();
    }
}

void ElementColors::onRemoveAllClicked()
{
    d->removeAll();
}

bool ElementColors::accept()
{
    d->accept();
    Application::Instance->setEditDocument(nullptr);
    return true;
}

bool ElementColors::reject()
{
    d->reset();
    Application::Instance->setEditDocument(nullptr);
    return true;
}

void ElementColors::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        d->ui->retranslateUi(this);
    }
}

void ElementColors::leaveEvent(QEvent *e) {
    QWidget::leaveEvent(e);
    Selection().rmvPreselect();
}

void ElementColors::onElementListItemEntered(QListWidgetItem *item) {
    std::string name(qPrintable(item->data(Qt::UserRole+1).value<QString>()));
    const char *hidden = ViewProvider::hasHiddenMarker(name.c_str());
    if(hidden)
        name.resize(name.size()-std::strlen(hidden));
    Selection().setPreselect(d->editDoc.c_str(),
            d->editObj.c_str(), (d->editSub+name).c_str(),0,0,0,
            d->ui->onTop->isChecked() ? Gui::SelectionChanges::MsgSource::TreeView
                                      : Gui::SelectionChanges::MsgSource::Internal);
}

void ElementColors::onElementListItemSelectionChanged() {
    d->onSelectionChanged();
}

void ElementColors::onSelectionChanged(const SelectionChanges& msg)
{
    d->onSelectionChanged(msg);
}

void ElementColors::onElementListItemDoubleClicked(QListWidgetItem *item) {
    d->editItem(this,item);
}

/* TRANSLATOR Gui::TaskElementColors */

TaskElementColors::TaskElementColors(ViewProviderDocumentObject* vp, bool noHide)
{
    widget = new ElementColors(vp,noHide);
    taskbox = new TaskView::TaskBox(
        QPixmap(), widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskElementColors::~TaskElementColors() = default;

void TaskElementColors::open()
{
}

void TaskElementColors::clicked(int)
{
}

bool TaskElementColors::accept()
{
    return widget->accept();
}

bool TaskElementColors::reject()
{
    return widget->reject();
}

#include "moc_TaskElementColors.cpp"
