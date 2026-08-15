// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 The FreeCAD Project Association AISBL               *
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
#include <algorithm>
#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
#include <QTextLayout>
#include <QWidget>
#include <QStackedWidget>
#endif

#include "StartView.h"
#include "FileCardDelegate.h"
#include "FileCardView.h"
#include "FirstStartWidget.h"
#include "FlowLayout.h"
#include "Gui/Workbench.h"
#include <Gui/Document.h>
#include <App/DocumentObject.h>
#include <App/Application.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>

using namespace StartGui;

TYPESYSTEM_SOURCE_ABSTRACT(StartGui::StartView, Gui::MDIView)  // NOLINT

namespace
{

struct NewButton
{
    QString heading;
    QString description;
    QString iconPath;
};

// Formats several modules claim, so the user gets asked which one to import
// with rather than silently getting whichever registered first.
bool wantsImportChooser(const QString& extension)
{
    static const QStringList extensions {QStringLiteral("fcstd"),
                                         QStringLiteral("stp"),
                                         QStringLiteral("step"),
                                         QStringLiteral("iges"),
                                         QStringLiteral("igs")};
    return extensions.contains(extension);
}

// An image is not a document to open but something to place into one, so it
// takes a different route entirely -- see fileCardSelected().
bool isImage(const QString& extension)
{
    static const QStringList extensions {QStringLiteral("bmp"),
                                         QStringLiteral("cur"),
                                         QStringLiteral("gif"),
                                         QStringLiteral("ico"),
                                         QStringLiteral("pbm"),
                                         QStringLiteral("pgm"),
                                         QStringLiteral("png"),
                                         QStringLiteral("jpg"),
                                         QStringLiteral("jpeg"),
                                         QStringLiteral("ppm"),
                                         QStringLiteral("svg"),
                                         QStringLiteral("svgz"),
                                         QStringLiteral("xbm"),
                                         QStringLiteral("xpm")};
    return extensions.contains(extension);
}

/*!
 * \brief A wrapping label that never grows past \a maxLines, eliding instead.
 *
 * The alternative -- letting the text have all the lines it wants -- makes one
 * long description raise the whole row of cards, so a single wordy translation
 * costs every card vertical space. Capping keeps the grid regular and pays for
 * it in the one place the text is actually too long. Nothing is lost: whenever
 * the label elides, the full text becomes its tooltip.
 *
 * QLabel cannot do this itself. Qt::ElideRight is a QTextEdit/QTextLayout
 * facility, and QLabel offers no eliding at all, so the last visible line is
 * measured and shortened by hand.
 */
class DescriptionLabel: public QLabel
{
public:
    DescriptionLabel(const QString& text, int maxLines, QWidget* parent)
        : QLabel(parent)
        , _full(text)
        , _maxLines(std::max(1, maxLines))
    {
        setWordWrap(true);
        QLabel::setText(text);
    }

    // Both size questions are capped rather than answered honestly: the point
    // is that the layout above must never be asked for room beyond the cap.
    QSize sizeHint() const override
    {
        QSize hint = QLabel::sizeHint();
        hint.setHeight(std::min(hint.height(), capHeight()));
        return hint;
    }

    int heightForWidth(int width) const override
    {
        return std::min(QLabel::heightForWidth(width), capHeight());
    }

    QSize minimumSizeHint() const override
    {
        // Without this the label demands enough width for its longest word and
        // drags the card wider. The card owns the width; the text fits into it.
        QSize hint = QLabel::minimumSizeHint();
        hint.setHeight(std::min(hint.height(), capHeight()));
        hint.setWidth(0);
        return hint;
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        applyElision();
    }

private:
    int capHeight() const
    {
        return fontMetrics().lineSpacing() * _maxLines;
    }

    /*!
     * The width the text really gets, which is not width().
     *
     * A QLabel draws inside its contents rect, less its margin, less an
     * indent that defaults to "derive one from the frame". Eliding against
     * the full widget width instead of this overshot by the few pixels a
     * themed border costs, and the text that was supposed to fit on the last
     * line wrapped onto one more -- which is the very thing being prevented.
     * Mirrors what QLabel does to lay the text out in the first place.
     */
    int usableWidth() const
    {
        QRect area = contentsRect();
        const int margins = margin();
        area.adjust(margins, margins, -margins, -margins);

        int textIndent = indent();
        if (textIndent < 0 && frameWidth() > 0) {
            textIndent = fontMetrics().horizontalAdvance(QLatin1Char('x')) / 2 - margins;
        }
        if (textIndent > 0) {
            const Qt::Alignment align =
                QStyle::visualAlignment(layoutDirection(), alignment());
            if (align & Qt::AlignLeft) {
                area.setLeft(area.left() + textIndent);
            }
            if (align & Qt::AlignRight) {
                area.setRight(area.right() - textIndent);
            }
        }
        return area.width();
    }

    /*!
     * Lay the full text out at the current width and, if it runs past the
     * last allowed line, replace that line's worth of text with an elided
     * version of everything still to come.
     */
    void applyElision()
    {
        const int usable = usableWidth();
        if (usable <= 0) {
            return;
        }

        QString shown = _full;
        QTextLayout layout(_full, font());
        layout.beginLayout();
        for (int line = 1;; ++line) {
            QTextLine current = layout.createLine();
            if (!current.isValid()) {
                break;  // the whole text fitted
            }
            current.setLineWidth(usable);
            if (line < _maxLines) {
                continue;
            }
            // On the last allowed line: anything the layout would still put
            // after it is overflow, so fold the remainder into this line.
            if (layout.createLine().isValid()) {
                shown = _full.left(current.textStart())
                    + fontMetrics().elidedText(_full.mid(current.textStart()),
                                               Qt::ElideRight,
                                               usable);
            }
            break;
        }
        layout.endLayout();

        // Guard against re-entry: setText() re-lays out, which resizes, which
        // lands back here. Only an actual change may go through.
        if (QLabel::text() != shown) {
            QLabel::setText(shown);
        }
        setToolTip(shown == _full ? QString() : _full);
    }

    QString _full;
    int _maxLines;
};

/*!
 * \brief A push button that is as big as what is inside it.
 *
 * QPushButton::sizeHint() is computed from the button's own text and icon and
 * never consults a layout set on it, so a card built out of child widgets
 * reported 38x21 no matter what it contained. What kept the cards on screen at
 * all was setMinimumHeight(), which is a floor and not a fit: the moment a
 * description wrapped to one more line than that floor allowed, the extra line
 * was simply cut off. Handing the three size questions to the layout makes the
 * card as big as what it holds.
 *
 * DescriptionLabel is what keeps that from turning into unbounded growth, so
 * in practice this no longer moves for long text. It still earns its place:
 * a larger UI font or icon size used to clip the card just as silently.
 */
class CardButton: public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override
    {
        QLayout* content = layout();
        if (!content) {
            return QPushButton::sizeHint();
        }

        QSize hint = content->totalSizeHint();
        // Settle the width first, because the height of wrapped text depends
        // on it -- and settle it against the widget's own bounds, so a caller
        // that fixed the width gets that width and the text is made to fit it
        // rather than the other way round. Asking the layout for the height
        // rather than reading the label's sizeHint matters: a wrapped QLabel
        // reports a height for whatever width it happens to have at the time,
        // which during the first pass is not yet the final one.
        hint.setWidth(std::clamp(hint.width(), minimumWidth(), maximumWidth()));
        if (content->hasHeightForWidth()) {
            hint.setHeight(std::max(hint.height(), content->heightForWidth(hint.width())));
        }
        return hint.expandedTo(minimumSize()).boundedTo(maximumSize());
    }

    QSize minimumSizeHint() const override
    {
        QLayout* content = layout();
        return content ? content->totalMinimumSize() : QPushButton::minimumSizeHint();
    }

    bool hasHeightForWidth() const override
    {
        QLayout* content = layout();
        return content && content->hasHeightForWidth();
    }

    int heightForWidth(int width) const override
    {
        QLayout* content = layout();
        return content ? content->heightForWidth(width) : QPushButton::heightForWidth(width);
    }
};

QPushButton* createNewButton(const NewButton& newButton)
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    const auto cardSpacing = static_cast<int>(hGrp->GetInt("FileCardSpacing", 25));       // NOLINT
    const auto newFileIconSize = static_cast<int>(hGrp->GetInt("NewFileIconSize", 48));   // NOLINT
    const auto cardLabelWith = static_cast<int>(hGrp->GetInt("FileCardLabelWith", 180));  // NOLINT
    // How many lines a description may wrap to before it is elided. Two is
    // what the card's height budget (icon + spacing) has always allowed.
    const auto descriptionLines =
        static_cast<int>(hGrp->GetInt("FileCardDescriptionLines", 2));  // NOLINT

    auto button = new CardButton();
    // Named so applyFileCardStyle() can find the six of them again when the
    // theme changes; nothing else keeps a handle on them.
    button->setObjectName(QLatin1String("newFileCard"));
    auto mainLayout = new QHBoxLayout(button);
    auto iconLabel = new QLabel(button);
    mainLayout->addWidget(iconLabel);
    QIcon baseIcon(newButton.iconPath);
    iconLabel->setPixmap(baseIcon.pixmap(newFileIconSize, newFileIconSize));

    auto textLayout = new QVBoxLayout;
    auto textLabelLine1 = new QLabel(button);
    textLabelLine1->setText(newButton.heading);
    textLabelLine1->setStyleSheet(QLatin1String("font-weight: bold;"));
    auto textLabelLine2 = new DescriptionLabel(newButton.description, descriptionLines, button);
    textLayout->addWidget(textLabelLine1);
    textLayout->addWidget(textLabelLine2);
    textLayout->setSpacing(0);
    // Stretch factor, where there used to be a trailing addStretch(): the
    // spare width belongs to the text, not to an empty gap after it. With the
    // gap taking it, the text column was only ever as wide as the heading --
    // so "Open File", the shortest heading of the six, gave its much longer
    // description the narrowest column of all and made it wrap to three lines
    // while the card sat in a half-empty row.
    mainLayout->addLayout(textLayout, 1);

    button->setMinimumHeight(newFileIconSize + cardSpacing);
    // Fixed, not minimum: the cards form a grid, and a grid of cards that are
    // each as wide as their own longest sentence is not one. The width is the
    // budget the text has to live within -- it wraps into it, and elides if it
    // still will not fit.
    button->setFixedWidth(newFileIconSize + cardLabelWith);
    return button;
}

/*!
 * \brief Give every card the height of the tallest one.
 *
 * FlowLayout hands each card exactly its own size hint, so any card that
 * wanted a pixel more than its neighbours would stand out of the row.
 * Descriptions can no longer cause that, but headings, icons and fonts still
 * can, and a grid of cards is only tidy if the cards match.
 */
void equalizeCardHeights(const QList<QPushButton*>& cards)
{
    int tallest = 0;
    for (auto* card : cards) {
        tallest = std::max(tallest, card->sizeHint().height());
    }
    for (auto* card : cards) {
        card->setMinimumHeight(tallest);
    }
}

}  // namespace

StartView::StartView(QWidget* parent)
    : Gui::MDIView(nullptr, parent)
    , _contents(new QStackedWidget(parent))
    , _newFileLabel {nullptr}
    , _examplesLabel {nullptr}
    , _recentFilesLabel {nullptr}
    , _showOnStartupCheckBox {nullptr}
{
    setObjectName(QLatin1String("StartView"));
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    auto cardSpacing = hGrp->GetInt("FileCardSpacing", 15);  // NOLINT

    // First start page
    auto firstStartScrollArea = new QScrollArea();
    auto firstStartScrollWidget = new QWidget(firstStartScrollArea);
    firstStartScrollArea->setWidget(firstStartScrollWidget);
    firstStartScrollArea->setWidgetResizable(true);

    auto firstStartRegion = new QHBoxLayout(firstStartScrollWidget);
    firstStartRegion->addStretch();
    auto firstStartWidget = new FirstStartWidget(this);
    connect(firstStartWidget,
            &FirstStartWidget::dismissed,
            this,
            &StartView::firstStartWidgetDismissed);
    firstStartRegion->addWidget(firstStartWidget);
    firstStartRegion->addStretch();
    _contents->addWidget(firstStartScrollArea);

    // Documents page
    auto documentsWidget = new QWidget();
    _contents->addWidget(documentsWidget);
    auto documentsMainLayout = new QVBoxLayout();
    documentsWidget->setLayout(documentsMainLayout);
    auto documentsScrollArea = new QScrollArea();
    documentsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
    documentsMainLayout->addWidget(documentsScrollArea);
    auto documentsScrollWidget = new QWidget(documentsScrollArea);
    documentsScrollArea->setWidget(documentsScrollWidget);
    documentsScrollArea->setWidgetResizable(true);
    auto documentsContentLayout = new QVBoxLayout(documentsScrollWidget);
    documentsContentLayout->setSizeConstraint(QLayout::SizeConstraint::SetMinAndMaxSize);

    _newFileLabel = new QLabel();
    documentsContentLayout->addWidget(_newFileLabel);

    auto createNewRow = new QWidget;
    auto flowLayout = new FlowLayout;

    // Reset margins of layout to provide consistent spacing
    flowLayout->setContentsMargins({});

    // This allows new file widgets to be targeted via QSS
    createNewRow->setObjectName(QStringLiteral("CreateNewRow"));
    createNewRow->setLayout(flowLayout);

    documentsContentLayout->addWidget(createNewRow);
    configureNewFileButtons(flowLayout);

    _recentFilesLabel = new QLabel();
    documentsContentLayout->addWidget(_recentFilesLabel);
    auto recentFilesListWidget = new FileCardView(_contents);
    connect(recentFilesListWidget, &QListView::clicked, this, &StartView::fileCardSelected);
    documentsContentLayout->addWidget(recentFilesListWidget);

    _examplesLabel = new QLabel();
    documentsContentLayout->addWidget(_examplesLabel);
    auto examplesListWidget = new FileCardView(_contents);
    connect(examplesListWidget, &QListView::clicked, this, &StartView::fileCardSelected);
    documentsContentLayout->addWidget(examplesListWidget);

    documentsContentLayout->setSpacing(static_cast<int>(cardSpacing));
    documentsContentLayout->addStretch();

    // Documents page footer
    auto footerLayout = new QHBoxLayout();
    documentsMainLayout->addLayout(footerLayout);

    _openFirstStart = new QPushButton();
    _openFirstStart->setIcon(QIcon(QLatin1String(":/icons/preferences-general.svg")));
    connect(_openFirstStart, &QPushButton::clicked, this, &StartView::openFirstStartClicked);

    _showOnStartupCheckBox = new QCheckBox();
    bool showOnStartup = hGrp->GetBool("ShowOnStartup", true);
    _showOnStartupCheckBox->setCheckState(showOnStartup ? Qt::CheckState::Unchecked
                                                        : Qt::CheckState::Checked);
    connect(_showOnStartupCheckBox, &QCheckBox::toggled, this, &StartView::showOnStartupChanged);

    footerLayout->addWidget(_openFirstStart);
    footerLayout->addStretch();
    footerLayout->addWidget(_showOnStartupCheckBox);

    setCentralWidget(_contents);

    // Set startup widget according to the first start parameter
    auto firstStart = hGrp->GetBool("FirstStart2024", true);  // NOLINT
    _contents->setCurrentWidget(firstStart ? firstStartScrollArea : documentsWidget);

    configureExamplesListWidget(examplesListWidget);
    configureRecentFilesListWidget(recentFilesListWidget);

    retranslateUi();
}

void StartView::configureNewFileButtons(QLayout* layout) const
{
    auto newEmptyFile = createNewButton({tr("Empty file"),
                                         tr("Create a new empty FreeCAD file"),
                                         QLatin1String(":/icons/document-new.svg")});
    auto openFile = createNewButton({tr("Open File"),
                                     tr("Open an existing CAD file or 3D model"),
                                     QLatin1String(":/icons/document-open.svg")});
    auto partDesign = createNewButton({tr("Parametric Part"),
                                       tr("Create a part with the Part Design workbench"),
                                       QLatin1String(":/icons/PartDesignWorkbench.svg")});
    auto assembly = createNewButton({tr("Assembly"),
                                     tr("Create an assembly project"),
                                     QLatin1String(":/icons/AssemblyWorkbench.svg")});
    auto draft = createNewButton({tr("2D Draft"),
                                  tr("Create a 2D Draft with the Draft workbench"),
                                  QLatin1String(":/icons/DraftWorkbench.svg")});
    auto arch = createNewButton({tr("BIM/Architecture"),
                                 tr("Create an architectural project"),
                                 QLatin1String(":/icons/BIMWorkbench.svg")});

    equalizeCardHeights({partDesign, assembly, draft, arch, newEmptyFile, openFile});

    // TODO: Ensure all of the required WBs are actually available
    layout->addWidget(partDesign);
    layout->addWidget(assembly);
    layout->addWidget(draft);
    layout->addWidget(arch);
    layout->addWidget(newEmptyFile);
    layout->addWidget(openFile);

    connect(newEmptyFile, &QPushButton::clicked, this, &StartView::newEmptyFile);
    connect(openFile, &QPushButton::clicked, this, &StartView::openExistingFile);
    connect(partDesign, &QPushButton::clicked, this, &StartView::newPartDesignFile);
    connect(assembly, &QPushButton::clicked, this, &StartView::newAssemblyFile);
    connect(draft, &QPushButton::clicked, this, &StartView::newDraftFile);
    connect(arch, &QPushButton::clicked, this, &StartView::newArchFile);

    applyFileCardStyle();
}

/*!
 * \brief Give the six New File cards their own style sheet, or take it away.
 *
 * fileCardStyle() yields nothing while a theme style sheet is loaded, so that
 * the cards are painted as the theme paints a QPushButton. Deciding that once,
 * at construction, was not enough: the Start page is built before the theme
 * sheet is applied, so the cards kept the light #DDDDDD fallback for the rest
 * of the session and stood out as near-white panels on a dark theme. Re-run it
 * whenever the style changes.
 */
void StartView::applyFileCardStyle() const
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    if (!hGrp->GetBool("FileCardUseStyleSheet", true)) {
        return;
    }

    const QString style = fileCardStyle();
    for (auto* card : findChildren<QPushButton*>(QLatin1String("newFileCard"))) {
        // Only when it differs: setStyleSheet() repolishes unconditionally,
        // and this runs from a style-change notification.
        if (card->styleSheet() != style) {
            card->setStyleSheet(style);
        }
    }
}

QString StartView::fileCardStyle() const
{
    if (!qApp->styleSheet().isEmpty()) {
        return {};
    }

    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");

    auto getUserColor = [&hGrp](QColor color, const char* parameter) {
        uint32_t packed = App::Color::asPackedRGB<QColor>(color);
        packed = hGrp->GetUnsigned(parameter, packed);
        color = App::Color::fromPackedRGB<QColor>(packed);
        return color;
    };

    QColor background(221, 221, 221);  // NOLINT
    background = getUserColor(background, "FileCardBackgroundColor");

    QColor hovered(98, 160, 234);  // NOLINT
    hovered = getUserColor(hovered, "FileCardBorderColor");

    QColor pressed(38, 162, 105);  // NOLINT
    pressed = getUserColor(pressed, "FileCardSelectionColor");

    return QString::fromLatin1("QPushButton {"
                               " background-color: rgb(%1, %2, %3);"
                               " border-radius: 8px;"
                               "}"
                               "QPushButton:hover {"
                               " border: 2px solid rgb(%4, %5, %6);"
                               "}"
                               "QPushButton:pressed {"
                               " border: 2px solid rgb(%7, %8, %9);"
                               "}")
        .arg(background.red())
        .arg(background.green())
        .arg(background.blue())
        .arg(hovered.red())
        .arg(hovered.green())
        .arg(hovered.blue())
        .arg(pressed.red())
        .arg(pressed.green())
        .arg(pressed.blue());
}

void StartView::configureFileCardWidget(QListView* fileCardWidget)
{
    auto delegate = new FileCardDelegate(fileCardWidget);
    fileCardWidget->setItemDelegate(delegate);
    fileCardWidget->setMinimumWidth(fileCardWidget->parentWidget()->width());
    //    fileCardWidget->setGridSize(
    //        fileCardWidget->itemDelegate()->sizeHint(QStyleOptionViewItem(),
    //                                                 fileCardWidget->model()->index(0, 0)));
}


void StartView::configureRecentFilesListWidget(QListView* recentFilesListWidget)
{
    _recentFilesListWidget = recentFilesListWidget;
    _recentFilesModel.loadRecentFiles();
    recentFilesListWidget->setModel(&_recentFilesModel);
    configureFileCardWidget(recentFilesListWidget);

    // The model reloads itself when the MRU list changes, so the section has to follow it
    // rather than be sized once - a first-ever save has to bring it into view.
    connect(&_recentFilesModel,
            &QAbstractItemModel::modelReset,
            this,
            &StartView::updateRecentFilesVisibility);
    updateRecentFilesVisibility();
}

void StartView::updateRecentFilesVisibility()
{
    bool haveFiles = _recentFilesModel.rowCount() > 0;
    if (_recentFilesListWidget) {
        _recentFilesListWidget->setVisible(haveFiles);
    }
    if (_recentFilesLabel) {
        _recentFilesLabel->setVisible(haveFiles);
    }
}


void StartView::configureExamplesListWidget(QListView* examplesListWidget)
{
    _examplesModel.loadExamples();
    examplesListWidget->setModel(&_examplesModel);
    configureFileCardWidget(examplesListWidget);
}


void StartView::newEmptyFile() const
{
    Gui::Application::Instance->commandManager().runCommandByName("Std_New");
    postStart(PostStartBehavior::switchWorkbench);
}

void StartView::newPartDesignFile() const
{
    Gui::Application::Instance->commandManager().runCommandByName("Std_New");
    Gui::Application::Instance->activateWorkbench("PartDesignWorkbench");
    Gui::Application::Instance->commandManager().runCommandByName("PartDesign_Body");
    postStart(PostStartBehavior::doNotSwitchWorkbench);
}

void StartView::openExistingFile() const
{
    auto originalDocument = Gui::Application::Instance->activeDocument();
    Gui::Application::Instance->commandManager().runCommandByName("Std_Open");
    if (Gui::Application::Instance->activeDocument() != originalDocument) {
        // Only run this if the user chose a new document to open (that is, they didn't cancel the
        // open file dialog)
        postStart(PostStartBehavior::switchWorkbench);
    }
}

void StartView::newAssemblyFile() const
{
    Gui::Application::Instance->commandManager().runCommandByName("Std_New");
    Gui::Application::Instance->activateWorkbench("AssemblyWorkbench");
    Gui::Application::Instance->commandManager().runCommandByName("Assembly_CreateAssembly");
    Gui::Application::Instance->commandManager().runCommandByName("Std_Refresh");
    postStart(PostStartBehavior::doNotSwitchWorkbench);
}

void StartView::newDraftFile() const
{
    Gui::Application::Instance->commandManager().runCommandByName("Std_New");
    Gui::Application::Instance->activateWorkbench("DraftWorkbench");
    Gui::Application::Instance->commandManager().runCommandByName("Std_ViewTop");
    postStart(PostStartBehavior::doNotSwitchWorkbench);
}

void StartView::newArchFile() const
{
    Gui::Application::Instance->commandManager().runCommandByName("Std_New");
    try {
        Gui::Application::Instance->activateWorkbench("BIMWorkbench");
    }
    catch (...) {
        Gui::Application::Instance->activateWorkbench("ArchWorkbench");
    }
    postStart(PostStartBehavior::doNotSwitchWorkbench);
}

bool StartView::onHasMsg(const char* pMsg) const
{
    if (strcmp("AllowsOverlayOnHover", pMsg) == 0) {
        return false;
    }

    return MDIView::onHasMsg(pMsg);
}

void StartView::postStart(PostStartBehavior behavior) const
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");

    if (behavior == PostStartBehavior::switchWorkbench) {
        auto wb = hGrp->GetASCII("AutoloadModule", "");
        if (wb == "$LastModule") {
            wb = App::GetApplication()
                     .GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")
                     ->GetASCII("LastModule", "");
        }
        if (!wb.empty()) {
            Gui::Application::Instance->activateWorkbench(wb.c_str());
        }
    }
    auto closeStart = hGrp->GetBool("closeStart", false);
    if (closeStart) {
        this->window()->close();
    }
}


void StartView::fileCardSelected(const QModelIndex& index)
{
    auto file = index.data(static_cast<int>(Start::DisplayedFilesModelRoles::path)).toString();
    const std::string path = Base::Tools::pythonLiteral(file);
    const QString extension = QFileInfo(file).suffix().toLower();

    // Which module imports a given extension is a user preference, written by
    // the import dialog as DefaultImport<ext>. Passing it on is what makes that
    // choice stick; leaving it empty takes whichever module registered first.
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    const std::string module = Base::Tools::pythonLiteral(QString::fromStdString(
        hGrp->GetASCII(("DefaultImport" + extension.toStdString()).c_str(), "")));

    std::string command;
    if (isImage(extension)) {
        // An image has no document of its own to open. The old web start page
        // made one and inserted the image into it, and loadFile cannot: it
        // would hand the file to a module's insert() with no document to
        // insert into.
        command = "FreeCAD.newDocument()\n"
                  "FreeCADGui.insert("
            + path
            + ", FreeCAD.activeDocument().Name)\n"
              "FreeCAD.activeDocument().recompute()\n"
              "FreeCADGui.activeDocument().sendMsgToViews('ViewFit')\n";
    }
    else {
        // FreeCADGui.loadFile, not FreeCAD.loadFile: the App-level one goes
        // straight to <module>.openDocument(), which throws when that document
        // is already open -- and a card for an open document is exactly what a
        // user clicks by mistake. Gui::Application::open() looks for a document
        // already holding this file path and reloads it instead (cc2f2151d5,
        // which is why the old web start page's LoadMRU.py called the Gui one).
        // It also does the rest of what opening from the UI means: dropping the
        // empty untouched startup document, adding the file to the recent list,
        // moving the file dialog's working directory, and fitting the view for
        // an imported, non-FCStd file.
        command = "FreeCADGui.loadFile(" + path + ", " + module
            + (wantsImportChooser(extension) ? ", interactive=True" : "") + ")";
    }
    try {
        Base::Interpreter().runString(command.c_str());
        postStart(PostStartBehavior::doNotSwitchWorkbench);
    }
    catch (Base::PyException& e) {
        Base::Console().Error(e.getMessage().c_str());
    }
    catch (Base::Exception& e) {
        Base::Console().Error(e.getMessage().c_str());
    }
    catch (...) {
        Base::Console().Error("An unknown error occurred");
    }
}

void StartView::showOnStartupChanged(bool checked)
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    hGrp->SetBool(
        "ShowOnStartup",
        !checked);  // The sense of this option has been reversed: the checkbox actually says
                    // "*Don't* show on startup" now, but the option is preserved in its
                    // original sense, so is stored inverted.
}

void StartView::openFirstStartClicked()
{
    _contents->setCurrentIndex(0);
}

void StartView::firstStartWidgetDismissed()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Start");
    hGrp->SetBool("FirstStart2024", false);
    _contents->setCurrentIndex(1);
}

void StartView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        this->retranslateUi();
    }
    else if (event->type() == QEvent::StyleChange) {
        // A theme was loaded or cleared: the cards' own sheet has to be
        // reconsidered against it.
        this->applyFileCardStyle();
    }
    Gui::MDIView::changeEvent(event);
}

void StartView::retranslateUi()
{
    QString title = QCoreApplication::translate("Workbench", "Start");
    setWindowTitle(title);

    const QLatin1String h1Start("<h1>");
    const QLatin1String h1End("</h1>");

    _newFileLabel->setText(h1Start + tr("New File") + h1End);
    _examplesLabel->setText(h1Start + tr("Examples") + h1End);
    _recentFilesLabel->setText(h1Start + tr("Recent Files") + h1End);

    QString application = QString::fromUtf8(App::Application::Config()["ExeName"].c_str());
    _openFirstStart->setText(tr("Open first start setup"));
    _showOnStartupCheckBox->setText(
        tr("Don't show this Start page again (start with blank screen)"));
}
