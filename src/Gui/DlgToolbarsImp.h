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


#ifndef GUI_DIALOG_DLGTOOLBARS_IMP_H
#define GUI_DIALOG_DLGTOOLBARS_IMP_H

#include <fastsignals/signal.h>
#include <memory>
#include "PropertyPage.h"

class QTreeWidgetItem;

namespace Gui {

class PrefWidgetStates;

namespace Dialog {
class Ui_DlgCustomToolbars;

/** This class implements the creation of user defined toolbars.
 * In the left panel are shown all command groups with their command objects.
 * If any changeable toolbar was created in the left panel are shown all commands
 * of the currently edited toolbar, otherwise it is empty.
 * All changes to a toolbar is done immediately.
 *
 * \author Werner Mayer
 */
class DlgCustomToolbars : public CustomizeActionPage
{
    Q_OBJECT

protected:
    enum Type { Toolbar, Toolboxbar };

    explicit DlgCustomToolbars(Type, QWidget* parent = nullptr);
    ~DlgCustomToolbars() override;

protected:
    void setupConnections();
    void onWorkbenchBoxActivated(int index);
    void onMoveActionRightButtonClicked();
    void onMoveActionLeftButtonClicked();
    void onMoveActionUpButtonClicked();
    void onMoveActionDownButtonClicked();
    void onNewButtonClicked();
    void onRecentButtonClicked();
    void onRenameButtonClicked();
    void onDeleteButtonClicked();
    void onAssignButtonClicked();
    void onResetButtonClicked();
    void onToolbarTreeWidgetCurrentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *);
    void onToolbarTreeWidgetItemChanged(QTreeWidgetItem *, int);
    void onEditShortcutTextChanged(const QString& sc);

protected Q_SLOTS:
    void onAddMacroAction(const QByteArray&) override;
    void onRemoveMacroAction(const QByteArray&) override;
    void onModifyMacroAction(const QByteArray&) override;

protected:
    void changeEvent(QEvent *e) override;
    void hideEvent(QHideEvent * event) override;
    bool checkWorkbench(QString *id=nullptr) const;
    virtual void addCustomToolbar(QString, const QString&);
    virtual void removeCustomToolbar(QString);
    virtual void renameCustomToolbar(QString, const QString&);
    virtual void addCustomCommand(QString, const QByteArray&);
    virtual void removeCustomCommand(QString, const QByteArray&);
    virtual void moveUpCustomCommand(QString, const QByteArray&);
    virtual void moveDownCustomCommand(QString, const QByteArray&);

private:
    void importCustomToolbars(const QByteArray&);
    void exportCustomToolbars(const QByteArray&, QTreeWidgetItem *item=nullptr);

protected:
    std::unique_ptr<Ui_DlgCustomToolbars> ui;
private:
    Type type;
    std::unique_ptr<Gui::PrefWidgetStates> widgetStates;
    fastsignals::scoped_connection conn;
};

/** This class implements the creation of user defined toolbars.
 * @see DlgCustomToolbars
 * @see DlgCustomCmdbarsImp
 * \author Werner Mayer
 */
class DlgCustomToolbarsImp : public DlgCustomToolbars
{
    Q_OBJECT

public:
    explicit DlgCustomToolbarsImp(QWidget* parent = nullptr);
    ~DlgCustomToolbarsImp() override;

    void createRecentToolbar();

protected:
    void changeEvent(QEvent *e) override;
    void addCustomToolbar(QString, const QString&) override;
    void removeCustomToolbar(QString) override;
    void renameCustomToolbar(QString, const QString&) override;
    void addCustomCommand(QString, const QByteArray&) override;
    void removeCustomCommand(QString, const QByteArray&) override;
    void moveUpCustomCommand(QString, const QByteArray&) override;
    void moveDownCustomCommand(QString, const QByteArray&) override;

private:
    QList<QAction*> getActionGroup(QAction*);
    void setActionGroup(QAction*, const QList<QAction*>& group);
};

/** This class implements the creation of user defined toolbox bars.
 * A toolbox bar is the same as a toolbar - a collection of several
 * action objects - unless a toolbox bar is placed in a toolbox,
 * while a toolbar is placed in the dock areas of the main window.
 * So toolbox bars are predestinated to save place on your desktop.
 * @see DlgCustomToolbars
 * @see DlgCustomToolbarsImp
 * \author Werner Mayer
 */
class DlgCustomToolBoxbarsImp : public DlgCustomToolbars
{
    Q_OBJECT

public:
    explicit DlgCustomToolBoxbarsImp(QWidget* parent = nullptr);
    ~DlgCustomToolBoxbarsImp() override;

protected:
    void changeEvent(QEvent *e) override;
};

} // namespace Dialog
} // namespace Gui

#endif // GUI_DIALOG_DLGTOOLBARS_IMP_H
