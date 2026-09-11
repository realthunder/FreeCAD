/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
# include <algorithm>
# include <sstream>
# include <QAbstractItemView>
# include <QIcon>
# include <QApplication>
# include <QCheckBox>
# include <QCompleter>
# include <QHBoxLayout>
# include <QKeyEvent>
# include <QLabel>
# include <QMenu>
# include <QMouseEvent>
# include <QPainter>
# include <QPushButton>
# include <QStandardItemModel>
# include <QStyle>
# include <QStyledItemDelegate>
# include <QTimer>
# include <QToolButton>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/Property.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "OmniSearchBox.h"
#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "CallTips.h"
#include "Command.h"
#include "CommandCompleter.h"
#include "Document.h"
#include "ExpressionCompleter.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "PrefWidgets.h"
#include "Selection/Selection.h"
#include "Selection/SelectionView.h"
#include "Tree.h"
#include "ViewProviderDocumentObject.h"
#include "propertyeditor/PropertyItem.h"

using namespace Gui;
using namespace Gui::OmniSearch;
using App::ParamInfo;
using App::ParamRegistry;

namespace {

/** Two-line rows for the completer popups: icon, title, a grey description,
 * the shortcut or value on the right, and an arrow on a group command.
 */
class OmniItemDelegate : public QStyledItemDelegate
{
public:
    static constexpr int IconSize = 24;
    static constexpr int ArrowSize = 16;
    static constexpr int Margin = 4;

    explicit OmniItemDelegate(QObject *parent)
        : QStyledItemDelegate(parent)
    {}

    static QRect arrowRect(const QRect &itemRect)
    {
        return QRect(itemRect.right() - Margin - ArrowSize,
                     itemRect.top() + (itemRect.height() - ArrowSize) / 2,
                     ArrowSize, ArrowSize);
    }

    static bool isGroup(const QModelIndex &index)
    {
        return index.data(IsGroupRole).toBool();
    }

    // A row without a description (the object completer's) is one line high
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        int text = QFontMetrics(option.font).height();
        if (!index.data(DescriptionRole).toString().isEmpty()) {
            QFont small = option.font;
            small.setPointSizeF(small.pointSizeF() * 0.9);
            text += QFontMetrics(small).height();
        }
        int h = std::max(text, IconSize) + 2 * Margin;
        return QSize(option.rect.width(), h);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();

        QVariant activeVar = index.data(IsActiveRole);
        bool active = !activeVar.isValid() || activeVar.toBool();
        if (!active)
            opt.state &= ~QStyle::State_Enabled;

        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

        QRect r = opt.rect.adjusted(Margin, Margin, -Margin, -Margin);

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            QRect iconRect(r.left(), r.top() + (r.height() - IconSize) / 2, IconSize, IconSize);
            icon.paint(painter, iconRect, Qt::AlignCenter,
                       active ? QIcon::Normal : QIcon::Disabled);
        }
        r.setLeft(r.left() + IconSize + Margin * 2);

        bool group = isGroup(index);
        if (group) {
            QStyleOption arrow;
            arrow.rect = arrowRect(opt.rect);
            arrow.palette = opt.palette;
            arrow.state = opt.state & (QStyle::State_Enabled | QStyle::State_Selected);
            style->drawPrimitive(QStyle::PE_IndicatorArrowRight, &arrow, painter, widget);
            r.setRight(arrowRect(opt.rect).left() - Margin);
        }

        QPalette::ColorGroup cg = active ? QPalette::Normal : QPalette::Disabled;
        bool selected = opt.state & QStyle::State_Selected;
        QColor textColor = opt.palette.color(cg, selected ? QPalette::HighlightedText : QPalette::Text);
        if (!selected) {
            // The expression completer's model flags some rows by colour
            QVariant fg = index.data(Qt::ForegroundRole);
            if (fg.canConvert<QColor>() && fg.value<QColor>().isValid())
                textColor = fg.value<QColor>();
        }
        QColor dimColor = selected ? textColor : opt.palette.color(cg, QPalette::PlaceholderText);
        if (!dimColor.isValid())
            dimColor = textColor;

        QString title = index.data(TitleRole).toString();
        if (title.isEmpty())
            title = index.data(Qt::DisplayRole).toString();
        QString desc = index.data(DescriptionRole).toString();
        QString right = index.data(ShortcutRole).toString();

        QFont titleFont = opt.font;
        QFont smallFont = opt.font;
        smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);
        QFontMetrics titleFM(titleFont);
        QFontMetrics smallFM(smallFont);

        int textHeight = titleFM.height() + (desc.isEmpty() ? 0 : smallFM.height());
        int top = r.top() + (r.height() - textHeight) / 2;
        QRect titleRect(r.left(), top, r.width(), titleFM.height());
        QRect descRect(r.left(), top + titleFM.height(), r.width(), smallFM.height());

        painter->save();
        if (!right.isEmpty()) {
            painter->setFont(smallFont);
            painter->setPen(dimColor);
            int w = smallFM.horizontalAdvance(right) + Margin;
            QRect rightRect(titleRect.right() - w, titleRect.top(), w, titleRect.height());
            painter->drawText(rightRect, Qt::AlignRight | Qt::AlignVCenter, right);
            titleRect.setRight(rightRect.left() - Margin);
        }
        painter->setFont(titleFont);
        painter->setPen(textColor);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          titleFM.elidedText(title, Qt::ElideRight, titleRect.width()));
        if (!desc.isEmpty()) {
            painter->setFont(smallFont);
            painter->setPen(dimColor);
            painter->drawText(descRect, Qt::AlignLeft | Qt::AlignVCenter,
                              smallFM.elidedText(desc, Qt::ElideRight, descRect.width()));
        }
        painter->restore();
    }
};

} // anonymous namespace

namespace Gui {

/// The property editor of a resolved property, as the property view would build it
class OmniPropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OmniPropertyPanel(QWidget *parent)
        : QWidget(parent)
    {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        layout->addWidget(label);
        auto row = new QHBoxLayout;
        layout->addLayout(row);
        host = new QWidget(this);
        hostLayout = new QHBoxLayout(host);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        row->addWidget(host, 1);
        exprButton = new QToolButton(this);
        exprButton->setText(QStringLiteral("f(x)"));
        exprButton->setToolTip(tr("Edit as expression"));
        exprButton->setCheckable(true);
        exprButton->setAutoRaise(true);
        row->addWidget(exprButton);
        connect(exprButton, &QToolButton::toggled, this, &OmniPropertyPanel::toggleExpression);
    }

    ~OmniPropertyPanel() override
    {
        clear();
    }

    bool isFor(App::Property *p) const
    {
        return item && p == prop;
    }

    /// props: every property to edit together (prop first); empty means prop alone
    void setProperty(App::Property *p, const App::SubObjectT &objT,
                     const std::vector<App::Property*> &props = {})
    {
        if (isFor(p))
            return;
        clear();
        prop = p;
        obj = objT;
        if (!prop)
            return;
        std::vector<App::Property*> all = props;
        if (all.empty())
            all.push_back(prop);
        count = all.size();

        const char *editorName = prop->getEditorName();
        if (!editorName || !editorName[0])
            editorName = "Gui::PropertyEditor::PropertyItem";
        item = static_cast<PropertyEditor::PropertyItem*>(
                PropertyEditor::PropertyItemFactory::instance().createPropertyItem(editorName));
        if (!item) {
            label->setText(tr("%1: no editor for property type %2")
                    .arg(QString::fromUtf8(prop->getName()),
                         QString::fromUtf8(prop->getTypeId().getName())));
            return;
        }
        item->setPropertyName(*prop);
        item->setPropertyData(all);

        QString title = count > 1 ? tr("%1 objects").arg(count) : containerTitle();
        label->setText(QStringLiteral("%1 . %2").arg(title, QString::fromUtf8(prop->getName())));
        label->setToolTip(QString::fromUtf8(prop->getDocumentation()));

        openTransaction();
        exprButton->setVisible(item->isBound());
        {
            QSignalBlocker blocker(exprButton);
            exprButton->setChecked(item->hasExpression());
        }
        buildEditor(exprButton->isChecked());
    }

    void clear()
    {
        if (editor) {
            editor->removeEventFilter(this);
            delete editor;
            editor = nullptr;
        }
        closeTransaction();
        delete item;
        item = nullptr;
        prop = nullptr;
        label->clear();
    }

    void focusEditor()
    {
        if (editor)
            editor->setFocus();
    }

Q_SIGNALS:
    /// Return pressed in the editor: the edit is done
    void finished();

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(e);
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                    && ke->modifiers() == Qt::NoModifier) {
                // Let the editor commit (a spin box's editingFinished) first
                QTimer::singleShot(0, this, [this]() { Q_EMIT finished(); });
            }
        }
        return QWidget::eventFilter(o, e);
    }

private Q_SLOTS:
    void onValueChanged()
    {
        auto w = qobject_cast<QWidget*>(sender());
        if (!w || !item || !editor)
            return;
        applyValue(item->editorData(editor));
    }

    void onExpressionChanged()
    {
        if (!item || !editor)
            return;
        applyValue(item->expressionEditorData(editor));
    }

    void onUserEditValue(const QVariant &v)
    {
        applyValue(v);
    }

    void toggleExpression(bool on)
    {
        if (!item)
            return;
        buildEditor(on);
        focusEditor();
    }

private:
    void applyValue(const QVariant &v)
    {
        if (!item)
            return;
        try {
            item->setData(v);
        }
        catch (Base::Exception &e) {
            e.ReportException();
        }
    }

    void buildEditor(bool expression)
    {
        if (editor) {
            editor->removeEventFilter(this);
            delete editor;
            editor = nullptr;
        }
        if (!item)
            return;
        if (expression) {
            editor = item->createExpressionEditor(host, this, SLOT(onExpressionChanged()));
            if (editor)
                item->setExpressionEditorData(editor, item->data(1, Qt::EditRole));
        }
        else if (prop->testStatus(App::Property::UserEdit)) {
            auto w = item->createPropertyEditorWidget(host);
            w->setValue(PropertyEditor::PropertyItemAttorney::toString(item, item->data(1, Qt::EditRole)));
            connect(w, &PropertyEditor::PropertyEditorWidget::valueChanged,
                    this, &OmniPropertyPanel::onUserEditValue);
            editor = w;
        }
        else {
            editor = item->createEditor(host, this, SLOT(onValueChanged()));
            if (editor) {
                item->setEditorData(editor, item->data(1, Qt::EditRole));
                if (item->isReadOnly())
                    item->disableEditor(editor);
            }
        }
        if (!editor) {
            auto l = new QLabel(item->data(1, Qt::DisplayRole).toString(), host);
            l->setTextFormat(Qt::PlainText);
            editor = l;
        }
        editor->installEventFilter(this);
        for (auto child : editor->findChildren<QWidget*>())
            child->installEventFilter(this);
        hostLayout->addWidget(editor);
        editor->show();
    }

    // What the property belongs to, spelled the way the box addresses it
    QString containerTitle() const
    {
        auto parent = prop->getContainer();
        if (auto doc = Base::freecad_dynamic_cast<App::Document>(parent))
            return QString::fromUtf8(doc->getName()) + QStringLiteral("#");
        if (auto view = Base::freecad_dynamic_cast<MDIView>(parent)) {
            auto doc = view->getAppDocument();
            std::string name = view->getPersistentName();
            if (name.empty())
                name = "ActiveView";
            return (doc ? QString::fromUtf8(doc->getName()) : QString())
                + QStringLiteral("#.") + QString::fromUtf8(name.c_str());
        }
        QString title = QString::fromUtf8(obj.getSubObjectFullName().c_str());
        if (Base::freecad_dynamic_cast<ViewProviderDocumentObject>(parent))
            title += QStringLiteral(".ViewObject");
        return title;
    }

    App::Document *document() const
    {
        if (!prop)
            return nullptr;
        auto parent = prop->getContainer();
        if (auto doc = Base::freecad_dynamic_cast<App::Document>(parent))
            return doc;
        if (auto view = Base::freecad_dynamic_cast<MDIView>(parent))
            return view->getAppDocument();
        App::DocumentObject *object = Base::freecad_dynamic_cast<App::DocumentObject>(parent);
        if (!object) {
            if (auto view = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(parent))
                object = view->getObject();
        }
        if (object && object->isAttachedToDocument())
            return object->getDocument();
        return nullptr;
    }

    // As PropertyEditor::openEditor() and closeTransaction(): the edit is
    // one undo step, and the document recomputes when it ends.
    void openTransaction()
    {
        auto &app = App::GetApplication();
        if (app.getActiveTransaction())
            return;
        auto doc = document();
        if (!doc || doc->hasPendingTransaction())
            return;
        std::ostringstream str;
        str << tr("Edit").toUtf8().constData() << ' ';
        if (count > 1)
            str << count << ' ' << tr("objects").toUtf8().constData();
        else if (auto object = obj.getObject())
            str << object->Label.getValue();
        else
            str << tr("document").toUtf8().constData();
        str << ' ' << prop->getName();
        transactionID = app.setActiveTransaction(str.str().c_str());
        docT = doc;
    }

    void closeTransaction()
    {
        int tid = 0;
        auto &app = App::GetApplication();
        if (transactionID && app.getActiveTransaction(&tid) && tid == transactionID) {
            try {
                if (auto doc = docT.getDocument()) {
                    if (!doc->isTransactionEmpty()
                            && !doc->testStatus(App::Document::Recomputing)
                            && doc->mustExecute())
                        doc->recompute();
                }
            }
            catch (Base::Exception &e) {
                e.ReportException();
            }
            app.closeActiveTransaction();
        }
        transactionID = 0;
    }

    QLabel *label = nullptr;
    QWidget *host = nullptr;
    QHBoxLayout *hostLayout = nullptr;
    QToolButton *exprButton = nullptr;
    PropertyEditor::PropertyItem *item = nullptr;
    QWidget *editor = nullptr;
    App::Property *prop = nullptr;
    App::SubObjectT obj;
    App::DocumentT docT;
    int transactionID = 0;
    size_t count = 0;
};

/// The editor of a chosen parameter, built from its proxy, applying as it changes
class OmniParamPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OmniParamPanel(QWidget *parent)
        : QWidget(parent)
    {
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        label = new QLabel(this);
        label->setTextFormat(Qt::PlainText);
        layout->addWidget(label);
        pathLabel = new QLabel(this);
        pathLabel->setTextFormat(Qt::PlainText);
        pathLabel->setWordWrap(true);
        pathLabel->setEnabled(false);
        layout->addWidget(pathLabel);
        auto row = new QHBoxLayout;
        layout->addLayout(row);
        host = new QWidget(this);
        hostLayout = new QHBoxLayout(host);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        row->addWidget(host, 1);
        resetButton = new QPushButton(tr("Reset"), this);
        resetButton->setToolTip(tr("Remove the stored value so the default applies"));
        row->addWidget(resetButton);
        connect(resetButton, &QPushButton::clicked, this, &OmniParamPanel::reset);
    }

    void setParam(const ParamInfo *p)
    {
        if (p == info && editor)
            return;
        clear();
        info = p;
        if (!info)
            return;
        label->setText(QString::fromUtf8(info->displayPath().c_str()));
        QString doc = QCoreApplication::translate(info->className, info->doc);
        QString tip = QString::fromUtf8(info->fullPath().c_str());
        if (!doc.isEmpty())
            tip += QStringLiteral("\n\n") + doc;
        label->setToolTip(tip);
        QString title = QCoreApplication::translate(info->className, info->title);
        QString def = QString::fromUtf8(info->defaultValue.c_str());
        editor = createParamEditor(*info, host);
        if (!editor) {
            editor = new QLabel(tr("No editor"), host);
        }
        else if (auto pref = dynamic_cast<PrefWidget*>(editor)) {
            // Save on every change: the effect is the point of editing here
            pref->initAutoSave(QVariant(), true);
        }
        // A check box already carries the title as its text
        if (auto check = qobject_cast<QCheckBox*>(editor)) {
            if (check->text() == title)
                title.clear();
        }
        pathLabel->setText(title.isEmpty()
                ? tr("default: %1").arg(def)
                : tr("%1    default: %2").arg(title, def));
        editor->installEventFilter(this);
        for (auto child : editor->findChildren<QWidget*>())
            child->installEventFilter(this);
        hostLayout->addWidget(editor);
        editor->show();
    }

    void clear()
    {
        delete editor;
        editor = nullptr;
        info = nullptr;
        label->clear();
        pathLabel->clear();
    }

    void focusEditor()
    {
        if (editor)
            editor->setFocus();
    }

Q_SIGNALS:
    void finished();

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(e);
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                    && ke->modifiers() == Qt::NoModifier) {
                QTimer::singleShot(0, this, [this]() { Q_EMIT finished(); });
            }
        }
        return QWidget::eventFilter(o, e);
    }

private Q_SLOTS:
    void reset()
    {
        if (!info)
            return;
        ParamRegistry::instance().reset(*info);
        if (auto pref = dynamic_cast<PrefWidget*>(editor))
            pref->onRestore();
    }

private:
    QLabel *label = nullptr;
    QLabel *pathLabel = nullptr;
    QWidget *host = nullptr;
    QHBoxLayout *hostLayout = nullptr;
    QPushButton *resetButton = nullptr;
    QWidget *editor = nullptr;
    const ParamInfo *info = nullptr;
};

} // namespace Gui

// ---------------------------------------------------------------------------
// OmniSearchEdit

OmniSearchEdit::OmniSearchEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setPlaceholderText(tr("/ objects and properties, /cmd commands, /param parameters"));
    setupChooser();
    setupCommands();
    setupParams();
    setupMembers();
    connect(this, &QLineEdit::textEdited, this, &OmniSearchEdit::onTextEdited);
}

OmniSearchEdit::~OmniSearchEdit() = default;

void OmniSearchEdit::setupChooser()
{
    auto model = new QStandardItemModel(this);
    struct Row { Mode mode; const char *title; QString desc; };
    const Row rows[] = {
        {Mode::Object, "/", tr("Documents, objects, sub-objects and properties")},
        {Mode::Command, "/cmd", tr("Commands")},
        {Mode::Param, "/param", tr("Application parameters")},
    };
    for (const auto &row : rows) {
        auto item = new QStandardItem(QString::fromLatin1(modePrefix(row.mode)));
        item->setData(QString::fromLatin1(row.title), TitleRole);
        item->setData(row.desc, DescriptionRole);
        item->setData(QString::fromLatin1(modePrefix(row.mode)), SearchTextRole);
        model->appendRow(item);
    }
    chooser = new QCompleter(model, this);
    chooser->setWidget(this);
    chooser->setCompletionMode(QCompleter::PopupCompletion);
    chooser->setFilterMode(Qt::MatchStartsWith);
    chooser->setCaseSensitivity(Qt::CaseInsensitive);
    chooser->popup()->setItemDelegate(new OmniItemDelegate(chooser->popup()));
    chooser->popup()->installEventFilter(this);
    connect(chooser, qOverload<const QString&>(&QCompleter::activated),
            this, &OmniSearchEdit::setInputText);
}

void OmniSearchEdit::setupCommands()
{
    static_assert(int(CommandListModel::SearchTextRole) == int(OmniSearch::SearchTextRole),
                  "the omni roles must line up with CommandListModel's");
    static_assert(int(CommandListModel::IsGroupRole) == int(OmniSearch::IsGroupRole),
                  "the omni roles must line up with CommandListModel's");
    cmdModel = new CommandListModel(this);
    cmdFilter = new KeywordFilterModel(this);
    cmdFilter->setSourceModel(cmdModel);
    cmdCompleter = new QCompleter(cmdFilter, this);
    cmdCompleter->setWidget(this);
    cmdCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    cmdCompleter->popup()->setItemDelegate(new OmniItemDelegate(cmdCompleter->popup()));
    cmdCompleter->popup()->installEventFilter(this);
    cmdCompleter->popup()->viewport()->installEventFilter(this);
    connect(cmdCompleter, qOverload<const QModelIndex&>(&QCompleter::activated),
            this, [this](const QModelIndex &index) {
                justActivated = true;
                if (index.data(IsActiveRole).toBool())
                    Q_EMIT commandChosen(index.data(CommandListModel::CommandNameRole).toByteArray());
            });
}

void OmniSearchEdit::setupParams()
{
    paramModel = new ParamListModel(this);
    paramFilter = new KeywordFilterModel(this);
    paramFilter->setSourceModel(paramModel);
    paramCompleter = new QCompleter(paramFilter, this);
    paramCompleter->setWidget(this);
    paramCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    paramCompleter->popup()->setItemDelegate(new OmniItemDelegate(paramCompleter->popup()));
    paramCompleter->popup()->installEventFilter(this);
    connect(paramCompleter, qOverload<const QModelIndex&>(&QCompleter::activated),
            this, [this](const QModelIndex &index) {
                justActivated = true;
                if (auto info = ParamListModel::infoOf(index))
                    Q_EMIT paramChosen(info);
            });
}

// The members of a document ("#.") or of one of its views ("#.View1."):
// the expression completer's model has no row for either, so their tails
// are completed from OmniSearch::documentMembers() instead.
void OmniSearchEdit::setupMembers()
{
    memberModel = new QStandardItemModel(this);
    memberFilter = new KeywordFilterModel(this);
    memberFilter->setSourceModel(memberModel);
    memberCompleter = new QCompleter(memberFilter, this);
    memberCompleter->setWidget(this);
    memberCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    memberCompleter->popup()->setItemDelegate(new OmniItemDelegate(memberCompleter->popup()));
    memberCompleter->popup()->installEventFilter(this);
    connect(memberCompleter, qOverload<const QModelIndex&>(&QCompleter::highlighted),
            this, [this](const QModelIndex &index) {
                completeMember(index.data(Qt::EditRole).toString(), false);
            });
    connect(memberCompleter, qOverload<const QModelIndex&>(&QCompleter::activated),
            this, [this](const QModelIndex &index) {
                QString name = index.data(Qt::EditRole).toString();
                completeMember(name, true);
                if (!name.endsWith(QLatin1Char('.')))
                    activateObject();
            });
}

void OmniSearchEdit::setOwner(App::DocumentObject *owner)
{
    if (owner && !owner->isAttachedToDocument())
        owner = nullptr;
    ownerObj = owner;
    if (objCompleter) {
        objCompleter->setDocumentObject(owner);
        return;
    }
    if (!owner)
        return;
    objCompleter = new ExpressionCompleter(owner, this, /*noProperty*/false, /*checkInList*/false);
    objCompleter->setLocalObjects(localObjects());
    objCompleter->setWidget(this);
    objCompleter->popup()->setItemDelegate(new OmniItemDelegate(objCompleter->popup()));
    objCompleter->popup()->installEventFilter(this);
    // Moving through the list only completes the text; picking a row (a
    // click here, Tab in the key handling) is what commits it.
    connect(objCompleter, qOverload<const QString&>(&QCompleter::highlighted),
            this, &OmniSearchEdit::completeObject);
    connect(objCompleter, qOverload<const QString&>(&QCompleter::activated),
            this, [this](const QString &completion) {
                completeObject(completion);
                activateObject();
            });
}

App::DocumentObject *OmniSearchEdit::owner() const
{
    return ownerObj.getObject();
}

void OmniSearchEdit::setLocalObjects(const std::vector<App::DocumentObject*> &objs)
{
    localObjs.clear();
    for (auto obj : objs) {
        if (obj && obj->isAttachedToDocument())
            localObjs.emplace_back(obj);
    }
    if (objCompleter)
        objCompleter->setLocalObjects(objs);
}

std::vector<App::DocumentObject*> OmniSearchEdit::localObjects() const
{
    std::vector<App::DocumentObject*> objs;
    for (auto &t : localObjs) {
        if (auto obj = t.getObject())
            objs.push_back(obj);
    }
    return objs;
}

void OmniSearchEdit::setInputText(const QString &text)
{
    setText(text);
    setCursorPosition(text.size());
    onTextEdited(text);
}

// The chooser popup is a window of its own: shown before the box is the
// active window it is torn down by the activation that follows, so wait
// for the focus that activation brings.
void OmniSearchEdit::requestChooser()
{
    if (hasFocus()) {
        chooserPending = false;
        setInputText(QStringLiteral("/"));
        return;
    }
    chooserPending = true;
}

void OmniSearchEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    if (chooserPending) {
        chooserPending = false;
        QTimer::singleShot(0, this, [this]() {
            if (isVisible() && hasFocus())
                setInputText(QStringLiteral("/"));
        });
    }
}

QCompleter *OmniSearchEdit::activeCompleter() const
{
    switch (input.mode) {
    case Mode::Chooser:
        return chooser;
    case Mode::Object:
        if (memberCompleter->popup()->isVisible())
            return memberCompleter;
        return objCompleter;
    case Mode::Command:
        return cmdCompleter;
    case Mode::Param:
        return paramCompleter;
    }
    return nullptr;
}

bool OmniSearchEdit::popupVisible() const
{
    for (auto c : {chooser, static_cast<QCompleter*>(objCompleter), cmdCompleter, paramCompleter,
                   memberCompleter}) {
        if (c && c->popup()->isVisible())
            return true;
    }
    return false;
}

void OmniSearchEdit::hidePopups()
{
    for (auto c : {chooser, static_cast<QCompleter*>(objCompleter), cmdCompleter, paramCompleter,
                   memberCompleter}) {
        if (c)
            c->popup()->hide();
    }
}

QRect OmniSearchEdit::popupRect() const
{
    QRect r = rect();
    if (r.width() < 300)
        r.setWidth(300);
    return r;
}

int OmniSearchEdit::filteredRowCount() const
{
    switch (input.mode) {
    case Mode::Command:
        return cmdFilter->rowCount();
    case Mode::Param:
        return paramFilter->rowCount();
    default:
        return 0;
    }
}

QModelIndex OmniSearchEdit::filteredRow(int row) const
{
    switch (input.mode) {
    case Mode::Command:
        return cmdFilter->index(row, 0);
    case Mode::Param:
        return paramFilter->index(row, 0);
    default:
        return {};
    }
}

static void showListPopup(QCompleter *completer, const QRect &rect)
{
    completer->complete(rect);
    auto popup = completer->popup();
    if (!popup->currentIndex().isValid() && completer->completionModel()->rowCount())
        popup->setCurrentIndex(completer->completionModel()->index(0, 0));
}

void OmniSearchEdit::onTextEdited(const QString &text)
{
    if (completing)
        return;
    justActivated = false;
    Input parsed = parseInput(text);
    bool changed = parsed.mode != input.mode;
    input = parsed;
    if (changed) {
        hidePopups();
        Q_EMIT modeChanged(input.mode);
    }

    switch (input.mode) {
    case Mode::Chooser:
        chooser->setCompletionPrefix(text);
        showListPopup(chooser, popupRect());
        break;
    case Mode::Object:
        runObjectQuery();
        break;
    case Mode::Command:
        cmdModel->update();
        cmdFilter->setKeywords(input.query);
        if (cmdFilter->rowCount())
            showListPopup(cmdCompleter, popupRect());
        else
            cmdCompleter->popup()->hide();
        break;
    case Mode::Param:
        paramFilter->setKeywords(input.query);
        if (paramFilter->rowCount())
            showListPopup(paramCompleter, popupRect());
        else
            paramCompleter->popup()->hide();
        break;
    }
}

// The '#' of "#Box": the expression completer only knows "Doc#Box", so
// it never sees that one
int OmniSearchEdit::objectSkip() const
{
    return input.query.startsWith(QLatin1Char('#'))
        && !input.query.startsWith(QLatin1String("#.")) ? 1 : 0;
}

void OmniSearchEdit::runObjectQuery()
{
    QString head, tail;
    if (splitMemberQuery(input.query, head, tail)) {
        if (objCompleter)
            objCompleter->popup()->hide();
        memberModel->clear();
        static QIcon docIcon(BitmapFactory().pixmap("Document"));
        for (auto &m : documentMembers(head, owner())) {
            auto item = new QStandardItem(m.name);
            item->setData(m.name, TitleRole);
            item->setData(m.description, DescriptionRole);
            item->setData(m.name, SearchTextRole);
            item->setIcon(m.name.endsWith(QLatin1Char('.'))
                    ? docIcon : CallTipsList::iconOfType(CallTip::Property));
            memberModel->appendRow(item);
        }
        memberFilter->setKeywords(tail);
        if (memberFilter->rowCount())
            showListPopup(memberCompleter, popupRect());
        else
            memberCompleter->popup()->hide();
        resolveObjectQuery();
        return;
    }
    memberCompleter->popup()->hide();
    if (objCompleter) {
        int skip = objectSkip();
        objCompleter->slotUpdate(input.query.mid(skip), cursorPosition() - input.offset - skip);
        // The completer's lazy init() re-sets its popup, which moves its
        // own filter ahead of ours; ours must see Tab and Return first,
        // as ExpressionCompleter turns Tab into Down and swallows it.
        auto popup = objCompleter->popup();
        popup->removeEventFilter(this);
        popup->installEventFilter(this);
    }
    resolveObjectQuery();
}

void OmniSearchEdit::resolveObjectQuery()
{
    ObjectMatch match;
    auto locals = localObjects();
    if (owner() && resolveObject(input.query, owner(), match, &locals)) {
        resolvedProp = match.prop;
        resolvedObj = match.obj;
        Q_EMIT objectResolved(match);
    }
    else {
        resolvedProp = nullptr;
        resolvedObj = App::SubObjectT();
        Q_EMIT objectUnresolved();
    }
}

// The splice of ExpressionLineEdit::slotCompleteText(), offset by the
// mode prefix the completer never sees.
void OmniSearchEdit::completeObject(const QString &completion)
{
    if (!objCompleter || input.mode != Mode::Object)
        return;
    Base::StateLocker guard(completing);
    int start, end, offset;
    QString prefix(completion);
    objCompleter->getPrefixRange(prefix, start, end, offset);
    int skip = objectSkip();
    QString query = input.query.mid(skip);
    // For a property or child of the owner object the model completes to
    // the expression shorthand ".Name" (a member of "this" object). The
    // owner here is only the document's first object, so keep what was
    // typed in front of the dot: "Box.Len" -> "Box.Length", not ".Length".
    if (prefix.startsWith(QLatin1Char('.'))) {
        QString replaced = query.mid(start, end - start);
        int dot = replaced.lastIndexOf(QLatin1Char('.'));
        if (dot > 0)
            prefix = replaced.left(dot) + prefix;
    }
    QString before = query.left(start) + prefix;
    QString after = query.mid(end);
    QString full = text().left(input.offset + skip) + before + after;
    setText(full);
    setCursorPosition(input.offset + skip + before.length() + offset);
    objCompleter->updatePrefixEnd(before.length());
    input = parseInput(full);
    resolveObjectQuery();
}

// A row of the member popup: the text becomes head + name. With advance,
// a name ending in '.' ("View1.") opens the next level's popup.
void OmniSearchEdit::completeMember(const QString &name, bool advance)
{
    QString head, tail;
    if (input.mode != Mode::Object || !splitMemberQuery(input.query, head, tail))
        return;
    Base::StateLocker guard(completing);
    QString full = text().left(input.offset) + head + name;
    setText(full);
    setCursorPosition(full.size());
    input = parseInput(full);
    if (advance && name.endsWith(QLatin1Char('.'))) {
        // Not from inside the popup's own activation
        QTimer::singleShot(0, this, [this]() { runObjectQuery(); });
        return;
    }
    resolveObjectQuery();
}

void OmniSearchEdit::activateObject()
{
    ObjectMatch match;
    auto locals = localObjects();
    if (owner() && resolveObject(input.query, owner(), match, &locals))
        Q_EMIT objectActivated(match);
}

// Tab on a popup: take its current row (the first when none is), as a
// click on it would.
bool OmniSearchEdit::chooseCurrentRow()
{
    auto c = activeCompleter();
    if (!c || !c->popup()->isVisible())
        return false;
    auto popup = c->popup();
    QModelIndex index = popup->currentIndex();
    if (!index.isValid() && popup->model() && popup->model()->rowCount())
        index = popup->model()->index(0, 0);
    if (!index.isValid())
        return false;
    popup->hide();
    switch (input.mode) {
    case Mode::Chooser:
        setInputText(index.data(chooser->completionRole()).toString());
        break;
    case Mode::Object:
        if (c == memberCompleter) {
            QString name = index.data(Qt::EditRole).toString();
            completeMember(name, true);
            if (!name.endsWith(QLatin1Char('.')))
                activateObject();
        }
        else {
            completeObject(index.data(objCompleter->completionRole()).toString());
            activateObject();
        }
        break;
    case Mode::Command:
        if (index.data(IsActiveRole).toBool())
            Q_EMIT commandChosen(index.data(CommandListModel::CommandNameRole).toByteArray());
        break;
    case Mode::Param:
        if (auto info = ParamListModel::infoOf(index))
            Q_EMIT paramChosen(info);
        break;
    }
    return true;
}

bool OmniSearchEdit::expandGroupAt(const QModelIndex &index)
{
    if (!index.isValid() || !index.data(IsGroupRole).toBool())
        return false;
    auto popup = cmdCompleter->popup();
    QRect r = popup->visualRect(index);
    QRect global(popup->viewport()->mapToGlobal(r.topLeft()), r.size());
    Q_EMIT groupExpandRequested(index.data(CommandListModel::CommandNameRole).toByteArray(), global);
    return true;
}

bool OmniSearchEdit::eventFilter(QObject *obj, QEvent *event)
{
    // Keys go to the popup while one is up, and the completers forward
    // them to the edit's event() -- past any filter on the edit -- so the
    // popups are filtered instead: Tab picks the current row, Shift+Tab
    // moves up in it.
    auto active = activeCompleter();
    if (active && obj == active->popup() && event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Tab && ke->modifiers() == Qt::NoModifier) {
            if (chooseCurrentRow())
                return true;
        }
        else if (ke->key() == Qt::Key_Backtab) {
            QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
            QApplication::sendEvent(active->popup(), &up);
            return true;
        }
    }
    // The command popup: the arrow of a group row expands it
    if (cmdCompleter && obj == cmdCompleter->popup()->viewport()
            && event->type() == QEvent::MouseButtonPress) {
        auto me = static_cast<QMouseEvent*>(event);
        auto popup = cmdCompleter->popup();
        QModelIndex index = popup->indexAt(me->pos());
        if (index.isValid() && OmniItemDelegate::isGroup(index)
                && OmniItemDelegate::arrowRect(popup->visualRect(index)).contains(me->pos())) {
            expandGroupAt(index);
            return true;
        }
    }
    if (cmdCompleter && obj == cmdCompleter->popup() && event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Right && ke->modifiers() == Qt::NoModifier) {
            if (expandGroupAt(cmdCompleter->popup()->currentIndex()))
                return true;
        }
    }
    // The object popup: ExpressionCompleter swallows Return into the list
    // view, so the box never hears it; take it here.
    if (objCompleter && obj == objCompleter->popup() && event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && ke->modifiers() == Qt::NoModifier) {
            objCompleter->popup()->hide();
            Q_EMIT enterPressed();
            return true;
        }
    }
    return QLineEdit::eventFilter(obj, event);
}

void OmniSearchEdit::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (justActivated) {
            // The popup already answered this key with an activation
            justActivated = false;
            event->accept();
            return;
        }
        if (!popupVisible()) {
            Q_EMIT enterPressed();
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QLineEdit::keyPressEvent(event);
}

void OmniSearchEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    if (objCompleter)
        objCompleter->setupContextMenu(menu);
    menu->exec(event->globalPos());
    delete menu;
}

// ---------------------------------------------------------------------------
// OmniSearchBox

OmniSearchBox *OmniSearchBox::instance()
{
    static QPointer<OmniSearchBox> inst;
    if (!inst)
        inst = new OmniSearchBox(getMainWindow());
    return inst;
}

OmniSearchBox::OmniSearchBox(QWidget *parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("OmniSearchBox"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setAutoFillBackground(true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    lineEdit = new OmniSearchEdit(this);
    lineEdit->setObjectName(QStringLiteral("OmniSearchEdit"));
    layout->addWidget(lineEdit);

    panelHost = new QWidget(this);
    panelLayout = new QVBoxLayout(panelHost);
    panelLayout->setContentsMargins(0, 4, 0, 0);
    propertyPanel = new OmniPropertyPanel(panelHost);
    propertyPanel->setObjectName(QStringLiteral("OmniPropertyPanel"));
    paramPanel = new OmniParamPanel(panelHost);
    paramPanel->setObjectName(QStringLiteral("OmniParamPanel"));
    panelLayout->addWidget(propertyPanel);
    panelLayout->addWidget(paramPanel);
    propertyPanel->hide();
    paramPanel->hide();
    panelHost->hide();
    layout->addWidget(panelHost);

    connect(lineEdit, &OmniSearchEdit::modeChanged, this, &OmniSearchBox::onModeChanged);
    connect(lineEdit, &OmniSearchEdit::objectResolved, this, &OmniSearchBox::onObjectResolved);
    connect(lineEdit, &OmniSearchEdit::objectUnresolved, this, &OmniSearchBox::onObjectUnresolved);
    connect(lineEdit, &OmniSearchEdit::objectActivated, this, &OmniSearchBox::onObjectActivated);
    connect(lineEdit, &OmniSearchEdit::commandChosen, this, &OmniSearchBox::onCommandChosen);
    connect(lineEdit, &OmniSearchEdit::groupExpandRequested, this, &OmniSearchBox::onGroupExpandRequested);
    connect(lineEdit, &OmniSearchEdit::paramChosen, this, &OmniSearchBox::onParamChosen);
    connect(lineEdit, &OmniSearchEdit::enterPressed, this, &OmniSearchBox::onEnterPressed);
    connect(propertyPanel, &OmniPropertyPanel::finished, this, &OmniSearchBox::dismiss);
    connect(paramPanel, &OmniParamPanel::finished, this, &OmniSearchBox::dismiss);
}

OmniSearchBox::~OmniSearchBox()
{
    qApp->removeEventFilter(this);
}

TreeWidget *OmniSearchBox::tree() const
{
    return TreeWidget::instance();
}

void OmniSearchBox::open()
{
    App::DocumentObject *owner = nullptr;
    if (auto t = tree())
        owner = t->startItemSearch();
    if (!owner) {
        if (auto doc = Application::Instance->activeDocument()) {
            const auto &objs = doc->getDocument()->getObjects();
            if (!objs.empty())
                owner = objs.front();
        }
    }
    // The selection is what a leading '.' refers to, and its first object
    // is the owner queries are parsed against; without one the owner is
    // only a parsing context and '.' names nothing.
    std::vector<App::DocumentObject*> selected;
    for (const auto &sel : Selection().getSelectionT("*", ResolveMode::NoResolve)) {
        auto obj = sel.getSubObject();
        if (obj && obj->isAttachedToDocument()
                && std::find(selected.begin(), selected.end(), obj) == selected.end())
            selected.push_back(obj);
    }
    if (!selected.empty())
        owner = selected.front();
    lineEdit->setOwner(owner);
    lineEdit->setLocalObjects(selected);

    hidePanels();
    lineEdit->hidePopups();
    place();
    show();
    raise();
    activateWindow();
    lineEdit->requestChooser();
    lineEdit->setFocus();
    qApp->removeEventFilter(this);
    qApp->installEventFilter(this);
}

void OmniSearchBox::dismiss()
{
    hide();
}

void OmniSearchBox::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    lineEdit->hidePopups();
    hidePanels();
    if (auto t = tree())
        t->resetItemSearch();
    Selection().rmvPreselect();
    QFrame::hideEvent(event);
}

void OmniSearchBox::place()
{
    QWidget *anchor = getMainWindow();
    if (auto view = getMainWindow()->activeWindow())
        anchor = view;
    QRect g(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    int w = std::clamp(int(g.width() * 0.6), 400, 720);
    // Fixed width: the panels below the edit only ever change the height
    setFixedWidth(w);
    adjustSize();
    move(g.center().x() - w / 2, g.top() + 40);
}

void OmniSearchBox::showPanel(QWidget *panel)
{
    for (auto p : {static_cast<QWidget*>(propertyPanel), static_cast<QWidget*>(paramPanel)})
        p->setVisible(p == panel);
    panelHost->show();
    adjustSize();
}

void OmniSearchBox::hidePanels()
{
    propertyPanel->clear();
    paramPanel->clear();
    propertyPanel->hide();
    paramPanel->hide();
    panelHost->hide();
    if (isVisible())
        adjustSize();
}

bool OmniSearchBox::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress && obj->isWidgetType() && isVisible()) {
        auto w = static_cast<QWidget*>(obj);
        if (w == this || isAncestorOf(w))
            return false;
        if (menuRunning || QApplication::activePopupWidget() || QApplication::activeModalWidget())
            return false;
        // A click into the main window while the box shows is a dismissal
        dismiss();
    }
    return false;
}

// Losing the activation to another window -- the main window on a click
// into it, another application -- is a dismissal, unless the window is one
// of ours: the expression dialog a bound property's editor opens is a
// child of the box, and a menu or completer popup takes no activation.
bool OmniSearchBox::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        QTimer::singleShot(0, this, [this]() {
            if (!isVisible() || menuRunning || isActiveWindow())
                return;
            if (QApplication::activePopupWidget())
                return;
            if (auto active = QApplication::activeWindow()) {
                for (QWidget *w = active; w; w = w->parentWidget()) {
                    if (w == this)
                        return;
                }
            }
            dismiss();
        });
    }
    return QFrame::event(event);
}

// Esc with a popup up closes the popup (the completer takes it before the
// edit); Esc here means there was none, and closes the box.
void OmniSearchBox::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void OmniSearchBox::onModeChanged(Mode mode)
{
    hidePanels();
    if (mode != Mode::Object) {
        if (auto t = tree())
            t->resetItemSearch();
        Selection().rmvPreselect();
    }
}

// The text names something: show it in the tree. The property editor waits
// for the row to be picked (onObjectActivated) or for Enter -- not for a
// keystroke or the highlight moving through the popup.
void OmniSearchBox::onObjectResolved(const ObjectMatch &match)
{
    auto t = tree();
    if (match.prop) {
        if (panelHost->isVisible() && !propertyPanel->isFor(match.prop))
            hidePanels();
        // A ".Name" query is about the selection, which the tree shows already
        if (lineEdit->currentInput().query.trimmed().startsWith(QLatin1Char('.')))
            return;
        if (t)
            t->resetItemSearch();
        auto obj = match.obj.getObject();
        if (!obj)  // a document's or its view's member: nothing in the tree
            return;
        if (t) {
            if (auto item = TreeWidget::selectUp(match.obj, nullptr, false))
                t->scrollToItem(item);
        }
        {
            SelectionNoTopParentCheck guard;
            Selection().setPreselect(obj->getDocument()->getName(), obj->getNameInDocument(),
                                     match.obj.getSubName().c_str(), 0, 0, 0,
                                     SelectionChanges::MsgSource::TreeView);
        }
        return;
    }
    if (panelHost->isVisible())
        hidePanels();
    if (t)
        t->itemSearch(lineEdit->currentInput().query, false);
}

void OmniSearchBox::onObjectActivated(const ObjectMatch &match)
{
    if (!match.prop)
        return;
    if (!propertyPanel->isFor(match.prop)) {
        propertyPanel->setProperty(match.prop, match.obj, match.props);
        showPanel(propertyPanel);
    }
    propertyPanel->focusEditor();
}

void OmniSearchBox::onObjectUnresolved()
{
    if (panelHost->isVisible())
        hidePanels();
    if (auto t = tree())
        t->resetItemSearch();
}

void OmniSearchBox::selectObject(const ObjectMatch &match)
{
    if (auto t = tree())
        t->itemSearch(lineEdit->currentInput().query, true);
    lineEdit->hidePopups();
    QPoint pt = lineEdit->mapToGlobal(QPoint(0, lineEdit->height()));
    {
        Base::StateLocker guard(menuRunning);
        SelectionContext selctx;
        SelUpMenu menu(this);
        TreeWidget::populateSelUpMenu(&menu, &match.obj);
        if (menu.actions().isEmpty())
            menu.addAction(tr("<None>"))->setEnabled(false);
        TreeWidget::execSelUpMenu(&menu, pt);
    }
    dismiss();
}

void OmniSearchBox::onCommandChosen(const QByteArray &name)
{
    dismiss();
    auto &manager = Application::Instance->commandManager();
    if (name.size()) {
        manager.runCommandByName(name.constData());
        CmdHistoryAction::onInvokeCommand(name.constData(), true);
    }
}

void OmniSearchBox::onGroupExpandRequested(const QByteArray &name, const QRect &rect)
{
    auto cmd = Application::Instance->commandManager().getCommandByName(name.constData());
    if (!cmd)
        return;
    cmd->initAction();
    auto group = qobject_cast<ActionGroup*>(cmd->getAction());
    if (!group)
        return;
    lineEdit->hidePopups();
    {
        Base::StateLocker guard(menuRunning);
        QMenu menu(this);
        group->populateMenu(&menu);
        setupMenuStyle(&menu);
        menu.exec(QPoint(rect.right(), rect.top()));
    }
    dismiss();
}

void OmniSearchBox::onParamChosen(const ParamInfo *info)
{
    lineEdit->hidePopups();
    paramPanel->setParam(info);
    showPanel(paramPanel);
    paramPanel->focusEditor();
}

void OmniSearchBox::onEnterPressed()
{
    switch (lineEdit->mode()) {
    case Mode::Chooser:
        break;
    case Mode::Object: {
        ObjectMatch match;
        auto locals = lineEdit->localObjects();
        if (!lineEdit->owner()
                || !resolveObject(lineEdit->currentInput().query, lineEdit->owner(), match, &locals))
            break;
        if (match.prop)
            onObjectActivated(match);
        else
            selectObject(match);
        break;
    }
    case Mode::Command:
        if (lineEdit->filteredRowCount() == 1) {
            auto index = lineEdit->filteredRow(0);
            if (index.data(IsActiveRole).toBool())
                onCommandChosen(index.data(CommandListModel::CommandNameRole).toByteArray());
        }
        break;
    case Mode::Param:
        if (lineEdit->filteredRowCount() == 1)
            onParamChosen(ParamListModel::infoOf(lineEdit->filteredRow(0)));
        break;
    }
}

#include "OmniSearchBox.moc"
#include "moc_OmniSearchBox.cpp"
