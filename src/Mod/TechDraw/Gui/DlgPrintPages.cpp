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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/FileDialog.h>
#include <Gui/MainWindow.h>
#include <Gui/WaitCursor.h>
#include <Mod/TechDraw/App/DrawPage.h>

#include "DlgPrintPages.h"
#include "PagePrinter.h"

using namespace TechDrawGui;

namespace
{
/// A page as the dialog keeps it: its document's name and its own
using PageKey = std::pair<std::string, std::string>;

/// The last choice, in its order, for the session (not saved): the reporter,
/// "remember the last selections for the live session (no need persist on
/// disk)"
struct Remembered
{
    bool valid = false;
    std::vector<PageKey> pages;
    /// The documents all of whose pages were checked: a page added to one
    /// since is checked too, as its document's box says
    std::vector<std::string> wholeDocuments;
};

Remembered& remembered()
{
    static Remembered memory;
    return memory;
}

PageKey keyOf(const QVariant& data)
{
    const QStringList parts = data.toStringList();
    if (parts.size() != 2) {
        return {};
    }
    return {parts[0].toStdString(), parts[1].toStdString()};
}

QVariant dataOf(const App::DocumentObject* page)
{
    return QStringList {QString::fromUtf8(page->getDocument()->getName()),
                        QString::fromUtf8(page->getNameInDocument())};
}

/// "document#page", by their labels
QString titleOf(const App::DocumentObject* page)
{
    return QStringLiteral("%1#%2").arg(QString::fromUtf8(page->getDocument()->Label.getValue()),
                                       QString::fromUtf8(page->Label.getValue()));
}
}  // namespace

DlgPrintPages::DlgPrintPages(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Print Pages"));

    tree = new QTreeWidget(this);
    tree->setHeaderLabel(tr("Documents and their pages"));
    tree->setToolTip(tr("Check the pages to print. A document's box checks or clears all its pages."));

    order = new QListWidget(this);
    order->setToolTip(tr("The pages checked, in the order they are printed: drag one, or move it\n"
                         "with the buttons below"));
    // reordered by dragging as well as by the buttons
    order->setDragDropMode(QAbstractItemView::InternalMove);
    order->setDefaultDropAction(Qt::MoveAction);
    upButton = new QPushButton(tr("Move up"), this);
    downButton = new QPushButton(tr("Move down"), this);

    auto side = new QVBoxLayout;
    side->addWidget(new QLabel(tr("Print order"), this));
    side->addWidget(order, 1);
    auto moves = new QHBoxLayout;
    moves->addWidget(upButton);
    moves->addWidget(downButton);
    side->addLayout(moves);

    auto lists = new QHBoxLayout;
    lists->addWidget(tree, 1);
    lists->addLayout(side, 1);

    auto buttons = new QDialogButtonBox(this);
    printButton = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
    pdfButton = buttons->addButton(tr("Export PDF..."), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    printButton->setDefault(true);

    auto layout = new QVBoxLayout(this);
    layout->addLayout(lists, 1);
    layout->addWidget(buttons);
    resize(640, 420);

    connect(printButton, &QPushButton::clicked, this, [this]() {
        chosen = Action::Print;
    });
    connect(pdfButton, &QPushButton::clicked, this, [this]() {
        chosen = Action::ExportPdf;
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(upButton, &QPushButton::clicked, this, [this]() {
        move(-1);
    });
    connect(downButton, &QPushButton::clicked, this, [this]() {
        move(1);
    });
    connect(order, &QListWidget::currentRowChanged, this, [this]() {
        updateButtons();
    });

    fill();
    connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
        syncOrder();
    });
    updateButtons();
}

App::DocumentObject* DlgPrintPages::findPage(const PageKey& key)
{
    App::Document* doc = App::GetApplication().getDocument(key.first.c_str());
    if (!doc) {
        return nullptr;
    }
    App::DocumentObject* obj = doc->getObject(key.second.c_str());
    if (!obj || !obj->isDerivedFrom(TechDraw::DrawPage::getClassTypeId())) {
        return nullptr;
    }
    return obj;
}

void DlgPrintPages::fill()
{
    const QSignalBlocker blocker(tree);
    App::Document* active = App::GetApplication().getActiveDocument();
    const Remembered& memory = remembered();
    QTreeWidgetItem* firstOfActive = nullptr;

    // Every open document with pages, the pages in the document's order
    for (App::Document* doc : App::GetApplication().getDocuments()) {
        const auto pages = doc->getObjectsOfType(TechDraw::DrawPage::getClassTypeId());
        if (pages.empty()) {
            continue;
        }
        auto docItem = new QTreeWidgetItem(tree);
        docItem->setText(0, QString::fromUtf8(doc->Label.getValue()));
        docItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        for (App::DocumentObject* page : pages) {
            auto pageItem = new QTreeWidgetItem(docItem);
            pageItem->setText(0, QString::fromUtf8(page->Label.getValue()));
            pageItem->setData(0, Qt::UserRole, dataOf(page));
            pageItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            bool checked = false;
            if (memory.valid) {
                const PageKey key {doc->getName(), page->getNameInDocument()};
                checked = std::find(memory.pages.begin(), memory.pages.end(), key)
                        != memory.pages.end()
                    || std::find(memory.wholeDocuments.begin(), memory.wholeDocuments.end(),
                                 doc->getName())
                        != memory.wholeDocuments.end();
            }
            else {
                checked = doc == active;
            }
            pageItem->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            if (doc == active && !firstOfActive) {
                firstOfActive = pageItem;
            }
        }
        // the current document open, the others closed
        docItem->setExpanded(doc == active);
    }

    // The order: as remembered, then any other page checked
    order->clear();
    if (memory.valid) {
        for (const PageKey& key : memory.pages) {
            if (App::DocumentObject* page = findPage(key)) {
                auto item = new QListWidgetItem(titleOf(page), order);
                item->setData(Qt::UserRole, dataOf(page));
            }
        }
    }
    syncOrder();

    if (firstOfActive) {
        tree->scrollToItem(firstOfActive);
    }
}

void DlgPrintPages::syncOrder()
{
    // The pages checked in the tree, in the tree's order
    std::vector<PageKey> checked;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* docItem = tree->topLevelItem(i);
        for (int j = 0; j < docItem->childCount(); ++j) {
            QTreeWidgetItem* pageItem = docItem->child(j);
            if (pageItem->checkState(0) == Qt::Checked) {
                checked.push_back(keyOf(pageItem->data(0, Qt::UserRole)));
            }
        }
    }
    // Those cleared go from the list, those not in it yet come at its end
    std::vector<PageKey> listed;
    for (int row = order->count() - 1; row >= 0; --row) {
        const PageKey key = keyOf(order->item(row)->data(Qt::UserRole));
        if (std::find(checked.begin(), checked.end(), key) == checked.end()) {
            delete order->takeItem(row);
        }
        else {
            listed.push_back(key);
        }
    }
    for (const PageKey& key : checked) {
        if (std::find(listed.begin(), listed.end(), key) != listed.end()) {
            continue;
        }
        if (App::DocumentObject* page = findPage(key)) {
            auto item = new QListWidgetItem(titleOf(page), order);
            item->setData(Qt::UserRole, dataOf(page));
        }
    }
    updateButtons();
}

void DlgPrintPages::move(int step)
{
    const int row = order->currentRow();
    const int target = row + step;
    if (row < 0 || target < 0 || target >= order->count()) {
        return;
    }
    QListWidgetItem* item = order->takeItem(row);
    order->insertItem(target, item);
    order->setCurrentRow(target);
}

void DlgPrintPages::updateButtons()
{
    const int row = order->currentRow();
    upButton->setEnabled(row > 0);
    downButton->setEnabled(row >= 0 && row + 1 < order->count());
    const bool any = order->count() > 0;
    printButton->setEnabled(any);
    pdfButton->setEnabled(any);
}

std::vector<App::DocumentObject*> DlgPrintPages::pages() const
{
    std::vector<App::DocumentObject*> result;
    for (int row = 0; row < order->count(); ++row) {
        if (App::DocumentObject* page = findPage(keyOf(order->item(row)->data(Qt::UserRole)))) {
            result.push_back(page);
        }
    }
    return result;
}

DlgPrintPages::Action DlgPrintPages::action() const
{
    return chosen;
}

void DlgPrintPages::remember() const
{
    Remembered& memory = remembered();
    memory.valid = true;
    memory.pages.clear();
    for (int row = 0; row < order->count(); ++row) {
        memory.pages.push_back(keyOf(order->item(row)->data(Qt::UserRole)));
    }
    memory.wholeDocuments.clear();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* docItem = tree->topLevelItem(i);
        if (docItem->checkState(0) == Qt::Checked && docItem->childCount() > 0) {
            memory.wholeDocuments.push_back(
                keyOf(docItem->child(0)->data(0, Qt::UserRole)).first);
        }
    }
}

void DlgPrintPages::run(QWidget* parent)
{
    if (!parent) {
        parent = Gui::getMainWindow();
    }
    DlgPrintPages dlg(parent);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    dlg.remember();
    const std::vector<App::DocumentObject*> pages = dlg.pages();
    if (pages.empty()) {
        return;
    }
    // The banner page names the first page's document
    App::Document* doc = pages.front()->getDocument();

    QPrinter printer(QPrinter::HighResolution);
    printer.setFullPage(true);
    // landscape to start with, as most drawing sheets are; each page is still
    // printed in its own template's orientation
    printer.setPageOrientation(QPageLayout::Landscape);

    if (dlg.action() == Action::ExportPdf) {
        QStringList filter;
        filter << QObject::tr("PDF (*.pdf)");
        filter << QObject::tr("All Files (*.*)");
        QString fn = Gui::FileDialog::getSaveFileName(parent, QObject::tr("Export Pages As PDF"),
                                                      QString(), filter.join(QStringLiteral(";;")));
        if (fn.isEmpty()) {
            return;
        }
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(fn);
        Gui::WaitCursor wc;
        PagePrinter::printAllPdf(&printer, doc, pages);
        return;
    }

    QPrintDialog printDialog(&printer, parent);
    if (printDialog.exec() != QDialog::Accepted) {
        return;
    }
    if (printer.outputFileName().isEmpty()) {
        PagePrinter::printAll(&printer, doc, pages);
    }
    else {
        PagePrinter::printAllPdf(&printer, doc, pages);
    }
}

#include <Mod/TechDraw/Gui/moc_DlgPrintPages.cpp>
