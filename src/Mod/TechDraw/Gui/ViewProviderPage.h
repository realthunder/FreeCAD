/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2012 Luke Parry <l.parry@warwick.ac.uk>                 *
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

#ifndef DRAWINGGUI_VIEWPROVIDERPAGE_H
#define DRAWINGGUI_VIEWPROVIDERPAGE_H

#include <QObject>
#include <QPointer>

#include <fastsignals/signal.h>

#include <App/PropertyUnits.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Mod/TechDraw/TechDrawGlobal.h>

#include <ViewProviderPageExtension.h>


namespace TechDraw
{
class DrawPage;
class DrawTemplate;
}// namespace TechDraw

namespace TechDrawGui
{

class MDIViewPage;
class QGVPage;
class QGSPage;
class QGITemplate;

class TechDrawGuiExport ViewProviderPage: public Gui::ViewProviderDocumentObject,
                                          public ViewProviderPageExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(TechDrawGui::ViewProviderPage);

public:
    /// constructor
    ViewProviderPage();
    /// destructor
    ~ViewProviderPage() override;

    App::PropertyBool ShowFrames;
    App::PropertyBool ShowGrid;
    App::PropertyDistance GridSpacing;

    void attach(App::DocumentObject*) override;

    bool canDragObjects() const override;
    bool canDragObject(App::DocumentObject* docObj) const override;
    bool canDropObject(App::DocumentObject* docObj) const override;

    void dropObject(App::DocumentObject* docObj) override;
    void constDropObject(App::DocumentObject* docObj) const;

    bool canDropObjectEx(App::DocumentObject* obj, App::DocumentObject* owner, const char* subname,
                         const std::vector<std::string>& elements) const override;

    bool useNewSelectionModel() const override { return false; }
    /// Closes the page's view
    void hide() override;
    /// Opens the page's view; not while the page is restored, where the
    /// document's record of the open views decides (finishRestoring)
    void show() override;
    /// Always: a page has no eye in the tree, like a spreadsheet, and its
    /// item is not greyed when its view is closed
    bool isShow() const override;
    /// A file that carries no record of the open views (written by another
    /// program, or before the records) opens the page if it was visible
    void finishRestoring() override;

    /// Claim all the views for the page
    std::vector<App::DocumentObject*> claimChildren() const override;

    /// Is called by the tree if the user double click on the object
    bool doubleClicked() override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;
    bool onDelete(const std::vector<std::string>&) override;
    /// The page is going, by any way -- a script, an undo, its document
    /// closing -- and its view goes first.
    void beforeDelete() override;
    void onChanged(const App::Property* prop) override;
    void updateData(const App::Property* prop) override;

    TechDraw::DrawPage* getDrawPage() const;
    TechDraw::DrawTemplate* getTemplate() const;
    QGITemplate* getQTemplate(void) const;

    //slots & connections
    void onGuiRepaint(const TechDraw::DrawPage* dp);
    using Connection = fastsignals::scoped_connection;
    Connection connectGuiRepaint;

    void unsetEdit(int ModNum) override;
    MDIViewPage* getMDIViewPage() const;
    bool showMDIViewPage();
    void removeMDIView();

    Gui::MDIView* getMDIView() const override;
    /// The page's view, opened if it is not
    Gui::MDIView* getOrCreateMDIView() override;

    bool getFrameState();
    void setFrameState(bool state);
    void toggleFrameState();
    void setTemplateMarkers(bool state);

    bool canDelete(App::DocumentObject* obj) const override;

    void setGrid();

    QGSPage* getQGSPage(void) { return m_graphicsScene; }
    QGVPage* getQGVPage(void);

    ViewProviderPageExtension* getVPPExtension() const;

    const char* whoAmI() const;

    void fixSceneDependencies();


protected:
    bool setEdit(int ModNum) override;
    void createMDIViewPage();

private:
    QPointer<MDIViewPage> m_mdiView;
    std::string m_pageName;
    /// Inside finishRestoring(): a Visibility set there opens no view
    bool m_finishingRestore = false;
    // A Qt child of m_mdiView, deleted with it whichever way that goes
    QPointer<QGVPage> m_graphicsView;
    QGSPage* m_graphicsScene;
};

}// namespace TechDrawGui


#endif// DRAWINGGUI_VIEWPROVIDERPAGE_H
