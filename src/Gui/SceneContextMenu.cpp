/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#include "PreCompiled.h"

#include <cctype>
#include <cstring>
#include <functional>
#include <map>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QMenu>

#include <Inventor/SbVec3f.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Exception.h>

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "Fw/FwImage.h"
#include "MenuManager.h"
#include "SceneContextMenu.h"
#include "SceneControl.h"
#include "SceneControlP.h"
#include "SceneServeSource.h"
#include "Selection.h"
#include "ViewProviderDocumentObject.h"
#include "ViewerContext.h"

using namespace Gui;
using namespace Gui::SceneControlDetail;

namespace
{

/// The menu each client has open, one at most: a new right click replaces
/// it, as a new popup replaces the last on the desktop
std::map<uint64_t, std::unique_ptr<SceneContextMenu>>& openMenus()
{
    static std::map<uint64_t, std::unique_ptr<SceneContextMenu>> menus;
    return menus;
}

/// Let a menu go after the call that ran one of its entries has unwound:
/// the entry's code may still be on the stack, and its objects are the
/// menu's children
void release(std::unique_ptr<SceneContextMenu> menu)
{
    if (!menu)
        return;
    // An entry's preview goes with the menu, as a popup's does
    menu->endPreview();
    menu.release()->deleteLater();
}

/// A menu text as the eye reads it: a mnemonic's '&' dropped, '&&' one '&'
QString plainText(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('&')) {
            if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
                out += QLatin1Char('&');
                ++i;
            }
            continue;
        }
        out += text[i];
    }
    return out;
}

const char* kindName(SceneContextMenu::Kind kind)
{
    switch (kind) {
        case SceneContextMenu::Kind::Command:
            return "command";
        case SceneContextMenu::Kind::Edit:
            return "edit";
        case SceneContextMenu::Kind::FinishEdit:
            return "finishEdit";
        case SceneContextMenu::Kind::Local:
            return "local";
        case SceneContextMenu::Kind::Pick:
            return "pick";
        default:
            return "action";
    }
}

/// No separator first, last or twice in a row: the desktop's QMenu hides
/// those by itself, a browser's list does not
void tidySeparators(QJsonArray& items)
{
    QJsonArray out;
    bool pending = false;
    for (const QJsonValue& v : items) {
        if (v.toObject().value(QLatin1String("separator")).toBool()) {
            pending = !out.isEmpty();
            continue;
        }
        if (pending) {
            QJsonObject sep;
            sep[QLatin1String("separator")] = true;
            out.append(sep);
            pending = false;
        }
        out.append(v);
    }
    items = out;
}

void describeInto(QMenu& menu,
                  const std::function<QJsonObject(QAction*, int)>& entry,
                  int& next,
                  QJsonArray& out)
{
    for (QAction* action : menu.actions()) {
        if (!action->isVisible())
            continue;
        QJsonObject item;
        if (action->isSeparator()) {
            item[QLatin1String("separator")] = true;
            out.append(item);
            continue;
        }
        if (QMenu* sub = action->menu()) {
            QJsonArray items;
            describeInto(*sub, entry, next, items);
            tidySeparators(items);
            if (items.isEmpty())
                continue;
            item[QLatin1String("text")] = plainText(sub->title().isEmpty() ? action->text()
                                                                            : sub->title());
            const QIcon icon = sub->icon().isNull() ? action->icon() : sub->icon();
            if (!icon.isNull())
                item[QLatin1String("icon")] = Fw::ImageStore::instance().ofIcon(icon, QSize(32, 32));
            item[QLatin1String("enabled")] = action->isEnabled();
            item[QLatin1String("items")] = items;
            out.append(item);
            continue;
        }
        out.append(entry(action, next++));
    }
}

/// The client's view of \a doc: its mirror, null when it has stated no
/// camera or the document is not served
ViewerContext* clientViewer(App::Document* doc, uint64_t client)
{
    SceneServeSource* source = doc ? SceneServeSource::sourceFor(doc) : nullptr;
    return source ? source->viewerFor(client) : nullptr;
}

}  // namespace

SceneContextMenu::SceneContextMenu(uint64_t client,
                                   const std::string& doc,
                                   const App::SubObjectT& target,
                                   std::vector<App::SubObjectT> picks,
                                   QObject* parent)
    : QObject(parent)
    , _client(client)
    , _doc(doc)
    , _target(target)
    , _picks(std::move(picks))
{
    static int tokens = 0;
    _token = ++tokens;
}

SceneContextMenu::~SceneContextMenu() = default;

ViewProviderDocumentObject* SceneContextMenu::targetViewProvider() const
{
    if (_target.getObjectName().empty())
        return nullptr;
    // The object the click is about, the deepest one on its path: what the
    // tree's item for the pick is on the desktop (TreeWidget::setupObjectMenu)
    App::DocumentObject* obj = _target.getSubObject();
    if (!obj || !obj->isAttachedToDocument())
        return nullptr;
    return dynamic_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(obj));
}

QJsonArray SceneContextMenu::build(Render::ClientAccess access)
{
    App::Document* doc = App::GetApplication().getDocument(_doc.c_str());
    ViewerContext* viewer = clientViewer(doc, _client);
    // The entries ask for the selection and the active document as they are
    // set up, and those are the client's inside its view
    ViewerScope scope(viewer);

    _menu = std::make_unique<QMenu>();
    _entries.clear();

    // An edit mode with a right click of its own -- the sketcher's -- has
    // it first, as on the desktop the view's popup is then never reached;
    // only this client's edit, in the view it runs in
    Gui::Document* gdoc = doc ? Application::Instance->getDocument(doc) : nullptr;
    ViewProvider* editVp = gdoc ? gdoc->getInEdit() : nullptr;
    if (editVp && viewer && editVp->getEditViewer() == viewer) {
        MenuItem editMenu;
        if (editVp->editContextMenu(&editMenu)) {
            _inEdit = true;
            if (auto vpd = dynamic_cast<ViewProviderDocumentObject*>(editVp))
                _editObject = App::DocumentObjectT(vpd->getObject());
            MenuManager::getInstance()->setupContextMenu(&editMenu, *_menu);
            // The desktop reaches "Pick geometry" in an edit by its
            // shortcut (Std_PickGeometry); a browser, and a phone above
            // all, has no keyboard to press it with, so it heads the menu
            addPickMenu(nullptr);
            return describe(access);
        }
    }

    // The active workbench's entries for a 3D view, as
    // NavigationStyle::openPopupMenu asks for them
    MenuItem view;
    Application::Instance->setupContextMenu("View", &view);
    MenuManager::getInstance()->setupContextMenu(&view, *_menu);

    // Then the picked object's own, first, as the desktop puts them
    // (TreeWidget::_setupObjectMenu, built here without the tree: a served
    // document may be one the tree does not show). Its edit-mode entries
    // come to onEditEntry() rather than to the tree.
    QAction* objAction = nullptr;
    if (ViewProviderDocumentObject* vp = targetViewProvider()) {
        _builtFrom = vp;
        auto objMenu = new QMenu(_menu.get());
        vp->setupContextMenu(objMenu, this, SLOT(onEditEntry()));
        objMenu->setTitle(QString::fromUtf8(vp->getObject()->Label.getValue()));
        objMenu->setIcon(vp->getIcon());
        if (vp->isEditing())
            _finishEdit = objMenu->addAction(tr("Finish editing"));
        if (!objMenu->actions().isEmpty()) {
            QAction* first = _menu->actions().isEmpty() ? nullptr : _menu->actions().front();
            objAction = _menu->insertMenu(first, objMenu);
            if (first)
                _menu->insertSeparator(first);
        }
    }
    addPickMenu(objAction);
    return describe(access);
}

QJsonArray SceneContextMenu::describe(Render::ClientAccess access)
{
    int next = 1;
    QJsonArray items;
    describeInto(*_menu,
                 [this, access](QAction* action, int id) {
                     return describeEntry(action, id, access);
                 },
                 next,
                 items);
    tidySeparators(items);
    return items;
}

void SceneContextMenu::addPickMenu(QAction* after)
{
    if (_picks.empty())
        return;
    // As the desktop's (NavigationStyle::openPopupMenu, SelectionMenu::
    // doPick): what the ray went through, under its element's kind, "Other"
    // last; an entry is the object's label and the element
    QString title = tr("Pick geometry");
    Command* cmd = Application::Instance->commandManager().getCommandByName("Std_PickGeometry");
    if (Action* action = cmd ? cmd->getAction() : nullptr)
        title = plainText(action->text());
    auto pickMenu = new QMenu(title, _menu.get());

    std::map<std::string, std::vector<int>> byKind;
    for (int i = 0; i < int(_picks.size()); ++i) {
        int index = -1;
        std::string element = _picks[i].getOldElementName(&index);
        if (index < 0 || element.empty())
            element.clear();
        else {
            element = element.substr(0, element.find_first_of("0123456789"));
            // An edit's own geometry ("edge", the sketcher's) under the
            // kind it is
            if (!element.empty())
                element[0] = char(std::toupper(static_cast<unsigned char>(element[0])));
        }
        byKind[element].push_back(i);
    }
    auto addKind = [&](const std::string& kind, const std::vector<int>& indices) {
        auto kindMenu = pickMenu->addMenu(kind.empty() ? tr("Other") : QString::fromUtf8(kind.c_str()));
        for (int i : indices) {
            const App::SubObjectT& sel = _picks[i];
            App::DocumentObject* sobj = sel.getSubObject();
            if (!sobj)
                continue;
            QString text = QString::fromUtf8(sobj->Label.getValue());
            const std::string element = sel.getOldElementName();
            if (!element.empty())
                text += QStringLiteral(" (%1)").arg(QString::fromUtf8(element.c_str()));
            QIcon icon;
            if (ViewProvider* vp = Application::Instance->getViewProvider(sobj))
                icon = vp->getIcon();
            QAction* action = kindMenu->addAction(icon, text);
            action->setProperty("fcPickIndex", i);
        }
    };
    for (const auto& kind : byKind) {
        if (!kind.first.empty())
            addKind(kind.first, kind.second);
    }
    auto other = byKind.find(std::string());
    if (other != byKind.end())
        addKind(other->first, other->second);

    // After the target's own submenu, ahead of the view's entries
    const QList<QAction*> actions = _menu->actions();
    const int at = after ? int(actions.indexOf(after)) + 1 : 0;
    QAction* before = at < actions.size() ? actions.at(at) : nullptr;
    _menu->insertMenu(before, pickMenu);
    if (before && !before->isSeparator())
        _menu->insertSeparator(before);
}

QJsonObject SceneContextMenu::describeEntry(QAction* action, int id, Render::ClientAccess access)
{
    Entry entry;
    entry.action = action;

    QJsonObject item;
    item[QLatin1String("id")] = id;
    const QString text = plainText(action->text());
    item[QLatin1String("text")] = text;
    const QString tip = action->toolTip();
    if (!tip.isEmpty() && plainText(tip) != text)
        item[QLatin1String("tip")] = tip;
    if (!action->shortcut().isEmpty())
        item[QLatin1String("shortcut")] = action->shortcut().toString(QKeySequence::NativeText);
    if (!action->icon().isNull())
        item[QLatin1String("icon")] =
            Fw::ImageStore::instance().ofIcon(action->icon(), QSize(32, 32));
    if (action->isCheckable()) {
        item[QLatin1String("checkable")] = true;
        item[QLatin1String("checked")] = action->isChecked();
    }

    bool enabled = action->isEnabled();
    auto owner = qobject_cast<Action*>(action->parent());
    Command* cmd = owner ? owner->command() : nullptr;
    const QVariant pick = action->property("fcPickIndex");
    if (action == _finishEdit) {
        entry.kind = Kind::FinishEdit;
    }
    else if (pick.isValid()) {
        entry.kind = Kind::Pick;
        entry.pick = pick.toInt();
        // What it names, as a selection push names an item: the browser
        // paints it while the pointer is over the entry
        const App::SubObjectT& sel = _picks[entry.pick];
        QJsonObject names;
        names[QLatin1String("obj")] = QString::fromUtf8(sel.getObjectName().c_str());
        names[QLatin1String("sub")] = QString::fromUtf8(sel.getSubName().c_str());
        // Geometry the edit draws, which the browser has no names for:
        // the host previews and paints it (contextMenu.hover)
        if (editOwned(sel))
            names[QLatin1String("edit")] = true;
        item[QLatin1String("pick")] = names;
    }
    else if (cmd) {
        entry.command = QString::fromUtf8(cmd->getName());
        item[QLatin1String("command")] = entry.command;
        const QString local = localAction(entry.command);
        if (_inEdit && entry.command == QLatin1String("Sketcher_LeaveSketch")) {
            // The sketch's way out is the edit's way out, run as the tree's
            // "Finish editing" is
            entry.kind = Kind::FinishEdit;
        }
        else if (!local.isEmpty()) {
            entry.kind = Kind::Local;
            item[QLatin1String("local")] = local;
            enabled = true;
        }
        else {
            entry.kind = Kind::Command;
            // The shared action's state is the desktop's answer, kept by a
            // timer against the desktop's view; this client's is asked here,
            // inside its view
            try {
                enabled = cmd->isActive();
            }
            catch (...) {
                enabled = false;
            }
        }
    }
    else if (action->property(ViewProviderDocumentObject::EditEntryProperty).toBool()) {
        // An entry that says all it does is enter its edit mode
        entry.kind = Kind::Edit;
        entry.direct = true;
    }
    else {
        // Qt answers no question about a connection's receiver, but a
        // disconnect says whether there was one; it is put back
        entry.kind = Kind::Action;
        if (QObject::disconnect(action, SIGNAL(triggered(bool)), this, SLOT(onEditEntry()))) {
            QObject::connect(action, SIGNAL(triggered(bool)), this, SLOT(onEditEntry()));
            entry.kind = Kind::Edit;
        }
        else if (QObject::disconnect(action, SIGNAL(triggered()), this, SLOT(onEditEntry()))) {
            QObject::connect(action, SIGNAL(triggered()), this, SLOT(onEditEntry()));
            entry.kind = Kind::Edit;
        }
    }

    QString reason;
    const bool ok = allowed(entry.kind, entry.command, access, &reason);
    item[QLatin1String("kind")] = QLatin1String(kindName(entry.kind));
    item[QLatin1String("enabled")] = enabled;
    item[QLatin1String("allowed")] = ok;
    if (!ok)
        item[QLatin1String("reason")] = reason;
    _entries.insert(id, entry);
    return item;
}

bool SceneContextMenu::allowed(Kind kind,
                               const QString& command,
                               Render::ClientAccess access,
                               QString* reason)
{
    auto refuse = [reason](const QString& why) {
        if (reason)
            *reason = why;
        return false;
    };
    // The browser's own camera: nothing runs here
    if (kind == Kind::Local)
        return true;
    // The desktop's owner, who takes the modal risk as at the machine
    // (docs/ShareAccess.md sec 2.2)
    if (access == Render::ClientAccess::Host)
        return true;
    if (access < Render::ClientAccess::Edit)
        return refuse(QStringLiteral("View only"));
    switch (kind) {
        case Kind::Edit:
        case Kind::FinishEdit:
            // What the edit and resetEdit ops admit
            return true;
        case Kind::Pick:
            // What a pick admits
            return true;
        case Kind::Command:
            if (isBrowserSafeCommand(command))
                return true;
            return refuse(QStringLiteral("Not run from a browser: it may open a dialog on the host"));
        default:
            return refuse(QStringLiteral("Runs on the desktop only: it may open a dialog on the host"));
    }
}

QString SceneContextMenu::localAction(const QString& command)
{
    static const std::map<QString, QString> actions {
        {QStringLiteral("Std_ViewFitAll"), QStringLiteral("fitAll")},
        {QStringLiteral("Std_ViewIsometric"), QStringLiteral("isometric")},
        {QStringLiteral("Std_ViewDimetric"), QStringLiteral("dimetric")},
        {QStringLiteral("Std_ViewTrimetric"), QStringLiteral("trimetric")},
        {QStringLiteral("Std_ViewTop"), QStringLiteral("top")},
        {QStringLiteral("Std_ViewBottom"), QStringLiteral("bottom")},
        {QStringLiteral("Std_ViewFront"), QStringLiteral("front")},
        {QStringLiteral("Std_ViewRear"), QStringLiteral("rear")},
        {QStringLiteral("Std_ViewRight"), QStringLiteral("right")},
        {QStringLiteral("Std_ViewLeft"), QStringLiteral("left")},
    };
    auto it = actions.find(command);
    return it == actions.end() ? QString() : it->second;
}

const char* SceneContextMenu::trigger(int item, Render::ClientAccess access, QString& message,
                                      bool extend)
{
    auto it = _entries.find(item);
    if (it == _entries.end()) {
        message = QString::number(item);
        return "UnknownItem";
    }
    const Entry entry = it.value();
    if (!entry.action) {
        message = QStringLiteral("the entry is gone");
        return "Stale";
    }
    if (!allowed(entry.kind, entry.command, access, &message))
        return access < Render::ClientAccess::Edit ? "ViewOnly" : "Refused";
    if (entry.kind == Kind::Local) {
        message = QStringLiteral("the browser runs this one itself");
        return "Local";
    }
    // An entry's code may hold the view provider it was built from
    if (_builtFrom && targetViewProvider() != _builtFrom) {
        message = QStringLiteral("the object changed");
        return "Stale";
    }
    App::Document* doc = App::GetApplication().getDocument(_doc.c_str());
    Gui::Document* gdoc = doc ? Application::Instance->getDocument(doc) : nullptr;
    if (!gdoc) {
        message = QString::fromUtf8(_doc.c_str());
        return "Stale";
    }
    ViewerContext* viewer = clientViewer(doc, _client);
    if (SceneServeSource::sourceFor(doc) && !viewer) {
        message = QStringLiteral("state a camera before editing");
        return "NoView";
    }

    if (entry.kind == Kind::Pick) {
        // As SelectionMenu::onPicked: the pick replaces the selection, or
        // with Ctrl joins it; in the client's view, so in its selection, as
        // its click would land (SceneServeSource::pickAndSelect)
        const App::SubObjectT& sel = _picks[entry.pick];
        if (!sel.getSubObject()) {
            message = QStringLiteral("the object is gone");
            return "Stale";
        }
        ViewerScope scope(viewer);
        SelectionNoTopParentCheck guard;
        Selection().rmvPreselect();
        _previewing = false;
        if (!extend) {
            Selection().selStackPush();
            Selection().clearSelection();
        }
        Selection().addSelection(sel.getDocumentName().c_str(),
                                 sel.getObjectName().c_str(),
                                 sel.getSubName().c_str());
        Selection().selStackPush();
        // Inside an edit the selection is the scene: the edit recolours its
        // geometry, and that is what the wire carries
        // (SceneServeSource::pickAndSelect)
        if (gdoc->getInEdit()) {
            if (SceneServeSource* source = SceneServeSource::sourceFor(doc))
                source->schedulePublish();
        }
        return nullptr;
    }

    if (entry.kind == Kind::FinishEdit) {
        // The tree's entry: leave, recompute, close the transaction. No
        // scope: resetEdit opens one over the view it ran in
        gdoc->resetEdit();
        doc->recompute();
        gdoc->commitCommand();
        return nullptr;
    }

    _editReply = QJsonObject();
    try {
        // As the desktop's popup runs it: in the view it was opened in, with
        // the clicked object as the selection's context
        ViewerScope scope(viewer);
        SelectionContext context(_target);
        if (entry.direct)
            runEdit(entry.action->data().toInt());
        else
            entry.action->trigger();
    }
    catch (Base::Exception& e) {
        message = QString::fromUtf8(e.what());
        return "Failed";
    }
    catch (std::exception& e) {
        message = QString::fromUtf8(e.what());
        return "Failed";
    }
    if (entry.kind == Kind::Edit && !_editReply.value(QLatin1String("ok")).toBool()) {
        message = _editReply.value(QLatin1String("message")).toString();
        const QString code = _editReply.value(QLatin1String("code")).toString();
        return code == QLatin1String("NoView") ? "NoView" : "EditRefused";
    }
    return nullptr;
}

bool SceneContextMenu::editOwned(const App::SubObjectT& sel) const
{
    App::DocumentObject* edited = _editObject.getObject();
    return edited && sel.getSubObject() == edited;
}

const char* SceneContextMenu::preview(int item, QString& message)
{
    App::Document* doc = App::GetApplication().getDocument(_doc.c_str());
    SceneServeSource* source = doc ? SceneServeSource::sourceFor(doc) : nullptr;
    ViewerContext* viewer = clientViewer(doc, _client);
    if (!source || !viewer) {
        message = QString::fromUtf8(_doc.c_str());
        return "Stale";
    }
    const App::SubObjectT* sel = nullptr;
    if (item) {
        auto it = _entries.find(item);
        if (it == _entries.end() || it.value().kind != Kind::Pick) {
            message = QString::number(item);
            return "UnknownItem";
        }
        sel = &_picks[it.value().pick];
        if (!editOwned(*sel)) {
            message = QStringLiteral("the browser previews this one itself");
            return "Local";
        }
    }
    // As the desktop's pick menu preselects the entry under its cursor
    // (SelectionMenu::onHover), in the client's view, where its edit
    // listens; nothing, or an entry that is not a pick, drops it
    ViewerScope scope(viewer);
    _previewing = sel != nullptr;
    if (sel)
        Selection().setPreselect(sel->getDocumentName().c_str(), sel->getObjectName().c_str(),
                                 sel->getSubName().c_str(), 0, 0, 0,
                                 SelectionChanges::MsgSource::TreeView);
    else
        Selection().rmvPreselect();
    source->schedulePublish();
    return nullptr;
}

void SceneContextMenu::endPreview()
{
    if (!_previewing)
        return;
    QString ignored;
    preview(0, ignored);
}

void SceneContextMenu::onEditEntry()
{
    auto action = qobject_cast<QAction*>(sender());
    if (!action) {
        _editReply = sceneControlError(QJsonValue(), "Stale", QStringLiteral("no entry"));
        return;
    }
    runEdit(action->data().toInt());
}

void SceneContextMenu::runEdit(int mode)
{
    ViewProviderDocumentObject* vp = targetViewProvider();
    App::Document* doc = App::GetApplication().getDocument(_doc.c_str());
    Gui::Document* gdoc = doc ? Application::Instance->getDocument(doc) : nullptr;
    if (!vp || !gdoc) {
        _editReply = sceneControlError(QJsonValue(), "Stale", QStringLiteral("the object is gone"));
        return;
    }
    // What the tree does with the entry (TreeWidget::onStartEditing) less
    // raising a desktop window, under the edit op's rules
    _editReply = enterClientEdit(QJsonValue(), gdoc, vp, mode, QString(), _client);
}

void Gui::installSceneContextMenuOps()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    // A right click: the menu for what the client's ray hits. Not a write --
    // a view-only connection gets it too, with only its own camera entries
    // allowed -- though the entries are built, as on the desktop.
    registerSceneControlOp(QStringLiteral("contextMenu"), false,
                           [](const QJsonObject& req, const std::string& boundDoc, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString docName = req.value(QLatin1String("doc")).toString();
        App::Document* doc = sceneControlDocument(req, boundDoc);
        if (!doc)
            return sceneControlError(id, "UnknownDocument", docName);
        SceneServeSource* source = SceneServeSource::sourceFor(doc);
        if (!source)
            return sceneControlError(id, "NotServed", QString::fromUtf8(doc->getName()));
        if (!source->viewerFor(client))
            return sceneControlError(id, "NoView", QStringLiteral("state a camera first"));
        const QJsonArray ray = req.value(QLatin1String("ray")).toArray();
        if (ray.size() != 6)
            return sceneControlError(id, "BadRequest", QStringLiteral("ray: [ox,oy,oz,dx,dy,dz]"));
        const SbVec3f origin(float(ray[0].toDouble()), float(ray[1].toDouble()),
                             float(ray[2].toDouble()));
        const SbVec3f dir(float(ray[3].toDouble()), float(ray[4].toDouble()),
                          float(ray[5].toDouble()));

        App::SubObjectT target;
        source->pickSubObject(origin, dir, client, target);
        // The desktop offers "Pick geometry" when the click hit anything
        std::vector<App::SubObjectT> picks;
        if (!target.getObjectName().empty())
            picks = source->pickAllSubObjects(origin, dir, client);

        auto& menus = openMenus();
        auto previous = menus.find(client);
        if (previous != menus.end()) {
            release(std::move(previous->second));
            menus.erase(previous);
        }
        auto menu = std::make_unique<SceneContextMenu>(client, doc->getName(), target,
                                                       std::move(picks));
        QJsonArray items;
        try {
            items = menu->build(sceneControlAccess());
        }
        catch (Base::Exception& e) {
            return sceneControlError(id, "Failed", QString::fromUtf8(e.what()));
        }

        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("menu")] = menu->token();
        reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
        if (App::DocumentObject* obj = target.getSubObject()) {
            QJsonObject about;
            about[QLatin1String("doc")] = QString::fromUtf8(target.getDocumentName().c_str());
            about[QLatin1String("obj")] = QString::fromUtf8(target.getObjectName().c_str());
            about[QLatin1String("sub")] = QString::fromUtf8(target.getSubName().c_str());
            about[QLatin1String("label")] = QString::fromUtf8(obj->Label.getValue());
            reply[QLatin1String("target")] = about;
        }
        else {
            reply[QLatin1String("target")] = QJsonValue::Null;
        }
        reply[QLatin1String("items")] = items;
        menus[client] = std::move(menu);
        return reply;
    });

    // An entry chosen: run as the desktop runs it, judged again here
    registerSceneControlOp(QStringLiteral("contextMenu.trigger"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const int token = req.value(QLatin1String("menu")).toInt(0);
        const int item = req.value(QLatin1String("item")).toInt(0);
        auto& menus = openMenus();
        auto it = menus.find(client);
        if (it == menus.end() || !it->second || it->second->token() != token)
            return sceneControlError(id, "Stale", QStringLiteral("no such menu open"));
        QString message;
        const bool extend = req.value(QLatin1String("extend")).toBool(false);
        const char* error = it->second->trigger(item, sceneControlAccess(), message, extend);
        // A popup is gone once an entry is chosen; one refused stays open
        // for another try
        if (!error || std::strcmp(error, "Stale") == 0) {
            auto found = menus.find(client);
            if (found != menus.end() && found->second && found->second->token() == token) {
                release(std::move(found->second));
                menus.erase(found);
            }
        }
        if (error)
            return sceneControlError(id, error, message);
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("item")] = item;
        return reply;
    });

    // A pick entry pointed at, or nothing (item 0): the host previews what
    // only it can draw -- the edit's own geometry (the entry's pick.edit)
    registerSceneControlOp(QStringLiteral("contextMenu.hover"), false,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const int token = req.value(QLatin1String("menu")).toInt(0);
        const int item = req.value(QLatin1String("item")).toInt(0);
        auto& menus = openMenus();
        auto it = menus.find(client);
        if (it == menus.end() || !it->second || it->second->token() != token)
            return sceneControlError(id, "Stale", QStringLiteral("no such menu open"));
        QString message;
        if (const char* error = it->second->preview(item, message))
            return sceneControlError(id, error, message);
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("item")] = item;
        return reply;
    });

    registerSceneControlOp(QStringLiteral("contextMenu.close"), false,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const int token = req.value(QLatin1String("menu")).toInt(0);
        auto& menus = openMenus();
        auto it = menus.find(client);
        if (it != menus.end() && it->second && (token == 0 || it->second->token() == token)) {
            release(std::move(it->second));
            menus.erase(it);
        }
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        return reply;
    });
}

void Gui::dropSceneContextMenu(uint64_t client)
{
    auto& menus = openMenus();
    auto it = menus.find(client);
    if (it != menus.end()) {
        release(std::move(it->second));
        menus.erase(it);
    }
}

#include "moc_SceneContextMenu.cpp"
