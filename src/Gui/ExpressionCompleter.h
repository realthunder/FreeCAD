/***************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>             *
 *   Copyright (c) 2020 Zheng Lei <realthunder.dev@gmail.com>              *
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

#ifndef EXPRESSIONCOMPLETER_H
#define EXPRESSIONCOMPLETER_H

#include <vector>
#include <QCompleter>
#include <QLineEdit>
#include <QObject>
#include <QPlainTextEdit>
#include <App/DocumentObserver.h>
#include <App/ExpressionTokenizer.h>

class QStandardItem;

namespace App {
class Document;
class DocumentObject;
class Property;
class ObjectIdentifier;
}

namespace Gui {

/**
 * @brief The ExpressionCompleter class extends the QCompleter class to provide a completer model of documentobject names and properties.
 */

class GuiExport ExpressionCompleter : public QCompleter
{
    Q_OBJECT
public:
    ExpressionCompleter(const App::DocumentObject * currentDocObj,
                        QObject *parent = nullptr,
                        bool noProperty = false,
                        bool checkInList = true,
                        char leadChar = 0);

    void getPrefixRange(QString &prefix, int &start, int &end, int &offset) const;

    /** Completions for `text` with the caret at `pos`, showing nothing.
     *
     * The matches the popup would have listed, plus the prefix range
     * [start, end) in `text`'s own coordinates -- so a caller splices
     * text.left(start) + item + text.mid(end), the replacement
     * ExpressionLineEdit::slotCompleteText performs.
     *
     * For a remote client (a browser panel, docs/Sandbox.md 7.23),
     * which cannot use slotUpdate: that one ends in showPopup, and a
     * query must never raise a window on the desktop user's screen.
     * Ask this of a completer of your OWN -- setCompletionPrefix and
     * the tokenizer are mutable state, and the desktop's caret is not
     * the remote caller's.
     *
     * The match is contains, case-insensitive, filtered -- always: a
     * remote client's completion must not depend on the desktop user's
     * popup preferences (docs/Sandbox.md 7.25).
     *
     * Not named complete(): QCompleter::complete(const QRect&) is the
     * base class's "show the popup", which this must never do, and a
     * name that hid it would both mislead and break showPopup.
     */
    QStringList completionsFor(const QString &text, int pos, int &start, int &end,
                               QStringList *details = nullptr);

    void updatePrefixEnd(int end) {
        tokenizer.updatePrefixEnd(end);
    }

    void setDocumentObject(const App::DocumentObject*, bool checkInList=true);

    /** The objects a leading '.' refers to.
     *
     * By default that is the owner object of setDocumentObject(). Set,
     * the root of the completion lists the properties every one of the
     * objects has with the same type -- none for an empty list -- and no
     * sub-objects or elements; a selection of several objects edited
     * together. The owner object stays what paths are parsed against.
     */
    void setLocalObjects(const std::vector<App::DocumentObject*> &objs);

    void setNoProperty(bool enabled=true);

    void setSearchUnit(bool enabled=true);

    bool isInsideString() const {
        return tokenizer.isInsideString();
    }

    void setLeadChar(char lead) {
        tokenizer.setLeadChar(lead);
    }

    void setupContextMenu(QMenu *menu);

public Q_SLOTS:
    void slotUpdate(const QString &prefix, int pos);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

Q_SIGNALS:
    void visibilityChanged(bool visible);

private:
    void init();
    std::vector<App::DocumentObject*> localObjectPointers() const;
    QString pathFromIndex ( const QModelIndex & index ) const override;
    QStringList splitPath ( const QString & path ) const override;
    void showPopup(bool show);

    App::ExpressionTokenizer tokenizer;

    App::DocumentObjectT currentObj;
    bool checkInList = true;
    std::vector<App::DocumentObjectT> localObjs;
    bool localObjsSet = false;
};

class GuiExport ExpressionLineEdit : public QLineEdit {
    Q_OBJECT
public:
    ExpressionLineEdit(QWidget *parent = nullptr, bool noProperty=false,
            char leadChar=0, bool checkInList=true);
    void setDocumentObject(const App::DocumentObject *currentDocObj, bool checkInList=true);
    void setLeadChar(char lead);
    bool completerActive() const;
    void hideCompleter();
    void setNoProperty(bool enabled=true);
    void setSearchUnit(bool enabled=true);
Q_SIGNALS:
    void textChanged2(QString text, int pos);
    void sizeChanged();
    void completerVisibilityChanged(bool visible);
public Q_SLOTS:
    void slotTextChanged(const QString & text);
    void slotCompleteText(QString completionPrefix);
protected:
    void contextMenuEvent(QContextMenuEvent * event);
    void resizeEvent(QResizeEvent *);
private:
    ExpressionCompleter * completer;
    bool noProperty;
    bool checkInList;
    char leadChar;
    bool searchUnit;
};

class GuiExport ExpressionTextEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    ExpressionTextEdit(QWidget *parent = nullptr, char leadChar=0);
    void setLeadChar(char lead);
    void setDocumentObject(const App::DocumentObject *currentDocObj);
    bool completerActive() const;
    void hideCompleter();
protected:
    void keyPressEvent(QKeyEvent * event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void contextMenuEvent(QContextMenuEvent * event) override;
Q_SIGNALS:
    void textChanged2(QString text, int pos);
    void completerVisibilityChanged(bool visible);
public Q_SLOTS:
    void slotTextChanged();
    void slotCompleteText(QString completionPrefix);
private:
    ExpressionCompleter * completer;
    bool block;
    char leadChar;
};

}

#endif // EXPRESSIONCOMPLETER_H
