// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef TECHDRAWGUI_DLGPRINTPAGES_H
#define TECHDRAWGUI_DLGPRINTPAGES_H

#include <string>
#include <utility>
#include <vector>

#include <QDialog>

#include <Mod/TechDraw/TechDrawGlobal.h>

class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace App
{
class DocumentObject;
}

namespace TechDrawGui
{

/** The pages to print or export, chosen from every open document
 * (docs/HandsOnQueue.md entry 137, TechDraw_Print).
 *
 * A tree of the documents with pages and their pages, each with a check box;
 * a document's box checks or clears all its pages. Beside it, the pages
 * checked, as "document#page", in the order they print -- moved up and down
 * with the buttons under it; a page checked goes to the end. At first the
 * current document is open in the tree, scrolled to its first page, and all
 * its pages are checked; afterwards the last choice and its order come back,
 * for the session (not saved). Print opens the printer's dialog; Export PDF
 * asks for a file and writes one PDF.
 */
class TechDrawGuiExport DlgPrintPages: public QDialog
{
    Q_OBJECT

public:
    enum class Action
    {
        Print,
        ExportPdf,
    };

    explicit DlgPrintPages(QWidget* parent = nullptr);

    /// The pages checked, in the order they print
    std::vector<App::DocumentObject*> pages() const;
    /// Which button closed the dialog
    Action action() const;

    /// The command: the dialog, then the printer's dialog or a PDF file
    static void run(QWidget* parent = nullptr);

private:
    void fill();
    /// The order list after a check box changed: pages cleared go, pages
    /// checked come at the end
    void syncOrder();
    void move(int step);
    void updateButtons();
    void remember() const;
    static App::DocumentObject* findPage(const std::pair<std::string, std::string>& key);

    QTreeWidget* tree = nullptr;
    QListWidget* order = nullptr;
    QPushButton* upButton = nullptr;
    QPushButton* downButton = nullptr;
    QPushButton* printButton = nullptr;
    QPushButton* pdfButton = nullptr;
    Action chosen = Action::Print;
};

}  // namespace TechDrawGui

#endif  // TECHDRAWGUI_DLGPRINTPAGES_H
