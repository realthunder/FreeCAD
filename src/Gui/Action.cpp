/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef _PreComp_
# include <QActionEvent>
# include <QActionGroup>
# include <QApplication>
# include <QButtonGroup>
# include <QCheckBox>
# include <QClipboard>
# if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#   include <QDesktopWidget>
# endif
# include <QElapsedTimer>
# include <QEvent>
# include <QHBoxLayout>
# include <QMenu>
# include <QMenuBar>
# include <QMessageBox>
# include <QRadioButton>
# include <QRegularExpression>
# include <QWindow>
# include <QScreen>
# include <QSpinBox>
# include <QTimer>
# include <QToolBar>
# include <QToolButton>
# include <QToolTip>
#endif

#include <QWidgetAction>

#include <algorithm>
#include <cctype>
#include <QSet>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/regex.hpp>

#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/AutoTransaction.h>
#include <App/Expression.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Parameter.h>
#include <Base/Tools.h>

#include "Action.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "CommandCompleter.h"
#include "DlgUndoRedo.h"
#include "DockWindowManager.h"
#include "PreferencePackManager.h"
#include "PreferencePages/DlgSettingsWorkbenchesImp.h"
#include "Document.h"
#include "EditorView.h"
#include "FileDialog.h"
#include "Macro.h"
#include "MainWindow.h"
#include "PieMenu.h"
#include "PythonEditor.h"
#include "SelectionView.h"
#include "ShortcutManager.h"
#include "ToolBarManager.h"
#include "Tools.h"
#include "UserSettings.h"
#include "View3DInventor.h"
#include "ViewProvider.h"
#include "ViewParams.h"
#include "WhatsThis.h"
#include "Widgets.h"
#include "Workbench.h"
#include "WorkbenchManager.h"


FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::Dialog;
namespace sp = std::placeholders;

/**
 * Constructs an action called \a name with parent \a parent. It also stores a pointer
 * to the command object.
 */
Action::Action (Command* pcCmd, QObject * parent)
  : QObject(parent)
  , _action(new QAction( this ))
  , _pcCmd(pcCmd)
{
    _action->setObjectName(QString::fromUtf8(_pcCmd->getName()));
    _connection = connect(_action, &QAction::triggered, this, &Action::onActivated);
}

Action::Action (Command* pcCmd, QAction* action, QObject * parent)
  : QObject(parent)
  , _action(action)
  , _pcCmd(pcCmd)
{
    _action->setParent(this);
    _action->setObjectName(QString::fromUtf8(_pcCmd->getName()));
    _connection = connect(_action, &QAction::triggered, this, &Action::onActivated);
}

Action::~Action()
{
    delete _action;
}

/**
 * Adds this action to widget \a widget.
 */
void Action::addTo(QWidget *widget)
{
    widget->addAction(_action);
    ToolBarManager::getInstance()->checkToolBarIconSize(qobject_cast<QToolBar*>(widget));
}

/**
 * Activates the command.
 */
void Action::onActivated ()
{
    command()->invoke(0, Command::TriggerAction);
}

/**
 * Sets whether the command is toggled.
 */
void Action::onToggled(bool toggle)
{
    command()->invoke(toggle ? 1 : 0, Command::TriggerAction);
}

void Action::setCheckable(bool check)
{
    if (check == _action->isCheckable()) {
        return;
    }

    _action->setCheckable(check);

    if (check) {
        disconnect(_connection);
        _connection = connect(_action, &QAction::toggled, this, &Action::onToggled);
    }
    else {
        disconnect(_connection);
        _connection = connect(_action, &QAction::triggered, this, &Action::onActivated);
    }
}

void Action::setChecked(bool check, bool no_signal)
{
    if (_action->isChecked() != check) {
        if (no_signal) {
            QSignalBlocker blocker(_action);
            _action->setChecked(check);
        } else {
            _action->setChecked(check);
        }
    }
    // Emitted even when the action was already in that state. The
    // buttons standing in for this action in menus (Action::addWidget)
    // follow this signal and nothing else, and the action can reach the
    // state without them: an exclusive QActionGroup unchecks its other
    // members itself, so a shortcut that changes the mode leaves every
    // one of them agreeing with the action and disagreeing with the
    // button. A later sync would then have nothing to report and the
    // stale tick would survive it.
    Q_EMIT actionChecked(check);
}

bool Action::isChecked() const
{
    return _action->isChecked();
}

/**
 * Sets whether the action is enabled.
 */
void Action::setEnabled(bool enable)
{
    _action->setEnabled(enable);
}

bool Action::isEnabled() const
{
    return _action->isEnabled();
}

void Action::setVisible(bool visible)
{
    _action->setVisible(visible);
}

void Action::setShortcut(const QString & key)
{
    _action->setShortcut(key);
    setToolTip(_tooltip, _title);
}

QKeySequence Action::shortcut() const
{
    return _action->shortcut();
}

void Action::setIcon (const QIcon & icon)
{
    _action->setIcon(icon);
    if (!icon.isNull())
        ToolBarManager::getInstance()->checkToolBarIconSize(_action);
    setToolTip(_tooltip, _title);
}

QIcon Action::icon () const
{
    return _action->icon();
}

void Action::setStatusTip(const QString & text)
{
    _action->setStatusTip(text);
}

QString Action::statusTip() const
{
    return _action->statusTip();
}

void Action::setText(const QString & text)
{
    _action->setText(text);
    if (_title.isEmpty()) {
        setToolTip(_tooltip);
    }
}

QString Action::text() const
{
    return _action->text();
}

void Action::setToolTip(const QString & text, const QString & title)
{
    _tooltip = text;
    _title = title;
    _action->setToolTip(createToolTip(text,
                title.isEmpty() ? _action->text() : title,
                _action->font(),
                _action->shortcut().toString(QKeySequence::NativeText),
                _pcCmd));
}

QString Action::cleanTitle(const QString & title)
{
    QString text(title);
    // Deal with QAction title mnemonic
    static QRegularExpression re(QStringLiteral("&(.)"));
    text.replace(re, QStringLiteral("\\1"));

    // Probably not a good idea to trim ending punctuation
#if 0
    // Trim line ending punctuation
    static QRegularExpression rePunct(QStringLiteral("[[:punct:]]+$"));
    text.replace(rePunct, QString());
#endif
    return text;
}

QString Action::commandToolTip(const Command *cmd, bool richFormat)
{
    if (!cmd) {
        return {};
    }

    if (richFormat) {
        if (auto action = cmd->getAction()) {
            return action->_action->toolTip();
        }
    }

    QString title, tooltip;
    if (dynamic_cast<const MacroCommand*>(cmd)) {
        if (auto txt = cmd->getMenuText()) {
            title = QString::fromUtf8(txt);
        }
        if (auto txt = cmd->getToolTipText()) {
            tooltip = QString::fromUtf8(txt);
        }
    } else {
        if (auto txt = cmd->getMenuText()) {
            title = qApp->translate(cmd->className(), txt);
        }
        if (auto txt = cmd->getToolTipText()) {
            tooltip = qApp->translate(cmd->className(), txt);
        }
    }

    if (!richFormat) {
        return tooltip;
    }
    return createToolTip(tooltip, title, QFont(), cmd->getShortcut(), cmd);
}

QString Action::commandMenuText(const Command *cmd)
{
    if (!cmd) {
        return {};
    }

    QString title;
    if (auto action = cmd->getAction()) {
        title = action->text();
    }
    else if (dynamic_cast<const MacroCommand*>(cmd)) {
        if (auto txt = cmd->getMenuText()) {
            title = QString::fromUtf8(txt);
        }
    } else {
        if (auto txt = cmd->getMenuText()) {
            title = qApp->translate(cmd->className(), txt);
        }
    }
    if (title.isEmpty()) {
        title = QString::fromUtf8(cmd->getName());
    }
    else {
        title = cleanTitle(title);
    }
    return title;
}

QString Action::createToolTip(QString helpText,
                              const QString & title,
                              const QFont &font,
                              const QString &shortCut,
                              const Command *command,
                              QString iconPath)
{
    QString text = cleanTitle(title);

    if (text.isEmpty()) {
        return helpText;
    }

    // The following code tries to make a more useful tooltip by inserting at
    // the beginning of the tooltip the action title in bold followed by the
    // shortcut.
    //
    // The long winding code is to deal with the fact that Qt will auto wrap
    // a rich text tooltip but the width is too short. We can escape the auto
    // wrappin using <p style='white-space:pre'>.

    QString shortcut = shortCut;
    if (!shortcut.isEmpty() && helpText.endsWith(shortcut)) {
        helpText.resize(helpText.size() - shortcut.size());
    }
    if (!shortcut.isEmpty()) {
        shortcut = QStringLiteral(" (%1)").arg(shortcut);
    }

    QString tooltip = QStringLiteral(
            "<p style='white-space:pre; margin-bottom:0.5em;'><b>%1</b>%2</p>").arg(
            text.toHtmlEscaped(), shortcut.toHtmlEscaped());

    QString cmdName;
    if (command && command->getName()) {
        auto pCmd = command;
        cmdName = QString::fromUtf8(command->getName());
        int idx = -1;
        if (auto action = pCmd->getChildAction(-1, &idx)) {
            auto cmd = action->command();
            if (cmd && cmd->getName()) {
                cmdName = QStringLiteral("%1 (%2:%3)")
                    .arg(QString::fromUtf8(cmd->getName()))
                    .arg(cmdName)
                    .arg(idx);
                pCmd = cmd;
                idx = -1;
            }
        }
        if (idx >= 0) {
            cmdName = QStringLiteral("%1:%2").arg(cmdName).arg(idx);
        }
        if (iconPath.isEmpty() && ViewParams::getToolTipIconSize() > 0) {
            if (auto pixmap = pCmd->getPixmap()) {
                auto path = BitmapFactory().getIconPath(pixmap);
                if (path)
                    iconPath = QString::fromUtf8(path);
            }
        }
        cmdName = QStringLiteral("<p style='white-space:pre; margin-top:0.5em;'><i>%1</i></p>")
            .arg(cmdName.toHtmlEscaped());
    }

    QString img;
    if (!iconPath.isEmpty() && ViewParams::getToolTipIconSize() > 0) {
        img = QStringLiteral("<img src='%1' style='float:right'/>")
            .arg(iconPath.toHtmlEscaped());
        if (cmdName.size() > title.size() + shortcut.size()) {
            tooltip = img + tooltip;
            img.clear();
        }
    }

    if (!shortcut.isEmpty() && helpText.endsWith(shortcut))
        helpText.resize(helpText.size() - shortcut.size());

    if (helpText.isEmpty()
            || helpText.compare(text, Qt::CaseInsensitive)==0
            || helpText.compare(title, Qt::CaseInsensitive)==0)
    {
        return tooltip + cmdName + img;
    }
    if (Qt::mightBeRichText(helpText)) {
        // already rich text, so let it be to avoid duplicated unwrapping
        return img + tooltip + helpText + cmdName;
    }

    tooltip += QStringLiteral(
            "<p style='white-space:pre; margin:0;'>");

    // If the user supplied tooltip contains line break, we shall honour it.
    if (helpText.indexOf(QLatin1Char('\n')) >= 0)
        tooltip += helpText.toHtmlEscaped() + QStringLiteral("</p>") ;
    else {
        // If not, try to end the non wrapping paragraph at some pre defined
        // width, so that the following text can wrap at that width.
        float tipWidth = 400;
        QFontMetrics fm(font);
        int width = QtTools::horizontalAdvance(fm, helpText);
        if (width <= tipWidth)
            tooltip += helpText.toHtmlEscaped() + QStringLiteral("</p>") ;
        else {
            int index = tipWidth / width * helpText.size();
            // Try to only break at white space
            for(int i=0; i<50 && index<helpText.size(); ++i, ++index) {
                if (helpText[index] == QLatin1Char(' '))
                    break;
            }
            tooltip += helpText.left(index).toHtmlEscaped()
                + QStringLiteral("</p>")
                + helpText.right(helpText.size()-index).trimmed().toHtmlEscaped();
        }
    }
    if (img.size() && helpText.size() < text.size() + shortcut.size())
        return tooltip + cmdName + img;
    return img + tooltip + cmdName;
}

QString Action::toolTip() const
{
    return _tooltip;
}

void Action::setWhatsThis(const QString & text)
{
    _action->setWhatsThis(text);
}

QString Action::whatsThis() const
{
    return _action->whatsThis();
}

void Action::setMenuRole(QAction::MenuRole menuRole)
{
    _action->setMenuRole(menuRole);
}

QAction *
Action::addCheckBox(QMenu *menu,
                    const QString &txt,
                    bool checked,
                    QCheckBox **checkbox,
                    const QString &tooltip,
                    const QIcon &icon)
{
    return addCheckBox(menu, txt, tooltip, icon, checked, checkbox);
}

QAction *
Action::addCheckBox(QMenu *menu,
                    const QString &txt,
                    const QString &tooltip,
                    const QIcon &icon,
                    bool checked,
                    QCheckBox **_checkbox,
                    const QString &shortcut)
{
    auto checkbox = new QCheckBox(menu);
    checkbox->setText(txt);
    checkbox->setChecked(checked);
    if (_checkbox) *_checkbox = checkbox;
    auto action = addWidget(menu, txt, tooltip, checkbox, false, icon, shortcut);
    action->setCheckable(true);
    action->setChecked(checked);
    // setChecked emits toggled itself, so forwarding toggled as well
    // would deliver the state change twice -- and a command invoked
    // twice per click is not always idempotent (Std_DrawStyleShadow
    // reads its second call as "already in shadow mode" and toggles the
    // light manipulator on entry).
    QObject::connect(checkbox, &QCheckBox::toggled, action, &QAction::setChecked);
    return action;
}

QAction *
Action::addRadioButton(QMenu *menu,
                       const QString &txt,
                       const QString &tooltip,
                       const QIcon &icon,
                       bool checked,
                       QRadioButton **_radio,
                       const QString &shortcut)
{
    auto radio = new QRadioButton(menu);
    radio->setText(txt);
    radio->setChecked(checked);
    // Each entry is its own widget action with its own container, so the
    // siblings Qt would auto-exclude against are not siblings. The caller
    // puts them in a QButtonGroup instead; auto-exclusive would otherwise
    // make each one individually un-uncheckable and collectively free.
    radio->setAutoExclusive(false);
    if (_radio) *_radio = radio;
    auto action = addWidget(menu, txt, tooltip, radio, false, icon, shortcut);
    action->setCheckable(true);
    action->setChecked(checked);
    // One connection, not two: see addCheckBox.
    QObject::connect(radio, &QRadioButton::toggled, action, &QAction::setChecked);
    return action;
}

class MenuFocusEventFilter: public QObject {
public:
    MenuFocusEventFilter(QMenu *menu, QAction *action, QWidget *focusWidget=nullptr)
        :QObject(menu)
        ,menu(menu), action(action), focusWidget(focusWidget)
    {}

    bool eventFilter(QObject *, QEvent *e) {
        if (e->type() == QEvent::Enter) {
            menu->setActiveAction(action);
            if (focusWidget)
                focusWidget->setFocus();
        }
        return false;
    }

    QMenu *menu;
    QAction *action;
    QWidget *focusWidget;
};

QAction *
Action::addWidget(QMenu *menu,
                  const QString &txt,
                  const QString &tooltip,
                  QWidget *w,
                  bool needLabel,
                  const QIcon &icon,
                  const QString &shortcut)
{
    QWidgetAction *wa = new QWidgetAction(menu);
    QWidget *widget = new QWidget(menu);
    QHBoxLayout *layout = new QHBoxLayout(widget);
    widget->setLayout(layout);
    if (!icon.isNull()) {
        QLabel *iconLabel = new QLabel(widget);
        // label->installEventFilter(new MenuFocusEventFilter(menu, wa));
        int s = ToolBarManager::getInstance()->toolBarIconSize();
        QSize iconSize(s, s);
        auto size = icon.actualSize(iconSize);
        if (size.height() < iconSize.height()) {
            iconSize.setWidth(static_cast<float>(iconSize.height())/size.height()*size.width());
        }
        iconLabel->setPixmap(icon.pixmap(iconSize));
        layout->addWidget(iconLabel);
    }
    if (needLabel) {
        QLabel *label = new QLabel(widget);
        label->installEventFilter(new MenuFocusEventFilter(menu, wa));
        label->setText(txt);
        layout->addWidget(label);
    }
    layout->addWidget(w);
    layout->setContentsMargins(4,0,4,0);
    if (!icon.isNull() || needLabel || !shortcut.isEmpty())
        layout->addStretch();
    if (!shortcut.isEmpty()) {
        // A widget action draws none of the furniture a plain menu item
        // gets for free, and the shortcut column is the piece that is
        // missed: an entry that used to advertise its accelerator on the
        // right should keep doing so. Dimmed and spaced like the style's
        // own, so the row still reads as a menu row.
        auto accel = new QLabel(widget);
        accel->setText(shortcut);
        accel->setEnabled(false);
        accel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addSpacing(16);
        layout->addWidget(accel);
    }
    widget->setFocusProxy(w);
    widget->setFocusPolicy(Qt::TabFocus);
    w->installEventFilter(new MenuFocusEventFilter(menu, wa, w));
    w->setFocusPolicy(Qt::TabFocus);
    wa->setDefaultWidget(widget);
    wa->setToolTip(tooltip);
    // A widget action's row is drawn by the widget, so QMenu never gets
    // to render the action's tooltip over it: on the action alone the
    // text is set and never seen. Put it on the container as well, which
    // is the whole row -- children with no tooltip of their own let the
    // help event propagate up to it.
    widget->setToolTip(tooltip);
    // wa->setStatusTip(tooltip);
    wa->setVisible(true);
    wa->setText(txt);
    menu->addAction(wa);
    return wa;
}

// --------------------------------------------------------------------

/**
 * Constructs an action called \a name with parent \a parent. It also stores a pointer
 * to the command object.
 */
ActionGroup::ActionGroup(Command* pcCmd, QObject* parent)
    : Action(pcCmd, parent)
    , _group(nullptr)
    , _dropDown(false)
    , _isMode(false)
{
    _group = new QActionGroup(this);
    connect(_group, &QActionGroup::triggered, this, qOverload<QAction*>(&ActionGroup::onActivated));
}

ActionGroup::~ActionGroup()
{
    delete _group;
}

/**
 * Fill \a menu with a group's actions, rendering every checkable one as a
 * button widget action rather than a plain checkable entry.
 *
 * The point is that a widget action handles its own mouse events, so
 * clicking one does not dismiss the menu — the list can be walked with
 * the result visible under it instead of reopening the menu per try —
 * and the state of every entry is visible at once rather than only the
 * highlighted row's.
 *
 * An \a exclusive group gets radio buttons in a shared QButtonGroup,
 * because that is what the group means: one draw style, one dock state.
 * Checkboxes would advertise that any combination is available. A
 * non-exclusive group keeps checkboxes.
 *
 * The button drives the action through its toggled signal (a
 * signal-to-signal connection, so Gui::Action::onToggled runs and the
 * command is invoked) and follows it back through Action::actionChecked,
 * which is how a state set elsewhere reaches the menu.
 */
static void fillGroupMenu(QMenu *menu, const QList<QAction*> &actions,
                          bool exclusive)
{
    QButtonGroup *group = nullptr;
    if (exclusive) {
        group = menu->findChild<QButtonGroup*>();
        if (!group) {
            group = new QButtonGroup(menu);
            group->setExclusive(true);
        }
    }
    for (auto action : actions) {
        if (!action->isCheckable()) {
            menu->addAction(action);
            continue;
        }
        // A widget action draws none of a menu item's furniture, so the
        // accelerator has to be passed along to be rendered.
        const QString accel =
            action->shortcut().toString(QKeySequence::NativeText);
        QAbstractButton *button = nullptr;
        QAction *wa = nullptr;
        if (exclusive) {
            QRadioButton *radio = nullptr;
            wa = Action::addRadioButton(menu, action->text(), action->toolTip(),
                                        action->icon(), action->isChecked(),
                                        &radio, accel);
            group->addButton(radio);
            button = radio;
        }
        else {
            QCheckBox *checkbox = nullptr;
            wa = Action::addCheckBox(menu, action->text(), action->toolTip(),
                                     action->icon(), action->isChecked(),
                                     &checkbox, accel);
            button = checkbox;
        }
        QObject::connect(wa, &QAction::toggled, action, &QAction::toggled);
        // ⚠️ No dismissal here, deliberately. A row built as a widget
        // action leaves the menu standing -- that is the point of the
        // list being a menu of radio buttons at all: comparing Shaded
        // against Flat Lines means trying them, and reopening the menu
        // between attempts is what made them hard to compare. Normal
        // menu items keep Qt's own behaviour and dismiss.
        //
        // A version of this closed the popup by hand and had to be
        // taken out again: hiding the menus is only half of
        // QMenuPrivate::hideUpToMenuBar, which also clears the menu
        // bar's current action and leaves keyboard mode, so the menu bar
        // went on believing its popup was up and swallowed the next
        // click on it. If a row ever does need to dismiss, hand the
        // popup a synthetic press beyond its own rect -- QMenu answers
        // that with hideUpToMenuBar itself -- rather than hiding it.
        // The row is a widget action now, so the command's own action is
        // no longer in any menu -- and an action that belongs to no
        // widget never receives QEvent::Shortcut, which would leave the
        // accelerator advertised in the row dead. Associate it with the
        // main window, which shows nothing for it and is where a
        // window-context shortcut wants to live anyway. Once only: the
        // menu is refilled on every toolbar rebuild, and a second
        // association makes Qt call the shortcut ambiguous and drop it.
        if (auto mainWindow = getMainWindow()) {
            if (!action->associatedObjects().contains(mainWindow))
                mainWindow->addAction(action);
        }
        if (auto parentAction = qobject_cast<Action*>(action->parent())) {
            QObject::connect(parentAction, &Action::actionChecked, button,
                [button](bool checked) {
                    QSignalBlocker blocker(button);
                    button->setChecked(checked);
                });
        }
    }
}

static inline QToolButton *setupMenuToolButton(QWidget *w)
{
    QToolButton* tb = w->findChildren<QToolButton*>().last();
    if (tb) {
        tb->setPopupMode(QToolButton::MenuButtonPopup);
        tb->setObjectName(QStringLiteral("qt_toolbutton_menubutton"));
    }
    return tb;
}

/**
 * Adds this action to widget \a w.
 */
void ActionGroup::addTo(QWidget *widget)
{
    // When adding an action that has defined a menu then shortcuts
    // of the menu actions don't work. To make this working we must
    // set the menu explicitly. This means calling QAction::setMenu()
    // and adding this action to the widget doesn't work.
    if (_dropDown) {
        if (widget->inherits("QMenu")) {
            auto menu = new QMenu(widget);
            auto item = qobject_cast<QMenu*>(widget)->addMenu(menu);
            item->setMenuRole(action()->menuRole());
            menu->setTitle(action()->text());
            fillGroupMenu(menu, actions(), isExclusive());

            QObject::connect(menu, &QMenu::aboutToShow, [this, menu]() {
                Q_EMIT aboutToShow(menu);
            });

            QObject::connect(menu, &QMenu::aboutToHide, [this, menu]() {
                Q_EMIT aboutToHide(menu);
            });
        }
        else if (widget->inherits("QToolBar")) {
            widget->addAction(action());
            auto tb = setupMenuToolButton(widget);
            auto menu = new QMenu(widget);
            fillGroupMenu(menu, actions(), isExclusive());
            tb->setMenu(menu);
            ToolBarManager::getInstance()->checkToolBarIconSize(static_cast<QToolBar*>(widget));

            QObject::connect(menu, &QMenu::aboutToShow, [this, menu]() {
                Q_EMIT aboutToShow(menu);
            });

            QObject::connect(menu, &QMenu::aboutToHide, [this, menu]() {
                Q_EMIT aboutToHide(menu);
            });
        }
        else {
            widget->addActions(actions()); // no drop-down
        }
    }
    else {
        widget->addActions(actions());
    }
}

void ActionGroup::setEnabled( bool check )
{
    Action::setEnabled(check);
    groupAction()->setEnabled(check);
}

void ActionGroup::setDisabled (bool check)
{
    Action::setEnabled(!check);
    groupAction()->setDisabled(check);
}

void ActionGroup::setExclusive (bool check)
{
    groupAction()->setExclusive(check);
}

bool ActionGroup::isExclusive() const
{
    return groupAction()->isExclusive();
}

void ActionGroup::setExclusiveOptional(bool check)
{
    groupAction()->setExclusionPolicy(
            check ? QActionGroup::ExclusionPolicy::ExclusiveOptional
                  : QActionGroup::ExclusionPolicy::Exclusive);
}

void ActionGroup::setVisible( bool check )
{
    Action::setVisible(check);
    groupAction()->setVisible(check);
}

QAction* ActionGroup::addAction(QAction* action)
{
    if (_actions.size() < groupAction()->actions().size()) {
        _actions = groupAction()->actions();
        for (auto a : _actions)
            track(a);
    }
    int index = _actions.size();
    action = groupAction()->addAction(action);
    action->setData(QVariant(index));
    // The same action can be added to multiple group, so setData() above is not
    // really reliable. Besides, it seems that Qt will auto remove duplicated
    // action from other groups if added to the same menu. So we can't really
    // rely on groupAction()->actions() to find the action either.
    track(action);
    _actions.append(action);
    return action;
}

QAction* ActionGroup::addAction(const QString& text)
{
    if (_actions.size() < groupAction()->actions().size()) {
        _actions = groupAction()->actions();
        for (auto a : _actions)
            track(a);
    }
    int index = _actions.size();
    QAction* action = groupAction()->addAction(text);
    action->setData(QVariant(index));
    track(action);
    _actions.append(action);
    return action;
}

QList<QAction*> ActionGroup::actions() const
{
    auto acts = groupAction()->actions();
    if (_actions.size() < acts.size()) {
        const_cast<ActionGroup*>(this)->_actions = acts;
        for (auto a : acts)
            track(a);
    }
    // The list is kept past what Qt removed from the group (see
    // addAction), so it must not keep what was DELETED: a group's
    // action may belong to another command, and that command can go
    // (a sandbox guest re-registering its commands after a reset,
    // docs/Sandbox.md 7.9 G2b, replaced 82 of them while Draft's
    // groups held their actions -- Command::testActive then read a
    // dead QAction).  A deleted QAction is gone from the QActionGroup,
    // so the ones Qt no longer lists are checked for life by the
    // registry of every action Qt ever gave this group.
    auto& live = const_cast<ActionGroup*>(this)->_actions;
    live.erase(std::remove_if(live.begin(), live.end(),
                              [&acts](QAction* a) {
                                  return !acts.contains(a) && !ActionGroup::isAlive(a);
                              }),
               live.end());
    return _actions;
}

namespace {
/// Every QAction an ActionGroup ever listed, dropped as it dies.
QSet<QAction*>& liveGroupActions()
{
    static QSet<QAction*> set;
    return set;
}
}  // namespace

void ActionGroup::track(QAction* action)
{
    auto& set = liveGroupActions();
    if (!action || set.contains(action))
        return;
    set.insert(action);
    QObject::connect(action, &QObject::destroyed, [action]() { liveGroupActions().remove(action); });
}

bool ActionGroup::isAlive(QAction* action)
{
    return liveGroupActions().contains(action);
}

int ActionGroup::checkedAction() const
{
    QAction* checked = groupAction()->checkedAction();
    return checked ? actions().indexOf(checked) : -1;
}

void ActionGroup::setCheckedAction(int index)
{
    auto actions = this->actions();
    if (index < 0 || index >= actions.size())
        return;
    QAction* a = actions[index];
    a->setChecked(true);
    this->setIcon(a->icon());

    if (!this->_isMode) {
        this->setToolTip(a->toolTip());
    }
    this->setProperty("defaultAction", QVariant(index));
}

/**
 * Activates the command.
 */
void ActionGroup::onActivated ()
{
    command()->invoke(this->property("defaultAction").toInt(), Command::TriggerAction);
}

void ActionGroup::onToggled(bool check)
{
    Q_UNUSED(check)
    onActivated();
}

/**
 * Activates the command.
 */
void ActionGroup::onActivated (QAction* act)
{
    int index = actions().indexOf(act);


    command()->invoke(index, Command::TriggerChildAction);
}


// --------------------------------------------------------------------

WorkbenchComboBox::WorkbenchComboBox(WorkbenchGroup* wb, QWidget* parent) : QComboBox(parent), group(wb)
{
    connect(this, qOverload<int>(&WorkbenchComboBox::activated), this,
        [this](int item) {
            int index = itemData(item).toInt();
            group->actions()[index]->trigger();
        });
    connect(getMainWindow(), &MainWindow::workbenchActivated,
            this, &WorkbenchComboBox::onWorkbenchActivated);
    connect(wb, &WorkbenchGroup::workbenchListUpdated,
            this, &WorkbenchComboBox::populate);
}

void WorkbenchComboBox::showPopup()
{
    int rows = count();
    if (rows > 0) {
        int height = view()->sizeHintForRow(0);
#if QT_VERSION < QT_VERSION_CHECK(5,14,0)
        int maxHeight = QApplication::desktop()->availableGeometry(getMainWindow()).height();
#else
        int maxHeight = getMainWindow()->screen()->availableGeometry().height();
#endif
        view()->setMinimumHeight(qMin(height * rows, maxHeight/2));
    }

    QComboBox::showPopup();
}

void WorkbenchComboBox::populate()
{
    clear();
    auto actions = group->actions();
    this->setIconSize(ToolBarManager::actionsIconSize(actions, this));

    if (ViewParams::getAutoSortWBList()) {
        std::sort(actions.begin(), actions.end(),
            [](const QAction *a, const QAction *b) {
                return a->text().compare(b->text(), Qt::CaseInsensitive) < 0;
            });
    }

    auto wb = WorkbenchManager::instance()->active();
    QString current;
    if (wb) {
        current = QString::fromUtf8(wb->name().c_str());
    }

    for (auto action : actions) {
        if (!action->isVisible()) {
            continue;
        }
        QIcon icon = action->icon();
        if (icon.isNull()) {
            this->addItem(action->text(), action->data());
        }
        else {
            this->addItem(icon, action->text(), action->data());
        }
        if (current == action->objectName()) {
            this->setCurrentIndex(this->count()-1);
        }
    }
}

void WorkbenchComboBox::onActivated(QAction* action)
{
    // set the according item to the action
    QVariant data = action->data();
    int index = this->findData(data);
    if (index >= 0) {
        setCurrentIndex(index);
    }
    else {
        populate();
    }
}

void WorkbenchComboBox::onWorkbenchActivated(const QString& name)
{
    // There might be more than only one instance of WorkbenchComboBox there.
    // However, all of them share the same QAction objects. Thus, if the user
    // has  selected one it also gets checked. Then Application::activateWorkbench
    // also invokes this slot method by calling the signal workbenchActivated in
    // MainWindow. If calling activateWorkbench() from within the Python console
    // the matching action must be set by calling this function.
    // To avoid to recursively (but only one recursion level) call Application::
    // activateWorkbench the method refreshWorkbenchList() shouldn't set the
    // checked item.
    //QVariant item = itemData(currentIndex());

    for (QAction* action : actions()) {
        if (action->objectName() == name) {
            if (!action->isChecked()) {
                action->trigger();
            }
            break;
        }
    }
}

// --------------------------------------------------------------------

class WorkbenchGroup::Private: public ParameterGrp::ObserverType
{
public:
    Private(WorkbenchGroup *master, const char *path):master(master)
    {
        hGeneral = App::GetApplication().GetUserParameter().GetGroup(
                "BaseApp/Preferences/General");
        handle = App::GetApplication().GetParameterGroupByPath(path);
        handle->Attach(this);
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, [master](){
            master->refreshWorkbenchList();
        });
    }

    virtual ~Private()
    {
        handle->Detach(this);
    }

    bool showText() {
        return handle->GetBool("TabBarShowText", false);
    }

    void setShowText(bool enable) {
        handle->SetBool("TabBarShowText", enable);
    }

    bool showTabBar() {
        return handle->GetBool("ShowTabBar", false);
    }

    void setShowTabBar(bool enable) {
        handle->SetBool("ShowTabBar", enable);
    }

    /*! How much room the tab bar may ask for along the direction the tabs run,
     * in pixels. 0 asks for as much as the tabs it holds actually need.
     */
    int tabBarMaxLength() {
        return handle->GetInt("TabBarMaxLength", 0);
    }

    void OnChange(Base::Subject<const char*> &, const char *reason)
    {
        if (!reason)
            return;
        if (boost::equals(reason, "Disabled")
                || boost::equals(reason, "Enabled"))
        {
            timer.start(100);
        }
    }

public:
    WorkbenchGroup *master;
    ParameterGrp::handle handle;
    ParameterGrp::handle hGeneral;
    QTimer timer;
};

// --------------------------------------------------------------------

void WorkbenchTabBar::setTabSize(int s)
{
    hasTabSize = true;
    _tabSize = s;
}

QSize WorkbenchTabBar::tabSizeHint(int index) const
{
    QSize size = QTabBar::tabSizeHint(index);
    if (!tabText(index).isEmpty())
        return size;
    switch (shape()) {
    case QTabBar::RoundedWest:
    case QTabBar::RoundedEast:
    case QTabBar::TriangularWest:
    case QTabBar::TriangularEast:
        if (_tabSize <= 0)
            return QSize(size.width(), std::max(iconSize().width(), size.width()+2));
        return QSize(size.width(), _tabSize);
    default:
        if (_tabSize <= 0)
            return QSize(std::max(iconSize().width(), size.height()+2), size.height());
        return QSize(_tabSize, size.height());
    }
}

void WorkbenchTabBar::changeEvent(QEvent *ev)
{
    QTabBar::changeEvent(ev);
    if (ev->type() == QEvent::StyleChange) {
        if (!hasTabSize)
            _tabSize = 0;
        hasTabSize = false;
    }
}

// --------------------------------------------------------------------

WorkbenchTabWidget::WorkbenchTabWidget(WorkbenchGroup* wb, QWidget* parent)
    : QTabWidget(parent), group(wb)
{
    this->setTabBar(new WorkbenchTabBar(this));
    connect(this, &QTabWidget::currentChanged, this, &WorkbenchTabWidget::onCurrentChanged);
    connect(this->tabBar(), &QTabBar::tabMoved, this, &WorkbenchTabWidget::onTabMoved);
    connect(getMainWindow(), &MainWindow::workbenchActivated,
            this, &WorkbenchTabWidget::onWorkbenchActivated);
    connect(wb, &WorkbenchGroup::workbenchListUpdated, this, &WorkbenchTabWidget::updateWorkbenches);
    updateWorkbenches();
    this->setMovable(true);
    this->tabBar()->setDrawBase(true);
    this->setDocumentMode(true);
    // this->setUsesScrollButtons(true);

    this->tabBar()->installEventFilter(this);

    connParam = App::GetApplication().GetUserParameter().signalParamChanged.connect(
        [this](ParameterGrp *Param, ParameterGrp::ParamType, const char *Name, const char *) {
            if (!Name)
                return;
            if (Param == this->group->_pimpl->handle) {
                if (boost::equals(Name, "TabBarShowText") || boost::equals(Name, "ShowTabBar")
                        || boost::equals(Name, "TabBarMaxLength"))
                    timer.start(100);
            } else if (Param == this->group->_pimpl->hGeneral) {
                if (boost::equals(Name, "ToolbarIconSize")
                        || boost::equals(Name, "WorkbenchTabIconSize"))
                    timer.start(100);
            }
        },
        fastsignals::advanced_tag {});

    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, [this]() {
        group->workbenchListUpdated();
        setupVisibility();
        // TabBarMaxLength changes nothing about the tabs themselves, only how
        // much room they may ask for, so nothing above invalidates the layout.
        updateGeometry();
        if (auto toolbar = getToolBar())
            toolbar->adjustSize();
    });

    timerCurrentChange.setSingleShot(true);
    connect(&timerCurrentChange, &QTimer::timeout, this, &WorkbenchTabWidget::onCurrentChanged);

    onChangeOrientation();
}

WorkbenchTabWidget::~WorkbenchTabWidget()
{
}

QSize WorkbenchTabWidget::sizeHint() const
{
    QSize size = QTabWidget::sizeHint();
    if (!usesScrollButtons())
        return size;

    // QTabWidget bounds the tab bar's contribution to its size hint at 200px
    // in each direction as soon as scroll buttons are in use, on the reasoning
    // that the pages behind the tabs are what should decide how big the widget
    // wants to be. There are no pages here -- the widget *is* the tab bar --
    // so that cap is all there is, and it asks for room for about five
    // workbenches however much the row has to give. Ask for the tabs we
    // actually hold instead, up to Workbenches/TabBarMaxLength.
    const QSize bar = tabBar()->sizeHint();
    const bool vertical = tabPosition() == West || tabPosition() == East;
    int wanted = vertical ? bar.height() : bar.width();
    if (int limit = group->_pimpl->tabBarMaxLength())
        wanted = std::min(wanted, limit);

    const int grown = wanted - std::min(vertical ? bar.height() : bar.width(), 200);
    if (grown > 0) {
        if (vertical)
            size.rheight() += grown;
        else
            size.rwidth() += grown;
    }
    return size;
}

QToolBar *WorkbenchTabWidget::getToolBar()
{
    for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto tb = qobject_cast<QToolBar*>(parent))
            return tb;
    }
    return nullptr;
}

void WorkbenchTabWidget::setupVisibility()
{
    if (!action)
        return;

    bool vis = this->group->_pimpl->showTabBar()
        || this->tabPosition() == QTabWidget::West
        || this->tabPosition() == QTabWidget::East;
    if (vis == this->action->isVisible())
        return;

    if (auto parent = qobject_cast<QToolBar*>(this->parentWidget())) {
        auto combo = parent->findChild<WorkbenchComboBox*>();
        if (combo && combo->getAction()) {
            for (auto w = QApplication::focusWidget();
                    w && !w->isWindow();
                    w = w->parentWidget())
            {
                if (w == parent) {
                    getMainWindow()->setFocus();
                    break;
                }
            }
            combo->getAction()->setVisible(!vis);
            this->action->setVisible(vis);
        }
    }
}

void WorkbenchTabWidget::onChangeOrientation()
{
    auto parent = qobject_cast<QToolBar*>(parentWidget());
    if (!parent)
        return;

    auto area = getMainWindow()->toolBarArea(parent);
    QTabWidget::TabPosition pos;
    if (parent->orientation() == Qt::Horizontal) {
        if(area == Qt::BottomToolBarArea)
            pos = QTabWidget::South;
        else
            pos = QTabWidget::North;
    } else if (area == Qt::RightToolBarArea)
        pos = QTabWidget::East;
    else
        pos = QTabWidget::West;
    if (pos != this->tabPosition()) {
        this->setTabPosition(pos);
        // inform layout change
        qApp->postEvent(parent, new QActionEvent(QEvent::ActionChanged, this->action));
    }

    setupVisibility();
}

void WorkbenchTabWidget::updateWorkbenches()
{
    auto tab = this->tabBar();

    QSize iconSize = ToolBarManager::actionsIconSize(this->group->actions(), this);
    if (this->iconSize() != iconSize) {
        this->setIconSize(iconSize);
    }

    auto wb = WorkbenchManager::instance()->active();
    QString current;
    if (wb)
        current = QString::fromUtf8(wb->name().c_str());

    QSignalBlocker block(this);
    bool showText = this->group->_pimpl->showText();
    if (showText) {
        if (this->styleSheet().size())
            this->setStyleSheet(QString());
    } else if (this->styleSheet().isEmpty()) {
        // These tabs carry an icon and no text. Left to the native metric the
        // label is laid out with PM_TabBarTabHSpace/2 of space before the icon
        // -- 12px against a 34px tab here -- which a tab shrunk by min-width
        // has no room for, so the icon comes out pushed right and clipped by
        // the tab border. Declaring the padding puts it in the middle of the
        // box instead.
        //
        // The horizontal value is the vertical one + 1 on purpose: the tab ends
        // up sized as icon + 2 + twice the vertical padding, so that is the
        // figure that splits the leftover evenly. 4/5 keeps the tab the same
        // size it had before, whatever the icon size is.
        this->setStyleSheet(
                QStringLiteral("::tab:top,"
                               "::tab:bottom {min-width: -1; padding: 4px 5px;}"
                               "::tab:left,"
                               "::tab:right {min-height: -1; padding: 5px 4px;}"));
    }
    int i=0;
    for (auto action : this->group->actions()) {
        if (!action->isVisible())
            continue;
        bool changed = true;
        QString wb = action->objectName();
        if (i >= this->count())
            this->addTab(new QWidget(), action->icon(), QString());
        else if (tab->tabData(i).toString() == action->objectName())
            changed = false;

        QString iconPath; 
        QPixmap px = Application::Instance->workbenchIcon(wb, &iconPath);

        tab->setTabIcon(i, action->icon());
        tab->setTabToolTip(i, action->toolTip());
        if (changed) {
            tab->setTabData(i, action->objectName());
        }
        if (showText) {
            if (tab->tabText(i) != action->text())
                tab->setTabText(i, action->text());
        } else if (tab->tabText(i).size())
            tab->setTabText(i, QString());

        if (current == action->objectName() && tab->currentIndex() != i) {
            setCurrentIndex(i);
            // if (!showText)
            //     tab->setTabText(i, action->text());
        }
        ++i;
    }
    while (this->count() > i)
        this->removeTab(this->count()-1);
}

void WorkbenchTabWidget::onCurrentChanged()
{
    if (QApplication::mouseButtons() & Qt::LeftButton) {
        timerCurrentChange.start(10);
        return;
    }

    auto tab = this->tabBar();
    int i = tab->currentIndex();
    QString name = tab->tabData(i).toString();
    // if (!this->group->_pimpl->showText()) {
    //     for (int j = 0, c = tab->count(); j<c; ++j) {
    //         if (j == i)
    //             tab->setTabText(i, Application::Instance->workbenchMenuText(name));
    //         else
    //             tab->setTabText(j, QString());
    //     }
    // }
    Application::Instance->activateWorkbench(name.toUtf8());
}

void WorkbenchTabWidget::onTabMoved(int, int)
{
    moved = true;
}

bool WorkbenchTabWidget::eventFilter(QObject *o, QEvent *ev)
{
    if (moved && o == this->tabBar()
              && ev->type() == QEvent::MouseButtonRelease
              && static_cast<QMouseEvent*>(ev)->button() == Qt::LeftButton)
    {
        moved = false;
        QStringList enabled;
        auto tab = this->tabBar();
        for (int i=0, c=tab->count(); i<c; ++i)
            enabled << tab->tabData(i).toString();
        Base::ConnectionBlocker blocker(connParam);
        QSignalBlocker blocker2(group);
        DlgSettingsWorkbenchesImp::saveWorkbenches(enabled);
    }
    return QTabWidget::eventFilter(o, ev);
}

void WorkbenchTabWidget::onWorkbenchActivated(const QString& name)
{
    QSignalBlocker block(this);
    auto tab = this->tabBar();
    // bool showText = this->group->_pimpl->showText();
    int current = tab->currentIndex();
    if (current >= 0 && tab->tabData(current).toString() != name) {
        // if (!showText)
        //     tab->setTabText(current, QString());
        for (int i=0, c=tab->count(); i<c; ++i) {
            if (tab->tabData(i).toString() == name) {
                setCurrentIndex(i);
                // if (!showText)
                //     tab->setTabText(i, Application::Instance->workbenchMenuText(name));
            }
        }
    }
}

// --------------------------------------------------------------------

/* TRANSLATOR Gui::WorkbenchGroup */
WorkbenchGroup::WorkbenchGroup (  Command* pcCmd, QObject * parent )
  : ActionGroup( pcCmd, parent )
{
    _pimpl.reset(new Private(this, "User parameter:BaseApp/Preferences/Workbenches"));

    // Start a list with 50 elements but extend it when requested
    for (int i=0; i<50; i++) {
        QAction* action = groupAction()->addAction(QStringLiteral(""));
        action->setVisible(false);
        action->setCheckable(true);
        action->setData(QVariant(i)); // set the index
    }

    Application::Instance->signalActivateWorkbench.connect(std::bind(&WorkbenchGroup::slotActivateWorkbench, this, sp::_1));
    Application::Instance->signalAddWorkbench.connect(std::bind(&WorkbenchGroup::slotAddWorkbench, this, sp::_1));
    Application::Instance->signalRemoveWorkbench.connect(std::bind(&WorkbenchGroup::slotRemoveWorkbench, this, sp::_1));
}

WorkbenchGroup::~WorkbenchGroup()
{
    delete _menu;
}

void WorkbenchGroup::onContextMenuRequested(const QPoint &)
{
    bool combo = qobject_cast<QComboBox*>(sender()) != nullptr;
    QMenu menu;
    bool showText = _pimpl->showText();
    QAction *actText = nullptr;
    if (!combo)
        actText = menu.addAction(showText ? tr("Hide text") : tr("Show text"));
    auto actShow = menu.addAction(combo ? tr("Expand") : tr("Collapse"));
    menu.addSeparator();

    QMenu subMenu(tr("Disabled workbenches"));
    std::vector<QAction*> actions;
    QStringList disabled = DlgSettingsWorkbenchesImp::getDisabledWorkbenches();
    for (auto &wb : disabled) {
        QString name = Application::Instance->workbenchMenuText(wb);
        QPixmap px = Application::Instance->workbenchIcon(wb);
        QString tip = Application::Instance->workbenchToolTip(wb);
        auto action = Action::addCheckBox(&subMenu, name, tip, px, false);
        action->setData(wb);
        actions.push_back(action);
    }
    if (!subMenu.isEmpty()) {
        menu.addMenu(&subMenu);
        menu.addSeparator();
    }
    QStringList enabled = DlgSettingsWorkbenchesImp::getEnabledWorkbenches();
    for (auto &wb : enabled) {
        QString name = Application::Instance->workbenchMenuText(wb);
        QPixmap px = Application::Instance->workbenchIcon(wb);
        QString tip = Application::Instance->workbenchToolTip(wb);
        auto action = Action::addCheckBox(&menu, name, tip, px, true);
        action->setData(wb);
        actions.push_back(action);
    }

    auto act = menu.exec(QCursor::pos());
    if (act && act == actText)
        _pimpl->setShowText(!showText);
    else if (act && act == actShow)
        _pimpl->setShowTabBar(combo);

    QStringList enabled2, disabled2;
    for (auto action : actions) {
        if (action->isChecked())
            enabled2 << action->data().toString();
        else
            disabled2 << action->data().toString();
    }
    if (enabled != enabled2)
        DlgSettingsWorkbenchesImp::saveWorkbenches(enabled2, disabled2);
}

void WorkbenchGroup::addTo(QWidget *widget)
{
    refreshWorkbenchList();
    auto setupBox = [&](WorkbenchComboBox* box) {
        box->setToolTip(_tooltip);
        box->setStatusTip(action()->statusTip());
        box->setWhatsThis(action()->whatsThis());
        connect(groupAction(), &QActionGroup::triggered, box, qOverload<QAction*>(&WorkbenchComboBox::onActivated));
    };

    if (widget->inherits("QToolBar")) {
        QToolBar* bar = qobject_cast<QToolBar*>(widget);
        auto box = new WorkbenchComboBox(this, widget);
        setupBox(box);
        box->populate();
        auto actBox = bar->addWidget(box);
        box->setAction(actBox);
        box->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(box, &WorkbenchComboBox::customContextMenuRequested,
                this, &WorkbenchGroup::onContextMenuRequested);

        auto tabbar = new WorkbenchTabWidget(this, widget);
        auto actTab = bar->addWidget(tabbar);
        tabbar->setAction(actTab);
        tabbar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tabbar, &WorkbenchTabWidget::customContextMenuRequested,
                this, &WorkbenchGroup::onContextMenuRequested);
        connect(bar, &QToolBar::orientationChanged,
                tabbar, &WorkbenchTabWidget::onChangeOrientation);
        connect(bar, &QToolBar::topLevelChanged,
                tabbar, &WorkbenchTabWidget::onChangeOrientation);


        connect(bar, &QToolBar::iconSizeChanged, this, [=] {
            auto actions = this->actions();
            box->setIconSize(ToolBarManager::actionsIconSize(actions, box));
            tabbar->setIconSize(ToolBarManager::actionsIconSize(actions, tabbar));
        });

        if (_pimpl->showTabBar())
            actBox->setVisible(false);
        else
            actTab->setVisible(false);
    }
    else if (widget->inherits("QMenu")) {
        QMenu* menu = qobject_cast<QMenu*>(widget);
        if (!_menu) {
            _menu = new QMenu(action()->text());
            onShowMenu();
            connect(_menu, &QMenu::aboutToShow, this, &WorkbenchGroup::onShowMenu);
        }
        menu->addMenu(_menu);
    }
}

void WorkbenchGroup::onShowMenu()
{
    _menu->clear();
    auto actions = groupAction()->actions();
    if (ViewParams::getAutoSortWBList()) {
        std::sort(actions.begin(), actions.end(),
            [](const QAction *a, const QAction *b) {
                return a->text().compare(b->text(), Qt::CaseInsensitive) < 0;
            });
    }
    _menu->addActions(actions);
}

void WorkbenchGroup::setWorkbenchData(int index, const QString& wb)
{
    QList<QAction*> workbenches = groupAction()->actions();
    QString name = Application::Instance->workbenchMenuText(wb);
    QString iconPath;
    QPixmap px = Application::Instance->workbenchIcon(wb, &iconPath);
    QString tip = Application::Instance->workbenchToolTip(wb);
    workbenches[index]->setObjectName(wb);
    workbenches[index]->setIcon(px);
    workbenches[index]->setText(name);
    workbenches[index]->setStatusTip(tr("Select the '%1' workbench").arg(name));
    workbenches[index]->setVisible(true);
    QString shortcut;
    if (index < 9) {
        shortcut = QString::fromUtf8("W,%1").arg(index+1);
        workbenches[index]->setShortcut(QKeySequence(shortcut));
    }
    workbenches[index]->setToolTip(Action::createToolTip(
                tip, name, workbenches[index]->font(), shortcut, nullptr, iconPath));
}

void WorkbenchGroup::refreshWorkbenchList()
{
    QStringList enabled_wbs_list = DlgSettingsWorkbenchesImp::getEnabledWorkbenches();

    // Resize the action group.
    QList<QAction*> workbenches = groupAction()->actions();
    int numActions = workbenches.size();
    int extend = enabled_wbs_list.size() - numActions;
    if (extend > 0) {
        for (int i=0; i<extend; i++) {
            QAction* action = groupAction()->addAction(QStringLiteral(""));
            action->setCheckable(true);
            action->setData(QVariant(numActions++)); // set the index
        }
    } else {
        for (;extend < 0; ++extend) {
            auto action = workbenches[numActions + extend];
            action->setVisible(false);
            action->setObjectName(QString());
        }
    }

    // Show all enabled wb
    int index = 0;
    for (const auto& it : enabled_wbs_list) {
        setWorkbenchData(index++, it);
    }

    workbenchListUpdated();
}

void WorkbenchGroup::slotActivateWorkbench(const char* /*name*/)
{
}

void WorkbenchGroup::slotAddWorkbench(const char* name)
{
    QString wb = QString::fromUtf8(name);
    int index = 0;
    for (QAction *action : groupAction()->actions()) {
        if (!action->isVisible()) {
            setWorkbenchData(index, wb);
            return;
        }
        ++index;
    }

    auto action = groupAction()->addAction(QStringLiteral(""));
    action->setCheckable(true);
    action->setData(QVariant(index)); // set the index
    setWorkbenchData(index, wb);
}

void WorkbenchGroup::slotRemoveWorkbench(const char* name)
{
    QString workbench = QString::fromUtf8(name);
    QList<QAction*> workbenches = groupAction()->actions();
    for (auto it : workbenches) {
        if (it->objectName() == workbench) {
            it->setObjectName(QString());
            it->setIcon(QIcon());
            it->setText(QString());
            it->setToolTip(QString());
            it->setStatusTip(QString());
            it->setVisible(false); // do this at last
            break;
        }
    }
}

// --------------------------------------------------------------------

class RecentFilesAction::Private: public ParameterGrp::ObserverType
{
public:
    Private(const Private&) = delete;
    Private(Private&&) = delete;
    void operator= (const Private&) = delete;
    void operator= (Private&&) = delete;

    Private(RecentFilesAction *master, const char *path):master(master)
    {
        handle = App::GetApplication().GetParameterGroupByPath(path);
        handle->Attach(this);
    }

    ~Private() override
    {
        handle->Detach(this);
    }

    void OnChange(Base::Subject<const char*> & sub, const char *reason) override
    {
        Q_UNUSED(sub)
        if (!updating && reason && strcmp(reason, "RecentFiles")==0) {
            Base::StateLocker guard(updating);
            master->restore();
        }
    }

    void trySaveUserParameter()
    {
        // update the XML structure and save the user parameter to disk (#0001989)
        bool saveParameter = App::GetApplication().GetParameterGroupByPath
            ("User parameter:BaseApp/Preferences/General")->GetBool("SaveUserParameter", true);
        if (saveParameter) {
            saveUserParameter();
        }
    }

    void saveUserParameter()
    {
        ParameterManager* parmgr = App::GetApplication().GetParameterSet("User parameter");
        parmgr->SaveDocument(App::Application::Config()["UserParameter"].c_str());
    }

public:
    RecentFilesAction *master;
    ParameterGrp::handle handle;
    bool updating = false;
};

// --------------------------------------------------------------------

/* TRANSLATOR Gui::RecentFilesAction */

RecentFilesAction::RecentFilesAction ( Command* pcCmd, QObject * parent )
  : ActionGroup( pcCmd, parent )
  , visibleItems(4)
  , maximumItems(20)
{
    _pimpl = std::make_unique<Private>(this, "User parameter:BaseApp/Preferences/RecentFiles");
    restore();
}

RecentFilesAction::~RecentFilesAction()
{
    _pimpl.reset(nullptr);
}

/** Adds the new item to the recent files. */
void RecentFilesAction::appendFile(const QString& filename)
{
    // empty file name signals the intention to rebuild recent file
    // list without changing the list content.
    if (filename.isEmpty()) {
        save();
        return;
    }

    // restore the list of recent files
    QStringList files = this->files();

    // if already inside remove and prepend it
    files.removeAll(filename);
    files.prepend(filename);
    setFiles(files);
    save();

    _pimpl->trySaveUserParameter();
}

/**
 * Set the list of recent files. For each item an action object is
 * created and added to this action group.
 */
void RecentFilesAction::setFiles(const QStringList& files)
{
    QList<QAction*> recentFiles = groupAction()->actions();

    int numRecentFiles = std::min<int>(recentFiles.count(), files.count());
    for (int index = 0; index < numRecentFiles; index++) {
        QFileInfo fi(files[index]);
        recentFiles[index]->setText(QStringLiteral("%1 %2").arg(index+1).arg(fi.fileName()));
        recentFiles[index]->setStatusTip(tr("Open file %1").arg(files[index]));
        recentFiles[index]->setToolTip(files[index]); // set the full name that we need later for saving
        recentFiles[index]->setData(QVariant(index));
        recentFiles[index]->setVisible(true);
    }

    // if less file names than actions
    numRecentFiles = std::min<int>(numRecentFiles, this->visibleItems);
    for (int index = numRecentFiles; index < recentFiles.count(); index++) {
        recentFiles[index]->setVisible(false);
        recentFiles[index]->setText(QString());
        recentFiles[index]->setToolTip(QString());
    }
}

/**
 * Returns the list of defined recent files.
 */
QStringList RecentFilesAction::files() const
{
    QStringList files;
    QList<QAction*> recentFiles = groupAction()->actions();
    for (int index = 0; index < recentFiles.count(); index++) {
        QString file = recentFiles[index]->toolTip();
        if (file.isEmpty()) {
            break;
        }
        files.append(file);
    }

    return files;
}

void RecentFilesAction::activateFile(int id)
{
    // restore the list of recent files
    QStringList files = this->files();
    if (id < 0 || id >= files.count()) {
        return; // no valid item
    }

    QString filename = files[id];
    QFileInfo fi(filename);
    if (!fi.exists()) {
        QMessageBox::critical(getMainWindow(), tr("File not found"), tr("The file '%1' cannot be opened.").arg(filename));
        files.removeAll(filename);
        setFiles(files);
        save();
    }
    else {
        // invokes appendFile()
        SelectModule::Dict dict = SelectModule::importHandler(filename);
        for (SelectModule::Dict::iterator it = dict.begin(); it != dict.end(); ++it) {
            Application::Instance->open(it.key().toUtf8(), it.value().toUtf8());
            break;
        }
    }
}

void RecentFilesAction::resizeList(int size)
{
    this->visibleItems = size;
    int diff = this->visibleItems - this->maximumItems;
    // create new items if needed
    for (int i=0; i<diff; i++) {
        groupAction()->addAction(QStringLiteral(""))->setVisible(false);
    }
    setFiles(files());
}

/** Loads all recent files from the preferences. */
void RecentFilesAction::restore()
{
    ParameterGrp::handle hGrp = _pimpl->handle;
    // we want at least 20 items but we do only show the number of files
    // that is defined in user parameters
    this->visibleItems = hGrp->GetInt("RecentFiles", this->visibleItems);

    int count = std::max<int>(this->maximumItems, this->visibleItems);
    for (int i=groupAction()->actions().size(); i<count; i++) {
        groupAction()->addAction(QStringLiteral(""))->setVisible(false);
    }
    std::vector<std::string> MRU = hGrp->GetASCIIs("MRU");
    QStringList files;
    for(const auto& it : MRU) {
        auto filePath = QString::fromUtf8(it.c_str());
        if (QFileInfo::exists(filePath)) {
            files.append(filePath);
        }
    }
    setFiles(files);
}

/** Saves all recent files to the preferences. */
void RecentFilesAction::save()
{
    ParameterGrp::handle hGrp = _pimpl->handle;
    int count = hGrp->GetInt("RecentFiles", this->visibleItems); // save number of files
    hGrp->Clear();

    // count all set items
    QList<QAction*> recentFiles = groupAction()->actions();
    int num = std::min<int>(count, recentFiles.count());
    for (int index = 0; index < num; index++) {
        QString key = QStringLiteral("MRU%1").arg(index);
        QString value = recentFiles[index]->toolTip();
        if (value.isEmpty()) {
            break;
        }
        hGrp->SetASCII(key.toUtf8(), value.toUtf8());
    }

    Base::StateLocker guard(_pimpl->updating);
    hGrp->SetInt("RecentFiles", count); // restore
}

// --------------------------------------------------------------------

/* TRANSLATOR Gui::RecentMacrosAction */

RecentMacrosAction::RecentMacrosAction ( Command* pcCmd, QObject * parent )
  : ActionGroup( pcCmd, parent )
  , visibleItems(4)
  , maximumItems(20)
{
    restore();
}

/** Adds the new item to the recent files. */
void RecentMacrosAction::appendFile(const QString& filename)
{
    // restore the list of recent files
    QStringList files = this->files();

    // if already inside remove and prepend it
    files.removeAll(filename);
    files.prepend(filename);
    setFiles(files);
    save();

    // update the XML structure and save the user parameter to disk (#0001989)
    bool saveParameter = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/General")->GetBool("SaveUserParameter", true);
    if (saveParameter) {
        ParameterManager* parmgr = App::GetApplication().GetParameterSet("User parameter");
        parmgr->SaveDocument(App::Application::Config()["UserParameter"].c_str());
    }
}

/**
 * Set the list of recent macro files. For each item an action object is
 * created and added to this action group.
 */
void RecentMacrosAction::setFiles(const QStringList& files)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                                ->GetGroup("Preferences")->GetGroup("RecentMacros");
    this->shortcut_modifiers = hGrp->GetASCII("ShortcutModifiers","Ctrl+Shift+");
    this->shortcut_count = std::min<int>(hGrp->GetInt("ShortcutCount",3),9);//max = 9, e.g. Ctrl+Shift+9
    this->visibleItems = hGrp->GetInt("RecentMacros",12);
    QList<QAction*> recentFiles = groupAction()->actions();

    int numRecentFiles = std::min<int>(recentFiles.count(), files.count());
    QStringList existingCommands;
    auto accel_col = QString::fromStdString(shortcut_modifiers);
    for (int index = 0; index < numRecentFiles; index++) {
        QFileInfo fi(files[index]);
        QString accel = tr(QString::fromUtf8(shortcut_modifiers.c_str())\
                           .append(QString::number(index+1,10)).toStdString().c_str());
        recentFiles[index]->setText(QStringLiteral("%1 %2").arg(index+1).arg(fi.completeBaseName()));
        recentFiles[index]->setStatusTip(tr("Run macro %1 (Shift+click to edit) shortcut: %2").arg(files[index]).arg(accel));
        recentFiles[index]->setToolTip(files[index]); // set the full name that we need later for saving
        recentFiles[index]->setData(QVariant(index));
        if (index < shortcut_count){
            auto accel_tmp = QString::fromStdString(shortcut_modifiers);
            accel_tmp.append(QString::number(index+1,10)).toStdString();
            auto check = Application::Instance->commandManager().checkAcceleratorForConflicts(qPrintable(accel_tmp));
            if (check) {
                recentFiles[index]->setShortcut(QKeySequence());
                accel_col.append(accel_tmp);
                existingCommands.append(QString::fromUtf8(check->getName()));
            }
            else {
                accel = accel_tmp;
                recentFiles[index]->setShortcut(accel);
            }
        }
        recentFiles[index]->setVisible(true);
    }

    // if less file names than actions
    numRecentFiles = std::min<int>(numRecentFiles, this->visibleItems);
    for (int index = numRecentFiles; index < recentFiles.count(); index++) {
        recentFiles[index]->setVisible(false);
        recentFiles[index]->setText(QString());
        recentFiles[index]->setToolTip(QString());
    }
    // Raise a single warning no matter how many conflicts
    if (!existingCommands.isEmpty()) {
        auto msgMain = QStringLiteral("Recent macros : keyboard shortcut(s)");
        for (int index = 0; index < accel_col.size(); index++) {
            msgMain += QStringLiteral(" %1").arg(accel_col[index]);
        }
        msgMain += QStringLiteral(" disabled because of conflicts with");
        for (int index = 0; index < existingCommands.count(); index++) {
            msgMain += QStringLiteral(" %1").arg(existingCommands[index]);
        }
        msgMain += QStringLiteral(" respectively.\nHint: In Preferences -> Python -> Macro ->"
                             " Recent Macros menu -> Keyboard Modifiers this should be Ctrl+Shift+"
                             " by default, if this is now blank then you should revert it back to"
                             " Ctrl+Shift+ by pressing both keys at the same time.");
        Base::Console().Warning("%s\n", qPrintable(msgMain));
    }
}

/**
 * Returns the list of defined recent files.
 */
QStringList RecentMacrosAction::files() const
{
    QStringList files;
    QList<QAction*> recentFiles = groupAction()->actions();
    for (int index = 0; index < recentFiles.count(); index++) {
        QString file = recentFiles[index]->toolTip();
        if (file.isEmpty()) {
            break;
        }
        files.append(file);
    }

    return files;
}

void RecentMacrosAction::activateFile(int id)
{
    // restore the list of recent files
    QStringList files = this->files();
    if (id < 0 || id >= files.count()) {
        return; // no valid item
    }

    QString filename = files[id];
    QFileInfo fi(filename);
    if (!fi.exists() || !fi.isFile()) {
        QMessageBox::critical(getMainWindow(), tr("File not found"), tr("The file '%1' cannot be opened.").arg(filename));
        files.removeAll(filename);
        setFiles(files);
    }
    else {
        if (QApplication::keyboardModifiers() == Qt::ShiftModifier){ //open for editing on Shift+click
            auto editor = new PythonEditor();
            editor->setWindowIcon(Gui::BitmapFactory().iconFromTheme("applications-python"));
            auto edit = new PythonEditorView(editor, getMainWindow());
            edit->setDisplayName(PythonEditorView::FileName);
            edit->open(filename);
            edit->resize(400, 300);
            getMainWindow()->addWindow(edit);
            getMainWindow()->appendRecentMacro(filename);
            edit->setWindowTitle(fi.fileName());
        } else { //execute macro on normal (non-shifted) click
            try {
                getMainWindow()->appendRecentMacro(fi.filePath());
                Application::Instance->macroManager()->run(Gui::MacroManager::File, fi.filePath().toUtf8());
                // after macro run recalculate the document
                if (Application::Instance->activeDocument()) {
                    Application::Instance->activeDocument()->getDocument()->recompute();
                }
            }
            catch (const Base::SystemExitException&) {
                // handle SystemExit exceptions
                Base::PyGILStateLocker locker;
                Base::PyException exc;
                exc.ReportException();
            }
        }
    }
}

void RecentMacrosAction::resizeList(int size)
{
    this->visibleItems = size;
    int diff = this->visibleItems - this->maximumItems;
    // create new items if needed
    for (int i=0; i<diff; i++) {
        groupAction()->addAction(QStringLiteral(""))->setVisible(false);
    }
    setFiles(files());
}

/** Loads all recent files from the preferences. */
void RecentMacrosAction::restore()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                                ->GetGroup("Preferences")->GetGroup("RecentMacros");

    for (int i=groupAction()->actions().size(); i<this->maximumItems; i++) {
        groupAction()->addAction(QStringLiteral(""))->setVisible(false);
    }
    resizeList(hGrp->GetInt("RecentMacros"));

    int count = std::max<int>(this->maximumItems, this->visibleItems);
    for (int i=groupAction()->actions().size(); i<count; i++) {
        groupAction()->addAction(QStringLiteral(""))->setVisible(false);
    }
    std::vector<std::string> MRU = hGrp->GetASCIIs("MRU");
    QStringList files;
    for (auto& filename : MRU) {
        files.append(QString::fromUtf8(filename.c_str()));
    }
    setFiles(files);
}

/** Saves all recent files to the preferences. */
void RecentMacrosAction::save()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")
                                ->GetGroup("Preferences")->GetGroup("RecentMacros");
    int count = hGrp->GetInt("RecentMacros", this->visibleItems); // save number of files
    hGrp->Clear();

    // count all set items
    QList<QAction*> recentFiles = groupAction()->actions();
    int num = std::min<int>(count, recentFiles.count());
    for (int index = 0; index < num; index++) {
        QString key = QStringLiteral("MRU%1").arg(index);
        QString value = recentFiles[index]->toolTip();
        if (value.isEmpty()) {
            break;
        }
        hGrp->SetASCII(key.toUtf8(), value.toUtf8());
    }

    hGrp->SetInt("RecentMacros", count); // restore
    hGrp->SetInt("ShortcutCount", this->shortcut_count);
    hGrp->SetASCII("ShortcutModifiers",this->shortcut_modifiers.c_str());
}

// --------------------------------------------------------------------

UndoAction::UndoAction (Command* pcCmd,QObject * parent)
  : Action(pcCmd, parent)
{
    _toolAction = new QAction(this);
    _toolAction->setMenu(new UndoDialog());
    connect(_toolAction, &QAction::triggered, this, &UndoAction::onActivated);
}

UndoAction::~UndoAction()
{
    QMenu* menu = _toolAction->menu();
    delete menu;
    delete _toolAction;
}

void UndoAction::addTo (QWidget * widget)
{
    if (widget->inherits("QToolBar")) {
        actionChanged();
        connect(action(), &QAction::changed, this, &UndoAction::actionChanged);
        setupMenuToolButton(widget);
        widget->addAction(_toolAction);
    }
    else {
        widget->addAction(action());
    }
}

void UndoAction::actionChanged()
{
    // Do NOT set the shortcut again for _toolAction since this is already
    // reserved for _action. Otherwise we get an ambiguity of it with the
    // result that it doesn't work anymore.
    _toolAction->setText(action()->text());
    _toolAction->setToolTip(action()->toolTip());
    _toolAction->setStatusTip(action()->statusTip());
    _toolAction->setWhatsThis(action()->whatsThis());
    _toolAction->setIcon(action()->icon());
}

void UndoAction::setEnabled(bool check)
{
    Action::setEnabled(check);
    _toolAction->setEnabled(check);
}

void UndoAction::setVisible(bool check)
{
    Action::setVisible(check);
    _toolAction->setVisible(check);
}

// --------------------------------------------------------------------

RedoAction::RedoAction ( Command* pcCmd,QObject * parent )
  : Action(pcCmd, parent)
{
    _toolAction = new QAction(this);
    _toolAction->setMenu(new RedoDialog());
    connect(_toolAction, &QAction::triggered, this, &RedoAction::onActivated);
}

RedoAction::~RedoAction()
{
    QMenu* menu = _toolAction->menu();
    delete menu;
    delete _toolAction;
}

void RedoAction::addTo ( QWidget * widget )
{
    if (widget->inherits("QToolBar")) {
        actionChanged();
        connect(action(), &QAction::changed, this, &RedoAction::actionChanged);
        setupMenuToolButton(widget);
        widget->addAction(_toolAction);
    }
    else {
        widget->addAction(action());
    }
}

void RedoAction::actionChanged()
{
    // Do NOT set the shortcut again for _toolAction since this is already
    // reserved for _action. Otherwise we get an ambiguity of it with the
    // result that it doesn't work anymore.
    _toolAction->setText(action()->text());
    _toolAction->setToolTip(action()->toolTip());
    _toolAction->setStatusTip(action()->statusTip());
    _toolAction->setWhatsThis(action()->whatsThis());
    _toolAction->setIcon(action()->icon());
}

void RedoAction::setEnabled  ( bool check )
{
    Action::setEnabled(check);
    _toolAction->setEnabled(check);
}

void RedoAction::setVisible ( bool check )
{
    Action::setVisible(check);
    _toolAction->setVisible(check);
}

// --------------------------------------------------------------------

DockWidgetAction::DockWidgetAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent)
  , _menu(nullptr)
{
}

DockWidgetAction::~DockWidgetAction()
{
    delete _menu;
}

void DockWidgetAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _menu = new QMenu();
        action()->setMenu(_menu);
        getMainWindow()->setDockWindowMenu(_menu);
        _menu->aboutToShow();
    }

    if (qobject_cast<QToolBar*>(widget)) {
        setupMenuToolButton(widget);
    }
    widget->addAction(action());
}

// --------------------------------------------------------------------

ToolBarAction::ToolBarAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent)
  , _menu(nullptr)
{
}

ToolBarAction::~ToolBarAction()
{
    delete _menu;
}

void ToolBarAction::addTo ( QWidget * widget )
{
    if (!_menu) {
      _menu = new QMenu();
      action()->setMenu(_menu);
      getMainWindow()->setToolBarMenu(_menu);
    }

    if (qobject_cast<QToolBar*>(widget)) {
        setupMenuToolButton(widget);
    }
    widget->addAction(action());
}

// --------------------------------------------------------------------

WindowAction::WindowAction ( Command* pcCmd, QObject * parent )
  : ActionGroup(pcCmd, parent)
  , _menu(nullptr)
{
}

void WindowAction::addTo ( QWidget * widget )
{
    auto menu = qobject_cast<QMenu*>(widget);
    if (!menu) {
        if (!_menu) {
            _menu = new QMenu();
            action()->setMenu(_menu);
            _menu->addActions(groupAction()->actions());
            getMainWindow()->setWindowsMenu(_menu);
        }

        if (qobject_cast<QToolBar*>(widget)) {
            setupMenuToolButton(widget);
        }
        widget->addAction(action());
    }
    else {
        menu->addActions(groupAction()->actions());
        getMainWindow()->setWindowsMenu(menu);
    }
}

// --------------------------------------------------------------------

ViewCameraBindingAction::ViewCameraBindingAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent), _menu(0)
{
}

ViewCameraBindingAction::~ViewCameraBindingAction()
{
}

void ViewCameraBindingAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _menu = new QMenu();
        setupMenuStyle(_menu);
        action()->setMenu(_menu);
        connect(_menu, &QMenu::aboutToShow, this, &ViewCameraBindingAction::onShowMenu);
        connect(_menu, &QMenu::triggered, this, &ViewCameraBindingAction::onTriggered);
    }
    if (qobject_cast<QToolBar*>(widget))
        setupMenuToolButton(widget);
    widget->addAction(action());
}

void ViewCameraBindingAction::onShowMenu()
{
    _menu->clear();
    setupMenuStyle(_menu);

    auto activeView = Base::freecad_dynamic_cast<View3DInventor>(
            Application::Instance->activeView());
    if(!activeView)
        return;

    auto boundViews = activeView->boundViews();
    if(boundViews.size()) {
        if(boundViews.size() == 1) {
            auto action = _menu->addAction(tr("Sync camera"));
            action->setData(1);
        }
        auto action = _menu->addAction(tr("Unbind"));
        action->setData(2);
        _menu->addSeparator();
    }
    for(auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if(!gdoc)
            continue;
        auto views = gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId());
        for(auto it=views.begin();it!=views.end();) {
            auto view = static_cast<View3DInventor*>(*it);
            if(view == activeView ||
                    (!boundViews.count(view) && view->boundViews(true).count(activeView)))
                it = views.erase(it);
            else
                ++it;
        }
        if(views.empty())
            continue;
        if(views.size() == 1) {
            auto view = static_cast<View3DInventor*>(views.front());
            auto action = _menu->addAction(view->windowTitle());
            action->setCheckable(true);
            if(boundViews.count(view))
                action->setChecked(true);
            continue;
        }
        auto menu = _menu->addMenu(QString::fromUtf8(doc->Label.getValue()));
        for(auto view : views) {
            auto action = menu->addAction(view->windowTitle());
            action->setCheckable(true);
            if(boundViews.count(static_cast<View3DInventor*>(view)))
                action->setChecked(true);
        }
    }
}

void ViewCameraBindingAction::onTriggered(QAction *action)
{
    auto activeView = Base::freecad_dynamic_cast<View3DInventor>(
            Application::Instance->activeView());
    if(!activeView)
        return;

    switch(action->data().toInt()) {
    case 1: {
        auto views = activeView->boundViews();
        if(views.size())
            activeView->syncCamera(*views.begin());
        break;
    }
    case 2:
        activeView->unbindView();
        break;
    default:
        if (action->isChecked())
            activeView->bindView(action->text(), true);
        else
            activeView->unbindView(action->text());
        break;
    }
}

// --------------------------------------------------------------------

SelUpAction::SelUpAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent), _menu(nullptr), _emptyAction(nullptr)
{
}

SelUpAction::~SelUpAction()
{
    delete _menu;
}

void SelUpAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _menu = new SelUpMenu(nullptr);
        action()->setMenu(_menu);
        connect(_menu, &QMenu::aboutToShow, this, &SelUpAction::onShowMenu);
    }
    if (qobject_cast<QToolBar*>(widget))
        setupMenuToolButton(widget);
    widget->addAction(action());
}

void SelUpAction::onShowMenu()
{
    _menu->clear();
    setupMenuStyle(_menu);
    TreeWidget::populateSelUpMenu(_menu);
}

void SelUpAction::popup(const QPoint &pt)
{
    SelectionContext selctx;
    if(_menu->actions().isEmpty()) {
        if (!_emptyAction) {
            _emptyAction = new QAction(tr("<None>"), this);
            _emptyAction->setDisabled(true);
        }
        _menu->addAction(_emptyAction);
    }
    TreeWidget::execSelUpMenu(qobject_cast<SelUpMenu*>(_menu), pt);
}

// --------------------------------------------------------------------

SelStackAction::SelStackAction ( Command* pcCmd, Type type, QObject * parent )
  : Action(pcCmd, parent), _menu(nullptr), _type(type)
{
}

SelStackAction::~SelStackAction()
{
    delete _menu;
}

void SelStackAction::select(int idx, const std::vector<int> &subIndices)
{
    if (idx < 0) {
        Selection().selStackGoForward(-idx, subIndices);
        return;
    }
    if (!Selection().hasSelection())
        ++idx;
    Selection().selStackGoBack(idx, subIndices);
}

void SelStackAction::populate()
{
    _menu->clear();
    int idx, count, step;
    if (_type == Type::Backward) {
        count = Selection().selStackBackSize();
        step = 1;
        idx = 0;
    } else {
        count = Selection().selStackForwardSize();
        step = -1;
        idx = -1;
    }
    auto doc = App::GetApplication().getActiveDocument();
    const char *activeDocName = doc ? doc->getName() : nullptr;
    for (int i=0; i<count; ++i, idx+=step) {
        auto sels = Selection().selStackGetT(nullptr, ResolveMode::NoResolve, idx);
        if (sels.empty())
            continue;

        QAction *act;
        if (sels.size() == 1) {
            sels[0].setSubName(sels[0].getSubNameNoElement() + sels[0].getOldElementName());
            act = _menu->addAction(QString::fromUtf8(
                        sels[0].getSubObjectFullName(activeDocName).c_str()), [=](){select(idx);});
        } else {
            auto sel = sels[0];
            sel.setSubName(sel.getSubNameNoElement());
            QMenu *menu = _menu->addMenu(QStringLiteral("%1...").arg(
                    QString::fromUtf8(sels[0].getSubObjectFullName(activeDocName).c_str())));
            act = menu->menuAction();
            QAction *action = menu->addAction(tr("Select all"), [=](){select(idx);});
            menu->addSeparator();
            std::map<App::SubObjectT, std::map<std::string, int>> subs;
            int sidx = -1;
            for (auto sel  : sels) {
                ++sidx;
                auto element = sel.getOldElementName();
                sel.setSubName(sel.getSubNameNoElement());
                subs[sel][element] = sidx;
            }
            auto elementName = [](const std::string &name) {
                if (name.empty())
                    return tr("Whole object");
                else
                    return QString::fromUtf8(name.c_str());
            };
            if (subs.size() == 1) {
                for (const auto &v : subs.begin()->second) {
                    int sidx = v.second;
                    action = menu->addAction(elementName(v.first.c_str()), [=](){select(idx,{sidx});});
                }
            } else {
                for (const auto &v : subs) {
                    if (v.second.size() == 1) {
                        auto &info = *v.second.begin();
                        int sidx = info.second;
                        App::SubObjectT objT = v.first;
                        objT.setSubName(objT.getSubName() + info.first);
                        action = menu->addAction(QString::fromUtf8(
                                    objT.getSubObjectFullName(activeDocName).c_str()),
                                [=](){select(idx,{sidx});});
                    } else {
                        auto subMenu = menu->addMenu(QString::fromUtf8(
                                    v.first.getSubObjectFullName(activeDocName).c_str()));
                        action = subMenu->menuAction();
                        subMenu->addAction(tr("Select all"), [&](){
                            std::vector<int> indices; 
                            for (const auto &vv : v.second)
                                indices.push_back(vv.second);
                            select(idx, indices);
                        });
                        subMenu->addSeparator();
                        for (const auto &vv : v.second) {
                            int sidx = vv.second;
                            subMenu->addAction(elementName(vv.first), [=](){select(idx,{sidx});});
                        }
                    }
                    auto vp = Application::Instance->getViewProvider(v.first.getSubObject());
                    if (vp)
                        action->setIcon(vp->getIcon());
                }
            }
        }

        auto vp = Application::Instance->getViewProvider(sels[0].getSubObject());
        if (vp)
            act->setIcon(vp->getIcon());
    }

}

void SelStackAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _menu = new QMenu();
        action()->setMenu(_menu);
        connect(_menu, &QMenu::aboutToShow, this, [this]() {populate();});
    }
    if (qobject_cast<QToolBar*>(widget))
        setupMenuToolButton(widget);
    widget->addAction(action());
}

// --------------------------------------------------------------------

class CmdHistoryMenu: public QMenu
{
public:
    CmdHistoryMenu(QWidget *focus)
        :focusWidget(focus)
    {}
protected:
    void keyPressEvent(QKeyEvent *e)
    {
        if (e->key() == Qt::Key_Space) {
            if (!isVisible()) {
                keyFocus = true;
                exec(QCursor::pos());
            } else {
                focusWidget->setFocus();
                e->accept();
            }
            return;
        }
        QMenu::keyPressEvent(e);
        return;
    }

    bool event(QEvent *e)
    {
        if (e->type() == QEvent::ShortcutOverride) {
            auto ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Space
                    && ke->modifiers() == Qt::NoModifier)
            {
                e->accept();
                return true;
            }
        }
        return QMenu::event(e);
    }

    void showEvent(QShowEvent *ev)
    {
        QMenu::showEvent(ev);
        if (keyFocus) {
            keyFocus = false;
            focusWidget->setFocus();
        }
    }

public:
    QWidget *focusWidget;
    bool keyFocus = false;
};

// --------------------------------------------------------------------
CmdHistoryAction::CmdHistoryAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent)
{
    qApp->installEventFilter(this);
}

CmdHistoryAction::~CmdHistoryAction()
{
    delete _menu;
}

void CmdHistoryAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _lineedit = new QLineEdit;
        _lineedit->setPlaceholderText(tr("Press SPACE to search"));
        _widgetAction = new QWidgetAction(this);
        _widgetAction->setDefaultWidget(_lineedit);
        auto completer = new CommandCompleter(_lineedit, this);
        connect(completer, &CommandCompleter::commandActivated,
                this, &CmdHistoryAction::onCommandActivated);

        _completer = completer;
        _newAction = new QAction(tr("Add toolbar or menu"), this);
        _newAction->setToolTip(tr("Create a Global customized toolbar or menu.\n"
                                  "Or, hold SHIFT key to create a toolbar/menu\n"
                                  "for the current workbench."));
        connect(_newAction, &QAction::triggered, this, &CmdHistoryAction::onNewAction);

        _menu = new CmdHistoryMenu(_lineedit);
        setupMenuStyle(_menu);
        _menu->addAction(_widgetAction);
        _menu->addAction(_newAction);
        action()->setMenu(_menu);
        connect(_menu, &QMenu::aboutToShow, this, &CmdHistoryAction::onShowMenu);
    }

    if (qobject_cast<QToolBar*>(widget))
        setupMenuToolButton(widget);
    widget->addAction(action());
}

static long _RecentCommandID;
static std::map<long, const char *, std::greater<long> > _RecentCommands;
static std::unordered_map<std::string, long> _RecentCommandMap;
static long _RecentCommandPopulated;
static QElapsedTimer _ButtonTime;

std::vector<Command*> CmdHistoryAction::recentCommands()
{
    auto &manager = Application::Instance->commandManager();
    std::vector<Command*> cmds;
    cmds.reserve(_RecentCommands.size());
    for (auto &v : _RecentCommands) {
        auto cmd = manager.getCommandByName(v.second);
        if (cmd)
            cmds.push_back(cmd);
    }
    return cmds;
}

void CmdHistoryAction::onNewAction()
{
    Application::Instance->commandManager().runCommandByName("Std_DlgCustomize", 1);
}

bool CmdHistoryAction::eventFilter(QObject *, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::MouseButtonPress: {
        auto e = static_cast<QMouseEvent*>(ev);
        if (e->button() == Qt::LeftButton)
            _ButtonTime.start();
        break;
    }
    default:
        break;
    }
    return false;
}

void CmdHistoryAction::onInvokeCommand(const char *name, bool force)
{
    if(!force && (!_ButtonTime.isValid() || _ButtonTime.elapsed() > 1000))
        return;

    _ButtonTime.invalidate();

    auto &manager = Application::Instance->commandManager();
    Command *cmd = manager.getCommandByName(name);
    if (!cmd || qobject_cast<CmdHistoryAction*>(cmd->getAction())
             || qobject_cast<ToolbarMenuAction*>(cmd->getAction()))
        return;
    
    if (!force && (cmd->getType() & Command::NoHistory) && !_RecentCommandMap.count(name))
        return;

    auto res = _RecentCommandMap.insert(std::make_pair(name, 0));
    if (!res.second)
        _RecentCommands.erase(res.first->second);
    res.first->second = ++_RecentCommandID;
    _RecentCommands[_RecentCommandID] = res.first->first.c_str();
    if (ViewParams::getCommandHistorySize() < (int)_RecentCommandMap.size()) {
        auto it = _RecentCommands.end();
        --it;
        _RecentCommandMap.erase(it->second);
        _RecentCommands.erase(it);
    }
}

void CmdHistoryAction::onShowMenu()
{
    setupMenuStyle(_menu);

    _menu->setFocus(Qt::PopupFocusReason);
    _lineedit->setText(QString());

    if (_RecentCommandPopulated == _RecentCommandID)
        return;

    _RecentCommandPopulated = _RecentCommandID;
    _menu->clear();
    _menu->addAction(_widgetAction);
    _menu->addAction(_newAction);
    _menu->addSeparator();
    auto &manager = Application::Instance->commandManager();
    for (auto &v : _RecentCommands)
        manager.addTo(v.second, _menu);
}

void CmdHistoryAction::onCommandActivated(const QByteArray &name)
{
    _menu->hide();

    auto &manager = Application::Instance->commandManager();
    if (name.size()) {
        manager.runCommandByName(name.constData());
        onInvokeCommand(name.constData(), true);
    }
}

void CmdHistoryAction::popup(const QPoint &pt)
{
    PieMenu::exec(_menu, pt, command()->getName(), true);
}

// --------------------------------------------------------------------

class ToolbarMenuAction::Private: public ParameterGrp::ObserverType
{
public:
    Private(ToolbarMenuAction *master, const char *path):master(master)
    {
        handle = App::GetApplication().GetParameterGroupByPath(path);
    }

    void OnChange(Base::Subject<const char*> &, const char *)
    {
        if (!updating) {
            Base::StateLocker guard(updating);
            master->update();
        }
    }

public:
    ToolbarMenuAction *master;
    ParameterGrp::handle handle;
    ParameterGrp::handle hShortcut;
    std::set<std::string> cmds;
    bool updating = false;
};

// --------------------------------------------------------------------

class GuiExport ToolbarMenuSubAction : public ToolbarMenuAction
{
public:
    ToolbarMenuSubAction (Command* pcCmd, ParameterGrp::handle hGrp, QObject * parent = 0)
        : ToolbarMenuAction(pcCmd, parent)
    {
        _pimpl->handle = hGrp;
        _pimpl->handle->Attach(_pimpl.get());
    }

    ~ToolbarMenuSubAction() {
        _pimpl->handle->Detach(_pimpl.get());
    }

protected:
    virtual void onShowMenu() {
        setupMenuStyle(_menu);

        auto &manager = Application::Instance->commandManager();
        if (revision == manager.getRevision())
            return;
        _menu->clear();
        for (auto &v : _pimpl->handle->GetASCIIMap()) {
            if (v.first == "Name")
                setText(QString::fromUtf8(v.second.c_str()));
            else if (boost::starts_with(v.first, "Separator"))
                _menu->addSeparator();
            else
                manager.addTo(v.first.c_str(), _menu);
        }
        revision = manager.getRevision();
    }

    virtual void update()
    {
        revision = 0;
        std::string text = _pimpl->handle->GetASCII("Name", "");
        this->setText(QString::fromUtf8(text.c_str()));
    }

private:
    int revision = 0;
};

// --------------------------------------------------------------------

class StdCmdToolbarSubMenu : public Gui::Command
{
public:
    StdCmdToolbarSubMenu(const char *name, ParameterGrp::handle hGrp)
        :Command(name)
    {
        menuText      = hGrp->GetASCII("Name", "Custom");
        sGroup        = "Tools";
        sMenuText     = menuText.c_str();
        sWhatsThis    = "Std_CmdToolbarSubMenu";
        eType         = NoTransaction | NoHistory;

        _pcAction = new ToolbarMenuSubAction(this, hGrp, getMainWindow());
        applyCommandData(this->className(), _pcAction);
    }

    virtual ~StdCmdToolbarSubMenu() {}

    virtual const char* className() const
    { return "StdCmdToolbarSubMenu"; }

protected:
    virtual void activated(int) {
        if (_pcAction)
            static_cast<ToolbarMenuSubAction*>(_pcAction)->popup(QCursor::pos());
    }
    virtual bool isActive() { return true;}

    virtual Gui::Action * createAction() {
        assert(false);
        return nullptr;
    }

private:
    std::string menuText;
};

// --------------------------------------------------------------------

ToolbarMenuAction::ToolbarMenuAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent), _menu(nullptr)
  , _pimpl(new Private(this, "User parameter:BaseApp/Workbench/Global/Toolbar"))
{
    _pimpl->hShortcut = WindowParameter::getDefaultParameter()->GetGroup("Shortcut");
    _pimpl->hShortcut->Attach(_pimpl.get());

    Application::Instance->signalActivateWorkbench.connect(
        [](const char*) {
            ToolbarMenuAction::populate();
        });
}

ToolbarMenuAction::~ToolbarMenuAction()
{
    _pimpl->hShortcut->Detach(_pimpl.get());
    delete _menu;
}

void ToolbarMenuAction::addTo ( QWidget * widget )
{
    if (!_menu) {
        _menu = new QMenu;
        setupMenuStyle(_menu);
        action()->setMenu(_menu);
        update();
        connect(_menu, &QMenu::aboutToShow, this, &ToolbarMenuAction::onShowMenu);
    }
    if (qobject_cast<QToolBar*>(widget))
        setupMenuToolButton(widget);
    widget->addAction(action());
}

void ToolbarMenuAction::onShowMenu()
{
    setupMenuStyle(_menu);
}

void ToolbarMenuAction::populate()
{
    auto &manager = Application::Instance->commandManager();
    auto cmd = manager.getCommandByName("Std_CmdToolbarMenus");
    if (!cmd)
        return;
    auto action = qobject_cast<ToolbarMenuAction*>(cmd->getAction());
    if (action)
        action->update();
}

std::string ToolbarMenuAction::paramName(const char *name, const char *workbench)
{
    if (!name)
        name = "";

    if (!workbench || strcmp(workbench, "Global")==0)
        return std::string("Std_ToolbarMenu_") + name;

    std::string res("Std_WBMenu_");
    res += workbench;
    if (name[0])
        res += "_";
    return res + name;
}

void ToolbarMenuAction::update()
{
    if (_pimpl->updating)
        return;
    Base::StateLocker guard(_pimpl->updating);

    auto &manager = Application::Instance->commandManager();
    _menu->clear();
    std::set<std::string> cmds;

    auto addCommand = [&](ParameterGrp::handle hGrp, const char *workbench) {
        std::string name = paramName(hGrp->GetGroupName(), workbench);
        QString shortcut = QString::fromUtf8(_pimpl->hShortcut->GetASCII(name.c_str()).c_str());
        if (shortcut.isEmpty())
            return;

        if (!cmds.insert(name).second)
            return;

        auto res = _pimpl->cmds.insert(name);
        Command *cmd = manager.getCommandByName(name.c_str());
        if (!cmd) {
            cmd = new StdCmdToolbarSubMenu(res.first->c_str(), hGrp);
            manager.addCommand(cmd);
        }
        if (cmd->getAction() && cmd->getAction()->shortcut() != shortcut)
            cmd->getAction()->setShortcut(shortcut);
        cmd->addTo(_menu);
    };

    for (auto &hGrp : _pimpl->handle->GetGroups()) {
        if(hGrp->GetASCII("Name","").empty())
            continue;
        addCommand(hGrp, nullptr);
    }

    auto wb = WorkbenchManager::instance()->active();
    if (wb) {
        auto hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Workbench");
        if (hGrp->HasGroup(wb->name().c_str())) {
            hGrp = hGrp->GetGroup(wb->name().c_str());
            if (hGrp->HasGroup("Toolbar")) {
                for (auto h : hGrp->GetGroup("Toolbar")->GetGroups()) {
                    if(h->GetASCII("Name","").empty())
                        continue;
                    addCommand(h, wb->name().c_str());
                }
            }
        }
    }

    for (auto it=_pimpl->cmds.begin(); it!=_pimpl->cmds.end();) {
        if (cmds.count(*it)) {
            ++it;
            continue;
        }
        Command *cmd = manager.getCommandByName(it->c_str());
        if (cmd)
            manager.removeCommand(cmd);
        it = _pimpl->cmds.erase(it);
    }
}

void ToolbarMenuAction::popup(const QPoint &pt)
{
    _menu->setActiveAction(nullptr);
    PieMenu::exec(_menu, pt, command()->getName());

    // WARNING! In some case, this ToolbarMenuAction instance may have been
    // destroyed after returning from PieMenu::exec(). E.g. when the toolbar
    // menu is a PartDesign context menu/command, and the action triggers
    // switching to Sketcher workbench, causing all PartDesign context commands
    // being destroyed. So, must not access any member here.
    //
    // _menu->setActiveAction(nullptr);
}

////////////////////////////////////////////////////////////////////

class ExpressionAction::Private {
public:
    void init(QMenu *menu)
    {
        pcActionCopySel = menu->addAction(QObject::tr("Copy selected"));
        pcActionCopyActive = menu->addAction(QObject::tr("Copy active document"));
        pcActionCopyAll = menu->addAction(QObject::tr("Copy all documents"));
        pcActionPaste = menu->addAction(QObject::tr("Paste"));
    }

    void onAction(QAction *action) {
        if (action == pcActionPaste) {
            pasteExpressions();
            return;
        }

        std::map<App::Document*, std::set<App::DocumentObject*> > objs;
        if (action == pcActionCopySel) {
            for(auto &sel : Selection().getCompleteSelection())
                objs[sel.pObject->getDocument()].insert(sel.pObject);
        }
        else if (action == pcActionCopyActive) {
            if(App::GetApplication().getActiveDocument()) {
                auto doc = App::GetApplication().getActiveDocument();
                auto array = doc->getObjects();
                auto &set = objs[doc];
                set.insert(array.begin(),array.end());
            }
        }
        else if (action == pcActionCopyAll) {
            for(auto doc : App::GetApplication().getDocuments()) {
                auto &set = objs[doc];
                auto array = doc->getObjects();
                set.insert(array.begin(),array.end());
            }
        }
        copyExpressions(objs);
    }

    void copyExpressions(const std::map<App::Document*, std::set<App::DocumentObject*> > &objs)
    {
        std::ostringstream ss;
        std::vector<App::Property*> props;
        for(auto &v : objs) {
            for(auto obj : v.second) {
                props.clear();
                obj->getPropertyList(props);
                for(auto prop : props) {
                    auto p = dynamic_cast<App::PropertyExpressionContainer*>(prop);
                    if(!p) continue;
                    for(auto &v : p->getExpressions()) {
                        ss << "##@@ " << v.first.toString() << ' '
                            << obj->getFullName() << '.' << p->getName()
                            << " (" << obj->Label.getValue() << ')' << std::endl;
                        ss << "##@@";
                        if(v.second->comment.size()) {
                            if(v.second->comment[0] == '&' 
                                    || v.second->comment.find('\n') != std::string::npos
                                    || v.second->comment.find('\r') != std::string::npos)
                            {
                                std::string comment = v.second->comment;
                                boost::replace_all(comment,"&","&amp;");
                                boost::replace_all(comment,"\n","&#10;");
                                boost::replace_all(comment,"\r","&#13;");
                                ss << '&' << comment;
                            }else
                                ss << v.second->comment;
                        }
                        ss << std::endl << v.second->toStr(true,true) << std::endl << std::endl;
                    }
                }
            }
        }
        QApplication::clipboard()->setText(QString::fromUtf8(ss.str().c_str()));
    }

    void pasteExpressions() {
        std::map<App::Document*, std::map<App::PropertyExpressionContainer*, 
            std::map<App::ObjectIdentifier, App::ExpressionPtr> > > exprs;

        bool failed = false;
        std::string txt = QApplication::clipboard()->text().toUtf8().constData();
        const char *tstart = txt.c_str();
        const char *tend = tstart + txt.size();

        static boost::regex rule("^##@@ ([^ ]+) (\\w+)#(\\w+)\\.(\\w+) [^\n]+\n##@@([^\n]*)\n");
        boost::cmatch m;
        if(!boost::regex_search(tstart,m,rule)) {
            FC_WARN("No expression header found");
            return;
        }
        boost::cmatch m2;
        bool found = true;
        for(;found;m=m2) {
            found = boost::regex_search(m[0].second,tend,m2,rule);

            auto pathName = m.str(1);
            auto docName = m.str(2);
            auto objName = m.str(3);
            auto propName = m.str(4);
            auto comment = m.str(5);

            App::Document *doc = App::GetApplication().getDocument(docName.c_str());
            if(!doc) {
                FC_WARN("Cannot find document '" << docName << "'");
                continue;
            }

            auto obj = doc->getObject(objName.c_str());
            if(!obj) {
                FC_WARN("Cannot find object '" << docName << '#' << objName << "'");
                continue;
            }

            auto prop = dynamic_cast<App::PropertyExpressionContainer*>(
                    obj->getPropertyByName(propName.c_str()));
            if(!prop) {
                FC_WARN("Invalid property '" << docName << '#' << objName << '.' << propName << "'");
                continue;
            }

            size_t len = (found?m2[0].first:tend) - m[0].second;
            try {
                // Check if the expression body contains a single
                // non-whitespace character '#'.  This is used to signal the
                // user's intention of unbinding the property.
                bool empty = false;
                const char *t = m[0].second;
                for(;*t && std::isspace(static_cast<unsigned char>(*t));++t);
                if(*t == '#') {
                    for(++t;*t && std::isspace(static_cast<unsigned char>(*t));++t);
                    empty = !*t;
                }
                App::ExpressionPtr expr;
                if(!empty)
                    expr = App::Expression::parse(obj,std::string(m[0].second,len));
                if(expr && comment.size()) {
                    if(comment[0] == '&') {
                        expr->comment = comment.c_str()+1;
                        boost::replace_all(expr->comment,"&amp;","&");
                        boost::replace_all(expr->comment,"&#10;","\n");
                        boost::replace_all(expr->comment,"&#13;","\r");
                    } else
                        expr->comment = comment;
                }
                exprs[doc][prop][App::ObjectIdentifier::parse(obj,pathName)] = std::move(expr);
            } catch(Base::Exception &e) {
                FC_ERR(e.what() << std::endl << m[0].str());
                failed = true;
            }
        }
        if(failed) {
            QMessageBox::critical(getMainWindow(), QObject::tr("Expression error"),
                QObject::tr("Failed to parse some of the expressions.\n"
                            "Please check the Report View for more details."));
            return;
        }

        App::AutoTransaction guard("Paste expressions");
        try {
            for(auto &v : exprs) {
                for(auto &v2 : v.second) {
                    auto &expressions = v2.second;
                    auto old = v2.first->getExpressions();
                    for(auto it=expressions.begin(),itNext=it;it!=expressions.end();it=itNext) {
                        ++itNext;
                        if(!it->second)
                            continue;
                        auto iter = old.find(it->first);
                        if(iter != old.end() && it->second->isSame(*iter->second))
                            expressions.erase(it);
                    }
                    if(expressions.size())
                        v2.first->setExpressions(std::move(expressions));
                }
            }
        } catch (const Base::Exception& e) {
            e.ReportException();
            App::GetApplication().closeActiveTransaction(true);
            QMessageBox::critical(getMainWindow(), QObject::tr("Failed to paste expressions"),
                QString::fromUtf8(e.what()));
        }
    }

    void update() {
        if(!App::GetApplication().getActiveDocument()) {
            pcActionCopyAll->setEnabled(false);
            pcActionCopySel->setEnabled(false);
            pcActionCopyActive->setEnabled(false);
            pcActionPaste->setEnabled(false);
            return;
        }
        pcActionCopyActive->setEnabled(true);
        pcActionCopyAll->setEnabled(true);
        pcActionCopySel->setEnabled(Selection().hasSelection());

        QString txt = QApplication::clipboard()->text();
        pcActionPaste->setEnabled(txt.startsWith(QStringLiteral("##@@ ")));
    }

    QAction *pcActionCopyAll;
    QAction *pcActionCopySel;
    QAction *pcActionCopyActive;
    QAction *pcActionPaste;
};

ExpressionAction::ExpressionAction (Command* pcCmd, QObject * parent)
    : Action(pcCmd, parent), _menu(nullptr), _pimpl(new Private)
{
}

ExpressionAction::~ExpressionAction ()
{
}

void ExpressionAction::addTo ( QWidget * w )
{
    if (!_menu) {
        _menu = new QMenu();
        action()->setMenu(_menu);
        connect(_menu, &QMenu::aboutToShow, this, &ExpressionAction::onShowMenu);
        connect(_menu, &QMenu::triggered, this, &ExpressionAction::onAction);

        _pimpl->init(_menu);
    }

    if (qobject_cast<QToolBar*>(w))
        setupMenuToolButton(w);
    w->addAction(action());
}

void ExpressionAction::onAction(QAction *action) {
    _pimpl->onAction(action);
}

void ExpressionAction::onShowMenu()
{
    _pimpl->update();
}

void ExpressionAction::popup(const QPoint &pt)
{
    if (_menu)
        _menu->exec(pt);
}

// --------------------------------------------------------------------

PresetsAction::PresetsAction ( Command* pcCmd, QObject * parent )
  : Action(pcCmd, parent), _menu(0), _undoMenu(0)
{
}

PresetsAction::~PresetsAction()
{
    delete _menu;
    delete _undoMenu;
    // _packMenu is a child of _menu, and went with it
}

namespace {
// Both of these keep state that lives in parameters which have just been
// rewritten underneath them, so they have to be told. PreferencePackManager
// reloads the same two after applying a pack, for the same reason.
void reloadWindowState()
{
    DockWindowManager::instance()->loadState();
    ToolBarManager::getInstance()->restoreState();
}
}

void PresetsAction::addTo ( QWidget * w )
{
    if (!_menu) {
      _menu = new QMenu();
      _menu->setToolTipsVisible(true);
      action()->setMenu(_menu);
      connect(_menu, &QMenu::aboutToShow, this, &PresetsAction::onShowMenu);
      connect(_menu, &QMenu::triggered, this, &PresetsAction::onAction);
    }

    if (qobject_cast<QToolBar*>(w))
        setupMenuToolButton(w);
    w->addAction(action());
}

void PresetsAction::onAction(QAction *action) {
    bool revert = (QApplication::queryKeyboardModifiers() == Qt::ControlModifier);
    auto pack = action->property("PreferencePack");
    if (pack.isValid())
        applyPreferencePack(pack.toString(), revert);
    else
        applyPreset(action->data().toByteArray(), action->text(), revert);
}

void PresetsAction::applyPreset(const QByteArray &name, const QString &title, bool revert)
{
    auto param = App::GetApplication().GetParameterSet(name.constData());
    if (param) {
        push(revert ? tr("Revert %1").arg(title) : title);
        if (revert)
            App::GetApplication().GetUserParameter().revert(param.get());
        else {
            auto manager = &App::GetApplication().GetUserParameter();
            param->insertTo(manager);
            // Remove name and tooltip in preset parameters from the application
            // setting
            manager->RemoveASCII("Name");
            manager->RemoveASCII("ToolTip");
        }
    }
}

void PresetsAction::applyPreferencePack(const QString &name, bool revert)
{
    auto manager = Application::Instance->prefPackManager();
    if (!revert) {
        // No push() here: apply() does it, so that a theme picked on the Theme
        // preferences page lands on this same stack rather than only in the
        // backup files it writes beside it.
        manager->apply(name.toStdString());
        return;
    }
    // A pack has no un-apply of its own, but its .cfg is exactly the file
    // ParameterGrp::revert() wants: it drops the keys still equal to the
    // pack's, so they fall back to the coded defaults and anything the user
    // changed since survives.
    auto configFile = manager->configFileFor(name.toStdString());
    if (configFile.empty())
        return;
    push(tr("Revert %1").arg(name));
    App::GetApplication().GetUserParameter().revert(configFile.string().c_str());
    reloadWindowState();
}

PresetsAction *PresetsAction::instance()
{
    // Called from PreferencePackManager, which is reachable before the command
    // is registered and in a session with no command manager at all
    if (!Application::Instance)
        return nullptr;
    auto cmd = Application::Instance->commandManager().getCommandByName("Std_CmdPresets");
    if (cmd)
        return static_cast<PresetsAction*>(cmd->getAction());
    return nullptr;
}

QStringList PresetsAction::undoTitles() const
{
    QStringList titles;
    for (auto it = _undos.rbegin(); it != _undos.rend(); ++it)
        titles.append(it->first);
    return titles;
}

QString PresetsAction::undo(int index)
{
    int i = (int)_undos.size() - 1 - index;
    if (index < 0 || i < 0)
        return QString();
    QString title = _undos[i].first;
    _undos[i].second->copyTo(&App::GetApplication().GetUserParameter());
    _undos.resize(i);
    reloadWindowState();
    return title;
}

void PresetsAction::push(const QString &title)
{
    auto manager = ParameterManager::Create();
    manager->CreateDocument();
    _undos.emplace_back(title, manager);
    if (_undos.size() > 10)
        _undos.pop_front();
    App::GetApplication().GetUserParameter().copyTo(_undos.back().second);
}

void PresetsAction::onShowMenu()
{
    _menu->clear();
    QString tooltip = tr("Click to apply the setting.\nCtrl + Click to revert to default.");
    QAction *pos = nullptr;
    for (auto &v : App::GetApplication().GetParameterSetList()) {
        if (v.second == &App::GetApplication().GetUserParameter()
                || v.second == &App::GetApplication().GetSystemParameter()
                || !v.second->GetBool("Preset", true))
            continue;
        auto action = new QAction(_menu);
        QString t;
        if (boost::starts_with(v.second->GetSerializeFileName(), App::GetApplication().getResourceDir())) {
            _menu->insertAction(pos, action);
            action->setText(tr(v.second->GetASCII("Name", v.first.c_str()).c_str()));
            t = tr(v.second->GetASCII("ToolTip").c_str());
        } else {
            if (!pos)
                pos = _menu->addSeparator();
            _menu->addAction(action);
            action->setText(QString::fromUtf8(v.second->GetASCII("Name", v.first.c_str()).c_str()));
            t = QString::fromUtf8(v.second->GetASCII("ToolTip").c_str());
        }
        if (t.size())
            t += QStringLiteral("\n\n");
        t += tooltip;
        action->setToolTip(t);
        action->setData(QByteArray(v.first.c_str()));
    }

    // Preference packs are the same idea as a preset -- a set of parameters
    // laid over the user's -- kept in a different place, with their metadata
    // in a package.xml instead of inside the file. Listing them here gives
    // them the undo and the Ctrl + Click un-apply they have nowhere else.
    QString packTip = tr("Click to apply the preference pack.\n"
                         "Ctrl + Click to drop the settings it applied.");
    if (!_packMenu) {
        // Parented, unlike the undo menu below, and it matters: a submenu's
        // action reaches onAction() through QMenu::triggered of its parent
        // menu, and off the mouse path Qt finds that parent by walking
        // parentWidget(). clear() does not take the submenu with it -- the
        // action it is added by belongs to the submenu, not to _menu.
        _packMenu = new QMenu(tr("Themes"), _menu);
        _packMenu->setToolTipsVisible(true);
    }
    _packMenu->clear();
    bool separated = false;
    for (const auto &v : Application::Instance->prefPackManager()->preferencePacks()) {
        auto name = QString::fromUtf8(v.first.c_str());
        auto metadata = v.second.metadata();
        // A theme is a pack too, but there are more of them than of everything
        // else in this menu put together, and they have a preferences page of
        // their own to be picked from. One entry for all of them, not nine.
        QMenu *target = _packMenu;
        if (metadata.type() != "Theme") {
            target = _menu;
            if (!separated) {
                separated = true;
                _menu->addSeparator();
            }
        }
        auto action = new QAction(name, target);
        QString t = QString::fromUtf8(metadata.description().c_str());
        if (t.size())
            t += QStringLiteral("\n\n");
        t += packTip;
        action->setToolTip(t);
        action->setProperty("PreferencePack", name);
        target->addAction(action);
    }
    if (!_packMenu->isEmpty()) {
        _menu->addSeparator();
        _menu->addMenu(_packMenu);
    }

    if (_undos.size()) {
        _menu->addSeparator();
        if (!_undoMenu) {
            _undoMenu = new QMenu(tr("Undo"));
            _undoMenu->setToolTipsVisible(true);
            QObject::connect(_undoMenu, &QMenu::aboutToShow, [this]() {
                _undoMenu->clear();
                int index = 0;
                for (const auto &title : undoTitles()) {
                    _undoMenu->addAction(title, [this, index]() { undo(index); });
                    ++index;
                }
            });
        }
        _menu->addMenu(_undoMenu);
    }
}

void PresetsAction::popup(const QPoint &pt)
{
    PieMenu::exec(_menu, pt, command()->getName(), true);
}

#include "moc_Action.cpp"
