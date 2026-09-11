// SPDX-License-Identifier: LGPL-2.1-or-later
/* The host widget layer (docs/Sandbox.md 7.12, H0): the property core,
 * the class set, the layouts, the .ui generator's output, and the Qt
 * backend binding uic's widgets to the models.  Needs a QApplication
 * (the widgets) and the App (Gui::InputField reads preferences); no
 * main window, no Gui::Application. */

#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QAction>
#include <QBoxLayout>
#include <QMenu>
#include <QToolBar>
#include <QWidgetAction>
#include <QTest>

#include <QLabel>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Expression.h>
#include <App/ExpressionParser.h>
#include <App/Placement.h>
#include <App/PropertyUnits.h>
#include <src/App/InitApplication.h>

#include "Gui/Camera.h"
#include "Gui/FileDialog.h"
#include "Gui/Fw/FwImage.h"
#include "Gui/Fw/FwPanelMirror.h"
#include "Gui/Fw/FwQtView.h"
#include "Gui/Fw/FwStore.h"
#include "Gui/SceneControl.h"
#include "Gui/SceneWidgets.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "Gui/Fw/FwWidgets.h"
#include "Gui/InputField.h"
#include "Gui/QuantitySpinBox.h"
#include "Gui/TaskView/TaskOrientation.h"
#include "Gui/TaskView/TaskView.h"
#include "Gui/UiLoader.h"
#include "Gui/WidgetFactory.h"
#include <QComboBox>
#include <QFile>
#include <QPainter>
#include <QPaintEvent>
#include <QStackedWidget>
#include "fwui_TaskPanel_OrthoArray.h"

namespace Fw = Gui::Fw;

/// A custom-painted leaf (docs/Sandbox.md 7.19 M2): no model of its own,
/// nothing inside, what it paints is what a client can get
class PaintedLeaf: public QWidget
{
    Q_OBJECT
public:
    explicit PaintedLeaf(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("painted"));
        setMinimumSize(40, 30);
    }
    QColor color = Qt::red;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), color);
    }
};

// NOLINTBEGIN(readability-magic-numbers)
class testFormWidgets: public QObject
{
    Q_OBJECT
public:
    testFormWidgets()
    {
        tests::initApplication();
    }

private Q_SLOTS:
    void test_bagDefaultsTouchedAndSignals()
    {
        Fw::QSpinBox spin;
        QCOMPARE(spin.qtClass(), QStringLiteral("QSpinBox"));
        QCOMPARE(spin.modelName(), QStringLiteral("QSpinBoxModel"));
        QCOMPARE(spin.value(), 0);
        QCOMPARE(spin.minimum(), 0);
        QCOMPARE(spin.maximum(), 99);
        QVERIFY(spin.touched().isEmpty());
        QVERIFY(spin.has(QStringLiteral("value")));
        QVERIFY(!spin.has(QStringLiteral("nonsense")));

        QSignalSpy changed(&spin, &Fw::Widget::propertiesChanged);
        QSignalSpy valueChanged(&spin, &Fw::QSpinBox::valueChanged);
        spin.setValue(5);
        QCOMPARE(spin.value(), 5);
        QVERIFY(spin.isTouched(QStringLiteral("value")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toStringList(), QStringList {QStringLiteral("value")});
        QCOMPARE(changed.at(0).at(1).toInt(), static_cast<int>(Fw::Source::Native));
        QCOMPARE(valueChanged.count(), 1);
        QCOMPARE(valueChanged.at(0).at(0).toInt(), 5);

        // an equal write is still a write (the backend re-applies), but
        // not a change (Qt fires no valueChanged)
        spin.setValue(5);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(valueChanged.count(), 1);

        // the range clamps, as Qt does
        spin.setValue(500);
        QCOMPARE(spin.value(), 99);
        spin.setRange(10, 20);
        QCOMPARE(spin.value(), 20);

        // a silent write: no signal, no touched mark
        Fw::QSpinBox other;
        QSignalSpy otherChanged(&other, &Fw::Widget::propertiesChanged);
        other.setInitial(QStringLiteral("value"), 7);
        QCOMPARE(other.value(), 7);
        QCOMPARE(otherChanged.count(), 0);
        QVERIFY(!other.isTouched(QStringLiteral("value")));

        // an unknown name is a dynamic property, as Qt's setProperty
        QVERIFY(!other.setProperty("rawValue", 3.0));
        QCOMPARE(other.property("rawValue").toDouble(), 3.0);
        QCOMPARE(other.dynamicPropertyNames(), QStringList {QStringLiteral("rawValue")});
    }

    void test_coercionAndBackendSource()
    {
        Fw::QSpinBox spin;
        // JSON brings doubles for ints; the bag keeps the declared type
        QVariantMap m;
        m.insert(QStringLiteral("value"), 3.0);
        spin.setProperties(m, Fw::Source::Guest);
        QCOMPARE(spin.property("value").typeId(), static_cast<int>(QMetaType::Int));
        QCOMPARE(spin.value(), 3);
        QVERIFY(!spin.isTouched(QStringLiteral("value")));  // the guest syncs its own list
        spin.setTouched(QStringList {QStringLiteral("value")});
        QVERIFY(spin.isTouched(QStringLiteral("value")));

        // a backend write marks nothing touched
        Fw::QLineEdit edit;
        QSignalSpy textChanged(&edit, &Fw::QLineEdit::textChanged);
        edit.setProperties(QVariantMap {{QStringLiteral("text"), QStringLiteral("abc")}},
                           Fw::Source::Backend);
        QCOMPARE(edit.text(), QStringLiteral("abc"));
        QVERIFY(!edit.isTouched(QStringLiteral("text")));
        QCOMPARE(textChanged.count(), 1);
    }

    void test_radioExclusivityAndEvents()
    {
        Fw::Widget box;
        auto a = new Fw::QRadioButton(QStringLiteral("a"), &box);
        auto b = new Fw::QRadioButton(QStringLiteral("b"), &box);
        QVERIFY(a->isCheckable());
        QVERIFY(a->autoExclusive());
        QSignalSpy aToggled(a, &Fw::QAbstractButton::toggled);
        a->setChecked(true);
        b->setChecked(true);
        QVERIFY(!a->isChecked());
        QVERIFY(b->isChecked());
        QCOMPARE(aToggled.count(), 2);
        QCOMPARE(aToggled.at(1).at(0).toBool(), false);

        Fw::QCheckBox check;
        QSignalSpy state(&check, &Fw::QCheckBox::stateChanged);
        QSignalSpy clicked(&check, &Fw::QAbstractButton::clicked);
        QSignalSpy emitted(&check, &Fw::Widget::eventEmitted);
        check.click();
        QVERIFY(check.isChecked());
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.at(0).at(0).toInt(), 2);
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.at(0).at(0).toBool(), true);
        QCOMPARE(emitted.count(), 1);
        QCOMPARE(emitted.at(0).at(0).toString(), QStringLiteral("clicked"));

        // a backend-reported event fires the typed signal
        Fw::QLineEdit edit;
        QSignalSpy finished(&edit, &Fw::QLineEdit::editingFinished);
        edit.notify(QStringLiteral("editingFinished"));
        QCOMPARE(finished.count(), 1);
        // a request goes the other way and touches no state
        QSignalSpy requested(&edit, &Fw::Widget::requested);
        edit.selectAll();
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("selectAll"));
    }

    void test_comboItems()
    {
        Fw::QComboBox combo;
        QSignalSpy index(&combo, &Fw::QComboBox::currentIndexChanged);
        combo.addItems(QStringList {QStringLiteral("a"), QStringLiteral("b")});
        QCOMPARE(combo.count(), 2);
        QCOMPARE(combo.currentIndex(), 0);
        QCOMPARE(combo.currentText(), QStringLiteral("a"));
        QCOMPARE(index.count(), 1);
        combo.insertItem(0, QStringLiteral("z"), 42);
        QCOMPARE(combo.currentIndex(), 1);
        QCOMPARE(combo.currentText(), QStringLiteral("a"));
        QCOMPARE(combo.itemData(0).toInt(), 42);
        QCOMPARE(combo.findData(42), 0);
        combo.setCurrentText(QStringLiteral("b"));
        QCOMPARE(combo.currentIndex(), 2);
        combo.removeItem(2);
        QCOMPARE(combo.currentIndex(), 1);
        QCOMPARE(combo.findText(QStringLiteral("b")), -1);
        combo.clear();
        QCOMPARE(combo.count(), 0);
        QCOMPARE(combo.currentIndex(), -1);
    }

    void test_layoutOpsReachTheOwner()
    {
        Fw::Widget owner;
        auto grid = Fw::createLayout(QStringLiteral("QGridLayout"), &owner);
        grid->setObjectName(QStringLiteral("g"));
        QCOMPARE(owner.layout(), grid);
        QCOMPARE(owner.findLayout(QStringLiteral("g")), grid);
        QSignalSpy ops(&owner, &Fw::Widget::layoutChanged);

        auto child = new Fw::QLabel(QStringLiteral("x"));
        grid->addWidget(child, 0, 1);
        QCOMPARE(child->parentWidget(), &owner);  // the layout's widget adopts it
        QCOMPARE(ops.count(), 1);
        QVariantMap op = ops.at(0).at(0).toMap();
        QCOMPARE(op.value(QStringLiteral("layout")).toString(), QStringLiteral("g"));
        QCOMPARE(op.value(QStringLiteral("op")).toString(), QStringLiteral("addWidget"));
        QCOMPARE(op.value(QStringLiteral("widget")).value<QObject*>(), child);
        QCOMPARE(op.value(QStringLiteral("args")).toList(), QVariantList({0, 1, 1, 1}));
        QCOMPARE(grid->count(), 1);
        QCOMPARE(grid->rowCount(), 1);
        QCOMPARE(grid->columnCount(), 2);

        auto sub = Fw::createLayout(QStringLiteral("QHBoxLayout"));
        sub->setObjectName(QStringLiteral("h"));
        grid->addLayout(sub, 1, 0, 1, 2);
        QCOMPARE(sub->parentWidget(), &owner);
        QCOMPARE(owner.findLayout(QStringLiteral("h")), sub);
        QCOMPARE(ops.count(), 2);
        QCOMPARE(ops.at(1).at(0).toMap().value(QStringLiteral("sublayout")).toString(),
                 QStringLiteral("h"));
        sub->addStretch(1);
        QCOMPARE(ops.count(), 3);  // through the parent layout to the owner

        Fw::LayoutItem taken = grid->takeAt(0);
        QCOMPARE(taken.widget, child);
        QCOMPARE(ops.count(), 4);
        QCOMPARE(ops.at(3).at(0).toMap().value(QStringLiteral("op")).toString(),
                 QStringLiteral("takeAt"));

        // an unnamed layout reaches no backend layout: no op crosses
        Fw::Widget other;
        auto anon = Fw::createLayout(QStringLiteral("QVBoxLayout"), &other);
        QSignalSpy none(&other, &Fw::Widget::layoutChanged);
        anon->addWidget(new Fw::QLabel);
        QCOMPARE(none.count(), 0);
    }

    void test_factory()
    {
        Fw::Widget* w = Fw::createWidget(QStringLiteral("Gui::PrefCheckBox"));
        QVERIFY(qobject_cast<Fw::QCheckBox*>(w));
        QCOMPARE(w->qtClass(), QStringLiteral("Gui::PrefCheckBox"));
        QCOMPARE(w->modelName(), QStringLiteral("QCheckBoxModel"));
        delete w;
        w = Fw::createWidget(QStringLiteral("InputFieldModel"));
        QVERIFY(qobject_cast<Fw::InputField*>(w));
        QCOMPARE(w->qtClass(), QStringLiteral("Gui::InputField"));
        delete w;
        w = Fw::createWidget(QStringLiteral("QTreeWidget"));
        QVERIFY(qobject_cast<Fw::QTreeWidget*>(w));
        QCOMPARE(w->modelName(), QStringLiteral("QTreeWidgetModel"));
        delete w;
        // an unknown class is a plain Widget under that name: the gap shows
        w = Fw::createWidget(QStringLiteral("QCalendarWidget"));
        QCOMPARE(w->modelName(), QStringLiteral("QWidgetModel"));
        QCOMPARE(w->qtClass(), QStringLiteral("QCalendarWidget"));
        delete w;
        QVERIFY(Fw::knownClasses().contains(QStringLiteral("Gui::QuantitySpinBox")));
    }

    void test_generatedForm()
    {
        Ui_DraftOrthoArrayTaskPanel ui;
        Fw::UiForm form;
        ui.setupUi(&form);
        QCOMPARE(form.uiFile(), QStringLiteral(":/ui/TaskPanel_OrthoArray.ui"));
        QCOMPARE(form.objectName(), QStringLiteral("DraftOrthoArrayTaskPanel"));
        QCOMPARE(form.windowTitle(), QStringLiteral("Orthogonal Array"));
        QVERIFY(form.touched().isEmpty());
        QCOMPARE(form.names().size(), 39);  // the file's named widgets (layouts are not)
        QCOMPARE(form.named(QStringLiteral("spinbox_n_X")), ui.spinbox_n_X);
        QCOMPARE(ui.spinbox_n_X->value(), 2);
        QCOMPARE(ui.spinbox_n_X->minimum(), 1);
        QCOMPARE(ui.spinbox_n_X->maximum(), 1000000);
        QVERIFY(ui.spinbox_n_X->touched().isEmpty());
        QCOMPARE(ui.spinbox_n_X->parentWidget(), ui.group_copies);
        QCOMPARE(ui.input_X_x->qtClass(), QStringLiteral("Gui::InputField"));
        QCOMPARE(ui.input_X_x->rawValue(), 100.0);
        QCOMPARE(ui.input_X_x->getUnitText(), QStringLiteral("mm"));
        QVERIFY(ui.radiobutton_x_axis->isChecked());
        QVERIFY(!ui.radiobutton_y_axis->isChecked());
        QVERIFY(ui.checkbox_link->isChecked());
        QVERIFY(ui.button_linear_mode->isCheckable());
        QCOMPARE(ui.label_n_X->text(), QStringLiteral("X"));
        QCOMPARE(form.findChildWidget(QStringLiteral("label_n_Z")), ui.label_n_Z);
        // the file reuses gridLayout_5: two layouts, two members
        QVERIFY(ui.gridLayout_5 != ui.gridLayout_5_2);
        QCOMPARE(ui.gridLayout_5->objectName(), QStringLiteral("gridLayout_5"));
        Fw::Layout* grid = form.findLayout(QStringLiteral("grid_number"));
        QCOMPARE(grid, ui.grid_number);
        QCOMPARE(grid->kind(), Fw::Layout::Grid);
        QCOMPARE(grid->count(), 6);
        QCOMPARE(grid->parentWidget(), ui.group_copies);
        QCOMPARE(grid->itemAt(1)->widget, ui.spinbox_n_X);
    }

    void test_qtViewBindsTheForm()
    {
        Ui_DraftOrthoArrayTaskPanel ui;
        Fw::UiForm form;
        ui.setupUi(&form);
        form.setUiFile(QString::fromUtf8(FW_TEST_UI_FILE));
        // what the dialog set before showing: touched, applied over uic's
        ui.spinbox_n_X->setValue(4);
        ui.label_n_Z->setText(QStringLiteral("Z-count"));

        QWidget* w = Gui::FwQt::realize(&form, nullptr);
        QVERIFY(w);
        QCOMPARE(w->objectName(), QStringLiteral("DraftOrthoArrayTaskPanel"));
        Gui::FwQt::View* view = Gui::FwQt::View::of(&form);
        QVERIFY(view);
        QVERIFY(view->isBound());
        auto nX = w->findChild<QSpinBox*>(QStringLiteral("spinbox_n_X"));
        auto nY = w->findChild<QSpinBox*>(QStringLiteral("spinbox_n_Y"));
        auto xX = w->findChild<Gui::InputField*>(QStringLiteral("input_X_x"));
        auto fuse = w->findChild<QCheckBox*>(QStringLiteral("checkbox_fuse"));
        auto rX = w->findChild<QRadioButton*>(QStringLiteral("radiobutton_x_axis"));
        auto rY = w->findChild<QRadioButton*>(QStringLiteral("radiobutton_y_axis"));
        auto labelZ = w->findChild<QLabel*>(QStringLiteral("label_n_Z"));
        QVERIFY(nX && nY && xX && fuse && rX && rY && labelZ);
        QVERIFY(Gui::FwQt::View::of(ui.spinbox_n_X));
        QCOMPARE(Gui::FwQt::View::of(ui.spinbox_n_X)->widget(), nX);

        // touched values reached uic's widgets; the file's own stayed
        QCOMPARE(nX->value(), 4);
        QCOMPARE(nY->value(), 2);
        QCOMPARE(labelZ->text(), QStringLiteral("Z-count"));
        QVERIFY(rX->isChecked());
        // binding read the widget back into the bag (untouched keys): the
        // bag mirrors the widget, not the file -- uic writes the file's
        // `quantity` double into a Base::Quantity property, which does
        // not take, so the real InputField shows 0 and so does the bag
        QCOMPARE(ui.spinbox_n_Y->value(), 2);
        QCOMPARE(ui.input_X_x->rawValue(), xX->rawValue());
        QCOMPARE(ui.checkbox_fuse->isChecked(), fuse->isChecked());
        QCOMPARE(ui.label_n_X->text(), QStringLiteral("X"));

        // model -> widget
        ui.spinbox_n_Y->setValue(9);
        QCOMPARE(nY->value(), 9);
        ui.input_X_x->setValue(33.0);
        QCOMPARE(xX->rawValue(), 33.0);
        ui.group_Z->hide();
        QVERIFY(w->findChild<QGroupBox*>(QStringLiteral("group_Z"))->isHidden());

        // widget -> model: the bag, the typed signal, no echo
        QSignalSpy nYChanged(ui.spinbox_n_Y, &Fw::QSpinBox::valueChanged);
        QSignalSpy nYProps(ui.spinbox_n_Y, &Fw::Widget::propertiesChanged);
        nY->setValue(7);
        QCOMPARE(ui.spinbox_n_Y->value(), 7);
        QCOMPARE(nYChanged.count(), 1);
        QCOMPARE(nYChanged.at(0).at(0).toInt(), 7);
        QCOMPARE(nYProps.count(), 1);
        QCOMPARE(nYProps.at(0).at(1).toInt(), static_cast<int>(Fw::Source::Backend));
        QCOMPARE(nY->value(), 7);

        xX->setProperty("rawValue", 55.0);
        QCOMPARE(ui.input_X_x->rawValue(), 55.0);
        QCOMPARE(ui.input_X_x->text(), xX->text());

        QSignalSpy fuseClicked(ui.checkbox_fuse, &Fw::QAbstractButton::clicked);
        QSignalSpy fuseState(ui.checkbox_fuse, &Fw::QCheckBox::stateChanged);
        bool was = fuse->isChecked();
        fuse->click();
        QCOMPARE(ui.checkbox_fuse->isChecked(), !was);
        QCOMPARE(fuseClicked.count(), 1);
        QCOMPARE(fuseState.count(), 1);

        rY->click();
        QVERIFY(ui.radiobutton_y_axis->isChecked());
        QVERIFY(!ui.radiobutton_x_axis->isChecked());  // Qt's exclusivity, mirrored

        // a layout op after realization reaches the real layout
        auto qgrid = w->findChild<QGridLayout*>(QStringLiteral("grid_number"));
        QVERIFY(qgrid);
        int before = qgrid->count();
        ui.grid_number->takeAt(0);
        QCOMPARE(qgrid->count(), before - 1);

        // a request
        ui.input_X_x->selectAll();
        QVERIFY(xX->hasSelectedText());

        // the dialog deletes its content: the views detach
        delete w;
        QCoreApplication::processEvents();
        QVERIFY(!Gui::FwQt::View::of(&form));
        QVERIFY(!Gui::FwQt::View::of(ui.spinbox_n_X));
        // the models live on
        QCOMPARE(ui.spinbox_n_Y->value(), 7);
    }

    void test_qtViewBuildsAWidget()
    {
        Fw::QPushButton button(QStringLiteral("Go"));
        button.setObjectName(QStringLiteral("go"));
        button.setToolTip(QStringLiteral("tip"));
        QWidget* w = Gui::FwQt::realize(&button, nullptr);
        QVERIFY(w);
        auto qb = qobject_cast<QAbstractButton*>(w);
        QVERIFY(qb);
        QCOMPARE(qb->text(), QStringLiteral("Go"));
        QCOMPARE(qb->objectName(), QStringLiteral("go"));
        QCOMPARE(qb->toolTip(), QStringLiteral("tip"));
        QVERIFY(!Gui::FwQt::View::of(&button)->isBound());
        QSignalSpy clicked(&button, &Fw::QAbstractButton::clicked);
        qb->click();
        QCOMPARE(clicked.count(), 1);
        Gui::FwQt::View::of(&button)->release(true);
        QCoreApplication::processEvents();
        QVERIFY(!Gui::FwQt::View::of(&button));

        Fw::Widget* pref = Fw::createWidget(QStringLiteral("Gui::PrefCheckBox"));
        QWidget* pw = Gui::FwQt::realize(pref, nullptr);
        QVERIFY(pw);
        QCOMPARE(QString::fromLatin1(pw->metaObject()->className()),
                 QStringLiteral("Gui::PrefCheckBox"));
        delete pw;
        delete pref;
    }

    // H1: the first native port (docs/Sandbox.md 7.12).  TaskOrientation
    // is models over the generated form; the dialog realizes them
    // through the Qt backend.  The gate: the panel drives its property
    // from either side, and the file's title reached the model.
    void test_taskOrientationPort()
    {
        App::Document* doc = App::GetApplication().newDocument("FwOrientation");
        auto feature = dynamic_cast<App::GeoFeature*>(doc->addObject("App::Placement", "Plane"));
        QVERIFY(feature);
        feature->Placement.setValue(
            Base::Placement(Base::Vector3d(0, 7, 0), Gui::Camera::convert(Gui::Camera::Rear)));

        auto dialog = new Gui::TaskOrientationDialog(feature);
        QCOMPARE(dialog->getDialogContent().size(), std::size_t(1));
        QWidget* box = dialog->getDialogContent().front();
        auto rXY = box->findChild<QRadioButton*>(QStringLiteral("XY_radioButton"));
        auto rXZ = box->findChild<QRadioButton*>(QStringLiteral("XZ_radioButton"));
        auto rYZ = box->findChild<QRadioButton*>(QStringLiteral("YZ_radioButton"));
        auto reverse = box->findChild<QCheckBox*>(QStringLiteral("Reverse_checkBox"));
        auto offset = box->findChild<Gui::QuantitySpinBox*>(QStringLiteral("Offset_doubleSpinBox"));
        auto preview = box->findChild<QLabel*>(QStringLiteral("previewLabel"));
        QVERIFY(rXY && rXZ && rYZ && reverse && offset && preview);
        Gui::Fw::UiForm* form = dialog->panel()->form();
        QVERIFY(Gui::FwQt::View::of(form));
        QCOMPARE(dialog->panel()->windowTitle(), QStringLiteral("Choose orientation"));
        QCOMPARE(preview->minimumWidth(), 48);

        // open restores the placement into the models; the widgets follow
        dialog->open();
        QVERIFY(rXZ->isChecked());
        QVERIFY(reverse->isChecked());
        QCOMPARE(offset->rawValue(), 7.0);
        QVERIFY(!preview->pixmap().isNull());
        QCOMPARE(preview->pixmap().width(), 48);
        auto mXZ = qobject_cast<Gui::Fw::QRadioButton*>(form->named(QStringLiteral("XZ_radioButton")));
        auto mOffset = qobject_cast<Gui::Fw::QuantitySpinBox*>(
            form->named(QStringLiteral("Offset_doubleSpinBox")));
        QVERIFY(mXZ && mOffset);
        QVERIFY(mXZ->isChecked());
        QCOMPARE(mOffset->rawValue(), 7.0);

        // the widget drives the model drives the property
        offset->setValue(12.0);
        QCOMPARE(mOffset->rawValue(), 12.0);
        QCOMPARE(feature->Placement.getValue().getPosition().y, 12.0);
        rYZ->click();
        QCOMPARE(feature->Placement.getValue().getPosition().x, 12.0);
        QVERIFY(feature->Placement.getValue().getRotation().isSame(
            Gui::Camera::convert(Gui::Camera::Left), 1e-5));
        reverse->click();
        QVERIFY(feature->Placement.getValue().getRotation().isSame(
            Gui::Camera::convert(Gui::Camera::Right), 1e-5));

        // the model drives the widget
        mOffset->setValue(3.0);
        QCOMPARE(offset->rawValue(), 3.0);
        QCOMPARE(feature->Placement.getValue().getPosition().x, 3.0);
        auto mXY = qobject_cast<Gui::Fw::QRadioButton*>(form->named(QStringLiteral("XY_radioButton")));
        mXY->setChecked(true);
        QVERIFY(rXY->isChecked());
        QVERIFY(!rYZ->isChecked());

        QVERIFY(dialog->accept());
        delete dialog;
        QCoreApplication::processEvents();
        App::GetApplication().closeDocument("FwOrientation");
    }

    // H1b: the expression seam (docs/Sandbox.md 7.12).  The model owns
    // the binding; the real widget binds to the same path; an
    // expression set on either side, or on the document, meets in the
    // bag and the value.  (`apply` runs a command through the Gui
    // application, which this test has none of: the GUI gate covers it.)
    // G3b: the item views (docs/Sandbox.md 7.11).  One row tree on the
    // model, one Qt path through the abstract item model for the tree
    // widget, the list, the table and a tree view over a model made by
    // the backend; the selection and the current row as state; a host
    // edit back as an event.
    void test_itemViews()
    {
        // a tree widget: rows with children, columns, flags, checks
        Fw::QTreeWidget tree;
        tree.setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
        int a = tree.addRow({QStringLiteral("A"), QStringLiteral("1")});
        int b = tree.addRow({QStringLiteral("B"), QStringLiteral("2")});
        int a1 = tree.addRow({QStringLiteral("A.1"), QStringLiteral("11")}, a);
        tree.setCheckState(a1, 0, Qt::Checked);
        tree.setExpanded(a, true);
        QCOMPARE(tree.rowCount(), 2);
        QCOMPARE(tree.rowCount(a), 1);
        QCOMPARE(tree.text(a1, 1), QStringLiteral("11"));

        QSignalSpy ops(&tree, &Fw::ItemView::itemsChanged);
        auto qw = qobject_cast<QTreeWidget*>(Gui::FwQt::realize(&tree, nullptr));
        QVERIFY(qw);
        QCOMPARE(qw->columnCount(), 2);
        QCOMPARE(qw->headerItem()->text(1), QStringLiteral("Value"));
        QCOMPARE(qw->topLevelItemCount(), 2);
        QTreeWidgetItem* ia = qw->topLevelItem(0);
        QCOMPARE(ia->text(0), QStringLiteral("A"));
        QCOMPARE(ia->childCount(), 1);
        QCOMPARE(ia->child(0)->text(1), QStringLiteral("11"));
        QCOMPARE(ia->child(0)->checkState(0), Qt::Checked);
        QVERIFY(ia->isExpanded());

        // a later row and a cell reach the widget; a removal too
        int c = tree.addRow({QStringLiteral("C")});
        QCOMPARE(qw->topLevelItemCount(), 3);
        tree.setText(c, 1, QStringLiteral("3"));
        QCOMPARE(qw->topLevelItem(2)->text(1), QStringLiteral("3"));
        tree.removeRow(b);
        QCOMPARE(qw->topLevelItemCount(), 2);
        QCOMPARE(qw->topLevelItem(1)->text(0), QStringLiteral("C"));
        QVERIFY(ops.count() >= 3);

        // the selection and the current row, both ways
        QSignalSpy selChanged(&tree, &Fw::ItemView::itemSelectionChanged);
        QSignalSpy curChanged(&tree, &Fw::ItemView::currentItemChanged);
        tree.setCurrent(c);
        QCOMPARE(qw->currentItem()->text(0), QStringLiteral("C"));
        QVERIFY(qw->currentItem()->isSelected());
        QCOMPARE(curChanged.count(), 1);
        QCOMPARE(selChanged.count(), 1);
        qw->setCurrentItem(ia);
        QCOMPARE(tree.currentId(), a);
        QCOMPARE(tree.selection(), QList<int> {a});
        ia->child(0)->setSelected(true);
        QVERIFY(tree.selection().contains(a1));

        // an edit in the widget comes back as itemChanged and the cell
        QSignalSpy changed(&tree, &Fw::ItemView::itemChanged);
        ia->setText(1, QStringLiteral("one"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(tree.text(a, 1), QStringLiteral("one"));
        ia->child(0)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(tree.checkState(a1, 0), static_cast<int>(Qt::Unchecked));

        // clear
        tree.clearRows();
        QCOMPARE(qw->topLevelItemCount(), 0);
        QCOMPARE(tree.currentId(), 0);
        delete qw;
        QCoreApplication::processEvents();

        // a list widget
        Fw::QListWidget list;
        list.addItem(QStringLiteral("x"));
        list.addItem(QStringLiteral("y"));
        auto ql = qobject_cast<QListWidget*>(Gui::FwQt::realize(&list, nullptr));
        QVERIFY(ql);
        QCOMPARE(ql->count(), 2);
        QCOMPARE(ql->item(1)->text(), QStringLiteral("y"));
        list.setCurrentRow(1);
        QCOMPARE(ql->currentRow(), 1);
        ql->setCurrentRow(0);
        QCOMPARE(list.currentRow(), 0);
        delete ql;

        // a table widget: rows of cells, header labels
        Fw::QTableWidget table;
        table.setHorizontalHeaderLabels({QStringLiteral("k"), QStringLiteral("v")});
        table.setRowCount(2);
        table.setItemText(0, 0, QStringLiteral("k0"));
        table.setItemText(1, 1, QStringLiteral("v1"));
        auto qt = qobject_cast<QTableWidget*>(Gui::FwQt::realize(&table, nullptr));
        QVERIFY(qt);
        QCOMPARE(qt->rowCount(), 2);
        QCOMPARE(qt->columnCount(), 2);
        QCOMPARE(qt->horizontalHeaderItem(1)->text(), QStringLiteral("v"));
        QCOMPARE(qt->item(0, 0)->text(), QStringLiteral("k0"));
        QCOMPARE(qt->item(1, 1)->text(), QStringLiteral("v1"));
        table.setCurrentCell(1, 1);
        QCOMPARE(qt->currentRow(), 1);
        QCOMPARE(qt->currentColumn(), 1);
        qt->item(0, 0)->setText(QStringLiteral("edited"));
        QCOMPARE(table.itemText(0, 0), QStringLiteral("edited"));
        delete qt;

        // a tree view: the backend makes the model
        Fw::QTreeView view;
        view.setColumns({QStringLiteral("c0"), QStringLiteral("c1")});
        int r = view.addRow({QStringLiteral("r"), QStringLiteral("rv")});
        view.addRow({QStringLiteral("r.0")}, r);
        auto qv = qobject_cast<QTreeView*>(Gui::FwQt::realize(&view, nullptr));
        QVERIFY(qv);
        auto model = qobject_cast<QStandardItemModel*>(qv->model());
        QVERIFY(model);
        QCOMPARE(model->columnCount(), 2);
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->item(0, 1)->text(), QStringLiteral("rv"));
        QCOMPARE(model->item(0, 0)->rowCount(), 1);
        QCOMPARE(model->item(0, 0)->child(0)->text(), QStringLiteral("r.0"));
        QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("c1"));
        view.setExpanded(r, true);
        QVERIFY(qv->isExpanded(model->index(0, 0)));
        delete qv;
        QCoreApplication::processEvents();
    }

    // G3b: dialogs and containers.  A tab widget's pages and current
    // index both ways, a splitter's sizes, a scroll area's content, a
    // dialog realized as a window of its own with its button box.
    /// G3c (docs/Sandbox.md 7.11): a form built in code -- the model's
    /// Layout tree realized when the widget is, hidden children left
    /// hidden, the ops after realization applied to the real layouts
    /// by their generated names, a font as data.
    void test_codeBuiltLayouts()
    {
        auto root = new Fw::Widget;
        root->setObjectName(QStringLiteral("root"));
        auto vbox = new Fw::Layout(Fw::Layout::VBox, root);
        vbox->setObjectName(QStringLiteral("v"));
        auto label = new Fw::QLabel(root);
        label->setText(QStringLiteral("Label"));
        auto edit = new Fw::QLineEdit(root);
        edit->setObjectName(QStringLiteral("edit"));
        auto row = new Fw::Layout(Fw::Layout::HBox);
        row->setObjectName(QStringLiteral("row"));
        vbox->addLayout(row);
        row->addWidget(label);
        row->addWidget(edit);
        auto grid = new Fw::Layout(Fw::Layout::Grid);
        grid->setObjectName(QStringLiteral("grid"));
        vbox->addLayout(grid);
        auto one = new Fw::QPushButton(QStringLiteral("One"), root);
        one->hide();  // hidden before it is placed: stays hidden
        auto two = new Fw::QPushButton(QStringLiteral("Two"), root);
        grid->addWidget(one, 0, 0);
        grid->addWidget(two, 0, 1, 1, 2);
        vbox->addSpacer(20, 40, QVariantList(), 1, 7);
        vbox->addStretch(1);
        vbox->setContentsMargins(1, 2, 3, 4);
        vbox->setSpacing(5);
        QVariantMap spec = vbox->spec();
        QCOMPARE(spec.value(QStringLiteral("class")).toString(), QStringLiteral("QVBoxLayout"));
        QCOMPARE(spec.value(QStringLiteral("items")).toList().size(), 4);
        QCOMPARE(spec.value(QStringLiteral("margins")).toList(), QVariantList({1, 2, 3, 4}));

        QWidget* w = Gui::FwQt::realize(root, nullptr);
        QVERIFY(w);
        QVERIFY(w->layout());
        QCOMPARE(w->layout()->objectName(), QStringLiteral("v"));
        QCOMPARE(w->layout()->count(), 4);
        QCOMPARE(w->layout()->contentsMargins(), QMargins(1, 2, 3, 4));
        QCOMPARE(w->layout()->spacing(), 5);
        auto qrow = w->findChild<QHBoxLayout*>(QStringLiteral("row"));
        QVERIFY(qrow);
        QCOMPARE(qrow->count(), 2);
        QCOMPARE(qrow->itemAt(1)->widget(), Gui::FwQt::widgetOf(edit));
        auto qgrid = w->findChild<QGridLayout*>(QStringLiteral("grid"));
        QVERIFY(qgrid);
        QCOMPARE(qgrid->count(), 2);
        int r = 0, c = 0, rs = 0, cs = 0;
        qgrid->getItemPosition(1, &r, &c, &rs, &cs);
        QCOMPARE(c, 1);
        QCOMPARE(cs, 2);
        QVERIFY(qobject_cast<QPushButton*>(Gui::FwQt::widgetOf(one)));
        QVERIFY(Gui::FwQt::widgetOf(one)->isHidden());
        QVERIFY(!Gui::FwQt::widgetOf(two)->isHidden());

        // an op after realization finds the real layout by name
        auto three = new Fw::QPushButton(QStringLiteral("Three"), root);
        grid->addWidget(three, 1, 0);
        QCOMPARE(qgrid->count(), 3);
        QCOMPARE(qgrid->itemAt(2)->widget(), Gui::FwQt::widgetOf(three));
        row->takeAt(0);
        QCOMPARE(qrow->count(), 1);
        label->setFont(QVariantMap {{QStringLiteral("bold"), true}});
        QVERIFY(Gui::FwQt::widgetOf(label)->font().bold());

        delete w;
        delete root;
    }

    /// G3c: a tool bar's content as a bar (widgets, actions,
    /// separators), the action model on both sides, the bar's toggle
    /// action bound, an action on a line edit, the key stream a widget
    /// asked for (eaten when answered so), a menu with a sub-menu.
    void test_barsActionsAndKeys()
    {
        auto bar = new Fw::QToolBar(QStringLiteral("Tray"));
        auto btn = new Fw::QPushButton(QStringLiteral("WP"));
        bar->addWidget(btn);
        auto act = new Fw::QAction(QStringLiteral("Act"));
        act->setCheckable(true);
        bar->addAction(act);
        bar->addSeparator();
        QCOMPARE(bar->actions().size(), 1);
        QWidget* w = Gui::FwQt::realize(bar, nullptr);
        auto tb = qobject_cast<QToolBar*>(w);
        QVERIFY(tb);
        QCOMPARE(tb->windowTitle(), QStringLiteral("Tray"));
        QCOMPARE(tb->actions().size(), 3);
        QVERIFY(qobject_cast<QWidgetAction*>(tb->actions().at(0)));
        QVERIFY(tb->actions().at(2)->isSeparator());
        QAction* qa = Gui::FwQt::actionWidgetOf(act);
        QVERIFY(qa);
        QCOMPARE(tb->actions().at(1), qa);
        QCOMPARE(qa->text(), QStringLiteral("Act"));
        QVERIFY(qa->isCheckable());
        QSignalSpy triggered(act, &Fw::QAction::triggered);
        QSignalSpy barTriggered(bar, &Fw::QToolBar::actionTriggered);
        qa->trigger();
        QCOMPARE(triggered.count(), 1);
        QVERIFY(triggered.at(0).at(0).toBool());
        QVERIFY(act->isChecked());
        QCOMPARE(barTriggered.count(), 1);
        QCOMPARE(barTriggered.at(0).at(0).value<Fw::Widget*>(), act);
        act->setChecked(false);
        QVERIFY(!qa->isChecked());
        act->setText(QStringLiteral("Renamed"));
        QCOMPARE(qa->text(), QStringLiteral("Renamed"));
        // added after realization
        auto act2 = new Fw::QAction(QStringLiteral("Two"));
        bar->addAction(act2);
        QCOMPARE(tb->actions().size(), 4);
        QCOMPARE(tb->actions().at(3), Gui::FwQt::actionWidgetOf(act2));
        bar->clear();
        QCOMPARE(tb->actions().size(), 0);
        // the toggle-view action is the real bar's own
        Fw::QAction* toggle = bar->toggleViewAction();
        QCOMPARE(Gui::FwQt::actionWidgetOf(toggle), tb->toggleViewAction());
        QCOMPARE(toggle->text(), QStringLiteral("Tray"));
        toggle->setVisible(false);
        QVERIFY(!tb->toggleViewAction()->isVisible());

        // an action on a line edit, positioned
        auto edit = new Fw::QLineEdit;
        auto lock = new Fw::QAction;
        lock->setIcon(QStringLiteral(":/icons/Draft_Snap_Lock.svg"));
        edit->addAction(lock, 1);
        QCOMPARE(edit->actions().size(), 1);
        QWidget* ew = Gui::FwQt::realize(edit, nullptr);
        auto qe = qobject_cast<QLineEdit*>(ew);
        QVERIFY(qe);
        QCOMPARE(qe->actions().size(), 1);
        QCOMPARE(qe->actions().at(0), Gui::FwQt::actionWidgetOf(lock));
        lock->setVisible(false);
        QVERIFY(!qe->actions().at(0)->isVisible());
        edit->removeAction(lock);
        QCOMPARE(qe->actions().size(), 0);

        // the key stream: a watched type crosses as `qevent`, the
        // answer given inside it decides whether the widget gets it
        edit->setWatchEvents(QVariantList {6, 8});
        QVariantList seen;
        bool eat = false;
        connect(edit, &Fw::Widget::eventEmitted, [&](const QString& n, const QVariantList& a) {
            if (n == QLatin1String("qevent")) {
                seen = a;
                if (a.value(0).toInt() == 6)
                    edit->request(QStringLiteral("eventDone"), QVariantList {eat});
            }
        });
        QTest::keyClick(qe, Qt::Key_A);
        QCOMPARE(seen.value(0).toInt(), 6);
        QCOMPARE(seen.value(1).toInt(), static_cast<int>(Qt::Key_A));
        QCOMPARE(seen.value(3).toString(), QStringLiteral("a"));
        QCOMPARE(qe->text(), QStringLiteral("a"));
        eat = true;
        QTest::keyClick(qe, Qt::Key_B);
        QCOMPARE(qe->text(), QStringLiteral("a"));
        QVERIFY(!edit->hasFocus());
        QFocusEvent focus(QEvent::FocusIn);
        QCoreApplication::sendEvent(qe, &focus);
        QVERIFY(edit->hasFocus());
        edit->setWatchEvents(QVariantList());
        QTest::keyClick(qe, Qt::Key_C);
        QCOMPARE(qe->text(), QStringLiteral("ac"));

        // a menu: actions, a separator, a sub-menu
        auto menu = new Fw::QMenu(QStringLiteral("M"));
        auto item = new Fw::QAction(QStringLiteral("Item"));
        menu->addAction(item);
        menu->addSeparator();
        auto sub = new Fw::QMenu(QStringLiteral("Sub"));
        menu->addMenu(sub);
        sub->addAction(new Fw::QAction(QStringLiteral("SubItem")));
        QWidget* mw = Gui::FwQt::realize(menu, nullptr);
        auto qm = qobject_cast<QMenu*>(mw);
        QVERIFY(qm);
        QCOMPARE(qm->title(), QStringLiteral("M"));
        QCOMPARE(qm->actions().size(), 3);
        QVERIFY(qm->actions().at(2)->menu());
        QCOMPARE(qm->actions().at(2)->menu()->title(), QStringLiteral("Sub"));
        QCOMPARE(qm->actions().at(2)->menu()->actions().size(), 1);
        QSignalSpy menuTriggered(menu, &Fw::QMenu::triggered);
        Gui::FwQt::actionWidgetOf(item)->trigger();
        QCOMPARE(menuTriggered.count(), 1);
        QCOMPARE(menuTriggered.at(0).at(0).value<Fw::Widget*>(), item);

        delete mw;
        delete menu;  // the sub-menu is its child (the bar attached it)
        delete ew;
        delete edit;
        delete lock;
        delete w;
        delete bar;  // the button is its child
        delete act;
        delete act2;
    }

    void test_dialogsAndContainers()
    {
        Fw::QTabWidget tabs;
        auto p1 = new Fw::QLabel(QStringLiteral("one"));
        auto p2 = new Fw::QLabel(QStringLiteral("two"));
        tabs.addTab(p1, QStringLiteral("One"));
        tabs.addTab(p2, QStringLiteral("Two"));
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(tabs.currentIndex(), 0);
        auto qtabs = qobject_cast<QTabWidget*>(Gui::FwQt::realize(&tabs, nullptr));
        QVERIFY(qtabs);
        QCOMPARE(qtabs->count(), 2);
        QCOMPARE(qtabs->tabText(1), QStringLiteral("Two"));
        QCOMPARE(qobject_cast<QLabel*>(qtabs->widget(1))->text(), QStringLiteral("two"));
        QSignalSpy tabChanged(&tabs, &Fw::QTabWidget::currentChanged);
        tabs.setCurrentIndex(1);
        QCOMPARE(qtabs->currentIndex(), 1);
        QCOMPARE(tabChanged.count(), 1);
        qtabs->setCurrentIndex(0);
        QCOMPARE(tabs.currentIndex(), 0);
        tabs.setTabText(0, QStringLiteral("Uno"));
        QCOMPARE(qtabs->tabText(0), QStringLiteral("Uno"));
        auto p3 = new Fw::QLabel(QStringLiteral("three"));
        tabs.addTab(p3, QStringLiteral("Three"));
        QCOMPARE(qtabs->count(), 3);
        delete qtabs;
        QCoreApplication::processEvents();

        Fw::QSplitter split;
        split.addWidget(new Fw::QLabel(QStringLiteral("l")));
        split.addWidget(new Fw::QLabel(QStringLiteral("r")));
        auto qsplit = qobject_cast<QSplitter*>(Gui::FwQt::realize(&split, nullptr));
        QVERIFY(qsplit);
        QCOMPARE(qsplit->count(), 2);
        qsplit->resize(400, 100);
        split.setSizes({300, 100});
        // Qt keeps the ratio, less the handle
        QVERIFY(qsplit->sizes().at(0) >= 290);
        QVERIFY(qsplit->sizes().at(0) > qsplit->sizes().at(1));
        delete qsplit;

        Fw::QScrollArea area;
        area.setWidgetResizable(true);
        area.setWidget(new Fw::QLabel(QStringLiteral("content")));
        auto qarea = qobject_cast<QScrollArea*>(Gui::FwQt::realize(&area, nullptr));
        QVERIFY(qarea);
        QVERIFY(qarea->widgetResizable());
        QVERIFY(qobject_cast<QLabel*>(qarea->widget()));
        delete qarea;

        Fw::FileChooser chooser;
        chooser.setFilter(QStringLiteral("Fonts (*.ttf)"));
        chooser.setFileName(QStringLiteral("/tmp/a.ttf"));
        auto qchooser = qobject_cast<Gui::FileChooser*>(Gui::FwQt::realize(&chooser, nullptr));
        QVERIFY(qchooser);
        QCOMPARE(qchooser->fileName(), QStringLiteral("/tmp/a.ttf"));
        QCOMPARE(qchooser->filter(), QStringLiteral("Fonts (*.ttf)"));
        QSignalSpy nameChanged(&chooser, &Fw::FileChooser::fileNameChanged);
        qchooser->setFileName(QStringLiteral("/tmp/b.ttf"));
        QCOMPARE(chooser.fileName(), QStringLiteral("/tmp/b.ttf"));
        QCOMPARE(nameChanged.count(), 1);
        delete qchooser;

        // a dialog with a button box, as a window of its own
        Fw::QDialog dialog;
        dialog.setWindowTitle(QStringLiteral("Ask"));
        auto box = new Fw::QDialogButtonBox(&dialog);
        box->setStandardButtons(Fw::QDialogButtonBox::Ok | Fw::QDialogButtonBox::Cancel);
        QWidget* window = Gui::FwQt::realizeTopLevel(&dialog);
        auto qdialog = qobject_cast<QDialog*>(window);
        QVERIFY(qdialog);
        QVERIFY(qdialog->isWindow());
        QCOMPARE(qdialog->windowTitle(), QStringLiteral("Ask"));
        auto qbox = qobject_cast<QDialogButtonBox*>(Gui::FwQt::realize(box, qdialog));
        QVERIFY(qbox);
        QVERIFY(qbox->button(QDialogButtonBox::Ok));
        QVERIFY(qbox->button(QDialogButtonBox::Cancel));
        QSignalSpy accepted(&dialog, &Fw::QDialog::accepted);
        QSignalSpy finished(&dialog, &Fw::QDialog::finished);
        QSignalSpy boxClicked(box, &Fw::QDialogButtonBox::clicked);
        QSignalSpy boxAccepted(box, &Fw::QDialogButtonBox::accepted);
        QObject::connect(qbox, &QDialogButtonBox::accepted, qdialog, &QDialog::accept);
        // exec: the nested loop, OK clicked from a timer inside it
        QTimer::singleShot(50, qbox->button(QDialogButtonBox::Ok), &QPushButton::click);
        int code = Gui::FwQt::execDialog(&dialog);
        QCOMPARE(code, static_cast<int>(QDialog::Accepted));
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(boxAccepted.count(), 1);
        QCOMPARE(boxClicked.count(), 1);
        QCOMPARE(boxClicked.at(0).at(0).toInt(), static_cast<int>(Fw::QDialogButtonBox::Ok));
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        // reject from the model
        QSignalSpy rejected(&dialog, &Fw::QDialog::rejected);
        qdialog->show();
        dialog.reject();
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!qdialog->isVisible());
        // the window dies with the model
        QPointer<QWidget> guard(window);
        {
            Fw::QDialog temp;
            QWidget* tw = Gui::FwQt::realizeTopLevel(&temp);
            guard = tw;
        }
        // deleteLater from outside an event loop: flushed explicitly
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(guard.isNull());
    }

    void test_expressionSeam()
    {
        App::Document* doc = App::GetApplication().newDocument("FwExpression");
        App::DocumentObject* obj = doc->addObject("App::Placement", "Holder");
        QVERIFY(obj);
        auto length = static_cast<App::PropertyLength*>(
            obj->addDynamicProperty("App::PropertyLength", "Length"));
        QVERIFY(length);
        length->setValue(4.0);

        Fw::QuantitySpinBox spin;
        spin.setUnitText(QStringLiteral("mm"));
        QVERIFY(!spin.isBound());
        QVERIFY(spin.has(QStringLiteral("binding")));
        QVERIFY(spin.has(QStringLiteral("expression")));
        spin.bind(*length);
        QVERIFY(spin.isBound());
        QVERIFY(spin.isTouched(QStringLiteral("binding")));
        QVERIFY(!spin.boundToName().isEmpty());
        QCOMPARE(spin.property("binding").toString(), spin.boundToName());
        QCOMPARE(spin.expressionText(), QString());

        // the document gets an expression: the model hears it, the value follows
        QSignalSpy valueChanged(&spin, &Fw::QuantitySpinBox::valueChanged);
        obj->setExpression(App::ObjectIdentifier(*length),
                           App::Expression::parse(obj, "2 * 5 mm"));
        QVERIFY(spin.hasExpression());
        QCOMPARE(spin.expressionText(), QStringLiteral("2 * 5 mm"));
        QCOMPARE(spin.rawValue(), 10.0);
        QCOMPARE(valueChanged.count(), 1);

        // realized: the real widget binds to the same path and shows it
        QWidget* w = Gui::FwQt::realize(&spin, nullptr);
        auto q = qobject_cast<Gui::QuantitySpinBox*>(w);
        QVERIFY(q);
        QVERIFY(q->isBound());
        QCOMPARE(q->expressionText(), QStringLiteral("2 * 5 mm"));
        QCOMPARE(q->rawValue(), 10.0);
        QVERIFY(q->isReadOnly());

        // the widget's dialog path: what it sets lands in the document
        // and comes back into the bag
        q->setExpression(App::Expression::parse(obj, "7 mm"));
        QCOMPARE(spin.expressionText(), QStringLiteral("7 mm"));
        QCOMPARE(spin.rawValue(), 7.0);

        // the model clears it: the widget is editable again
        QString error;
        QVERIFY(spin.setExpressionText(QString(), &error));
        QVERIFY(!spin.hasExpression());
        QCOMPARE(spin.expressionText(), QString());
        QVERIFY(!q->hasExpression());
        QVERIFY(!q->isReadOnly());

        // an expression that does not parse is refused with the reason
        QVERIFY(!spin.setExpressionText(QStringLiteral("2 * ("), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!spin.hasExpression());

        // a double spin box carries the same seam
        Fw::DoubleSpinBox dbl;
        auto x = obj->getPropertyByName("Placement");
        QVERIFY(x);
        dbl.bind(App::ObjectIdentifier::parse(obj, std::string("Placement.Base.x")));
        QVERIFY(dbl.isBound());
        obj->setExpression(App::ObjectIdentifier::parse(obj, std::string("Placement.Base.x")),
                           App::Expression::parse(obj, "3 + 4"));
        QCOMPARE(dbl.value(), 7.0);
        QCOMPARE(dbl.expressionText(), QStringLiteral("3 + 4"));

        // a QSignalBlocker on the model silences its typed signals but
        // not the backend's mirror
        {
            QSignalBlocker blocker(&spin);
            spin.setValue(22.0);
        }
        QCOMPARE(q->rawValue(), 22.0);
        QCOMPARE(valueChanged.count(), 2);  // 10, 7 (clearing kept 7); not 22

        delete w;
        QCoreApplication::processEvents();
        App::GetApplication().closeDocument("FwExpression");
    }

    void test_storeFanOut()
    {
        // docs/Sandbox.md 7.18 (a): the store's `message` signal beside
        // its one sink, a host producer's adopted objects, the snapshot
        // a late subscriber rebuilds from, and the writer's origin tag
        Fw::Store& store = Fw::Store::instance();
        store.reset();
        QSignalSpy messages(&store, &Fw::Store::message);
        QStringList sunk;
        store.setSink([&sunk](const QString& id, const QString& method, const QVariantMap&) {
            sunk << id + QLatin1Char('/') + method;
        });

        // a producer's bar with an action, a separator and a widget
        auto action = new Fw::QAction(QStringLiteral("Line"));
        action->setCommand(QStringLiteral("Std_Nothing"));
        action->setIcon(QStringLiteral("Draft_Line"));
        auto combo = new Fw::Widget;
        combo->setQtClass(QStringLiteral("WorkbenchComboBox"));
        auto bar = new Fw::QToolBar(QStringLiteral("Draft creation"));
        bar->setObjectName(QStringLiteral("Draft creation tools"));
        bar->addAction(action);
        bar->addSeparator();
        bar->addWidget(combo);
        store.adopt(QStringLiteral("cmd:Std_Nothing"), action);
        store.adopt(QStringLiteral("widget:Draft creation tools#2"), combo);
        store.adopt(QStringLiteral("toolbar:Draft creation tools"), bar);
        QVERIFY(store.isAdopted(QStringLiteral("toolbar:Draft creation tools")));
        QCOMPARE(messages.count(), 3);
        QCOMPARE(messages.at(2).at(1).toString(), QStringLiteral("open"));
        QVariantMap snap = messages.at(2).at(2).toMap();
        QCOMPARE(snap.value(QStringLiteral("model")).toString(), QStringLiteral("QToolBarModel"));
        QVariantList items = snap.value(QStringLiteral("layout")).toMap()
                                 .value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("action")).toString(),
                 QStringLiteral("IPY_MODEL_cmd:Std_Nothing"));
        QVERIFY(items.at(1).toMap().value(QStringLiteral("separator")).toBool());
        QCOMPARE(items.at(2).toMap().value(QStringLiteral("widget")).toString(),
                 QStringLiteral("IPY_MODEL_widget:Draft creation tools#2"));
        QCOMPARE(snap.value(QStringLiteral("state")).toMap()
                     .value(QStringLiteral("q_windowTitle")).toString(),
                 QStringLiteral("Draft creation"));
        // the snapshot order: the referenced before the referrer
        QStringList order = store.snapshotOrder();
        QVERIFY(order.indexOf(QStringLiteral("cmd:Std_Nothing"))
                < order.indexOf(QStringLiteral("toolbar:Draft creation tools")));
        QVERIFY(order.indexOf(QStringLiteral("widget:Draft creation tools#2"))
                < order.indexOf(QStringLiteral("toolbar:Draft creation tools")));
        // the sink (the guest) heard none of this: nothing of its own
        QVERIFY(sunk.isEmpty());

        // a native write: one update, origin 0, the sink hears it too
        messages.clear();
        action->setEnabled(false);
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toString(), QStringLiteral("cmd:Std_Nothing"));
        QCOMPARE(messages.at(0).at(1).toString(), QStringLiteral("update"));
        QCOMPARE(messages.at(0).at(2).toMap().value(QStringLiteral("q_enabled")).toBool(), false);
        QCOMPARE(messages.at(0).at(3).toULongLong(), 0ULL);
        QCOMPARE(sunk, QStringList {QStringLiteral("cmd:Std_Nothing/update")});

        // a client's write under its origin: the message carries it
        messages.clear();
        QVERIFY(store.applyUpdate(QStringLiteral("cmd:Std_Nothing"),
                                  QVariantMap {{QStringLiteral("q_checked"), true}}, 7));
        QVERIFY(action->isChecked());
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(3).toULongLong(), 7ULL);
        QCOMPARE(Fw::Store::currentOrigin(), 0ULL);
        // a client's request, likewise
        messages.clear();
        QSignalSpy requested(action, &Fw::Widget::requested);
        QVERIFY(store.applyCustom(QStringLiteral("cmd:Std_Nothing"),
                                  QVariantMap {{QStringLiteral("event"), QStringLiteral("trigger")}},
                                  7));
        QCOMPARE(requested.count(), 1);
        // a producer's layout change is one update
        messages.clear();
        bar->addSeparator();
        store.notifyLayout(QStringLiteral("toolbar:Draft creation tools"));
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(2).toMap().value(QStringLiteral("layoutSpec")).toMap()
                     .value(QStringLiteral("items")).toList().size(),
                 4);

        // the guest cannot close an adopted object, and a reset keeps it
        QVERIFY(!store.commClose(QStringLiteral("cmd:Std_Nothing")));
        QVariantMap guestState;
        guestState.insert(QStringLiteral("_model_name"), QStringLiteral("QLabelModel"));
        QVERIFY(store.commOpen(QStringLiteral("g1"), guestState));
        QCOMPARE(store.count(), 4);
        messages.clear();
        store.reset();
        QCOMPARE(store.count(), 3);
        QVERIFY(store.object(QStringLiteral("toolbar:Draft creation tools")) == bar);
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(0).toString(), QStringLiteral("g1"));
        QCOMPARE(messages.at(0).at(1).toString(), QStringLiteral("close"));
        // the producer takes its objects out: one close each
        messages.clear();
        QVERIFY(store.release(QStringLiteral("toolbar:Draft creation tools")));
        QVERIFY(!store.release(QStringLiteral("toolbar:Draft creation tools")));
        QCOMPARE(messages.count(), 1);
        QCOMPARE(messages.at(0).at(1).toString(), QStringLiteral("close"));
        store.release(QStringLiteral("cmd:Std_Nothing"));
        store.release(QStringLiteral("widget:Draft creation tools#2"));
        QCOMPARE(store.count(), 0);
        delete bar;  // the widget item is the bar's child, the action is not
        delete action;
        store.setSink(nullptr);
    }

    void test_widgetStream()
    {
        // docs/Sandbox.md 7.18: the store fanned out over the control
        // channel, per connection, with the writer skipped
        Fw::Store& store = Fw::Store::instance();
        store.reset();
        Gui::installSceneWidgetOps();
        Gui::SceneWidgetStream& stream = Gui::SceneWidgetStream::instance();
        QList<QPair<uint64_t, QJsonObject>> pushed;
        stream.setSender([&pushed](uint64_t client, const std::string& json) {
            if (client > 1000)
                return false;  // a lost connection
            pushed.append({client, QJsonDocument::fromJson(QByteArray::fromStdString(json)).object()});
            return true;
        });
        auto control = [](const char* json, uint64_t client, bool viewOnly = false) {
            return QJsonDocument::fromJson(QByteArray(
                                               Gui::handleSceneControlRequest(json, std::string(),
                                                                              viewOnly, client)
                                                   .c_str()))
                .object();
        };

        auto label = new Fw::QLabel(QStringLiteral("hello"));
        store.adopt(QStringLiteral("cmd:Std_Label"), label);
        auto other = new Fw::QLabel(QStringLiteral("guest"));
        store.adopt(QStringLiteral("g:other"), other);

        // a view-only connection may not subscribe (the ops mutate)
        QJsonObject reply = control(R"({"id":1,"op":"widgets.subscribe","toolbars":true})", 5,
                                    true);
        QCOMPARE(reply.value(QLatin1String("ok")).toBool(), false);
        // client 5 takes the mirror's ids only (no main window here: the
        // mirror itself does not start), client 6 everything
        reply = control(R"({"id":2,"op":"widgets.subscribe","toolbars":true})", 5);
        QCOMPARE(reply.value(QLatin1String("ok")).toBool(), true);
        QVERIFY(reply.contains(QLatin1String("locale")));
        reply = control(R"({"id":3,"op":"widgets.subscribe","all":true})", 6);
        QCOMPARE(reply.value(QLatin1String("ok")).toBool(), true);
        QCOMPARE(stream.subscriberCount(), 2);
        QVERIFY(pushed.isEmpty());  // the snapshot follows the reply
        QCoreApplication::processEvents();
        QStringList got;
        for (const auto& p : pushed)
            got << QString::number(p.first) + QLatin1Char('/')
                    + p.second.value(QLatin1String("id")).toString();
        got.sort();
        QCOMPARE(got, (QStringList {QStringLiteral("5/cmd:Std_Label"),
                                    QStringLiteral("6/cmd:Std_Label"),
                                    QStringLiteral("6/g:other")}));
        QCOMPARE(pushed.at(0).second.value(QLatin1String("method")).toString(),
                 QStringLiteral("open"));
        QCOMPARE(pushed.at(0).second.value(QLatin1String("model")).toString(),
                 QStringLiteral("QLabelModel"));

        // a native write reaches both, as one op each
        pushed.clear();
        label->setText(QStringLiteral("changed"));
        QCOMPARE(pushed.size(), 2);
        QCOMPARE(pushed.at(0).second.value(QLatin1String("method")).toString(),
                 QStringLiteral("update"));
        QCOMPARE(pushed.at(0).second.value(QLatin1String("content")).toObject()
                     .value(QLatin1String("q_text")).toString(),
                 QStringLiteral("changed"));

        // client 6's own write: applied, echoed to 5 only
        pushed.clear();
        reply = control(R"({"id":4,"op":"widgets.update","target":"cmd:Std_Label",)"
                        R"("state":{"q_text":"from six"}})", 6);
        QCOMPARE(reply.value(QLatin1String("ok")).toBool(), true);
        QCOMPARE(label->text(), QStringLiteral("from six"));
        QCOMPARE(pushed.size(), 1);
        QCOMPARE(pushed.at(0).first, 5ULL);
        // an unknown target
        reply = control(R"({"id":5,"op":"widgets.update","target":"nope","state":{}})", 6);
        QCOMPARE(reply.value(QLatin1String("code")).toString(), QStringLiteral("UnknownObject"));

        // an icon by name: a stock SVG, and a name that is nothing
        reply = control(R"({"id":6,"op":"widgets.icon","name":"document-new"})", 6);
        QCOMPARE(reply.value(QLatin1String("ok")).toBool(), true);
        QCOMPARE(reply.value(QLatin1String("format")).toString(), QStringLiteral("svg"));
        QVERIFY(reply.value(QLatin1String("data")).toString().contains(QLatin1String("<svg")));
        reply = control(R"({"id":7,"op":"widgets.icon","name":"no-such-icon-anywhere"})", 6);
        QCOMPARE(reply.value(QLatin1String("code")).toString(), QStringLiteral("UnknownIcon"));

        // leaving, and a connection that is gone
        reply = control(R"({"id":8,"op":"widgets.unsubscribe"})", 6);
        QCOMPARE(stream.subscriberCount(), 1);
        control(R"({"id":9,"op":"widgets.subscribe","all":true})", 1001);
        QCOMPARE(stream.subscriberCount(), 2);
        QCoreApplication::processEvents();  // its snapshot push fails: dropped
        QCOMPARE(stream.subscriberCount(), 1);
        pushed.clear();
        label->setText(QStringLiteral("last"));
        QCOMPARE(pushed.size(), 1);
        QCOMPARE(pushed.at(0).first, 5ULL);
        control(R"({"id":10,"op":"widgets.subscribe","toolbars":false})", 5);
        QCOMPARE(stream.subscriberCount(), 0);

        store.release(QStringLiteral("cmd:Std_Label"));
        store.release(QStringLiteral("g:other"));
        delete label;
        delete other;
        stream.setSender(nullptr);
    }

    void test_panelMirror()
    {
        // docs/Sandbox.md 7.19, M1: a real task panel -- a uic'd form under
        // a TaskBox, a hand-built box, the button box -- walked into store
        // models, kept current from the widgets, written into from a client
        Fw::Store& store = Fw::Store::instance();
        store.reset();
        Fw::PanelMirror& mirror = Fw::PanelMirror::instance();
        QSignalSpy messages(&store, &Fw::Store::message);
        auto ref = [](const QVariant& v) {
            return v.toString().mid(QStringLiteral("IPY_MODEL_").size());
        };
        auto items = [](const QVariantMap& snap) {
            return snap.value(QStringLiteral("layout")).toMap().value(QStringLiteral("items"))
                .toList();
        };
        auto state = [](const QVariantMap& snap, const char* key) {
            return snap.value(QStringLiteral("state")).toMap().value(QStringLiteral("q_")
                                                                    + QLatin1String(key));
        };
        auto named = [&store](const QString& name) -> Fw::Widget* {
            for (const QString& id : store.ids()) {
                Fw::Widget* w = store.object(id);
                if (w && w->objectName() == name && id.startsWith(QLatin1String("pw:")))
                    return w;
            }
            return nullptr;
        };
        auto idOf = [&store, &named](const QString& name) { return store.idOf(named(name)); };

        // Draft's OrthoArray form through the host's loader, as a workbench
        // has it, under a TaskBox
        Gui::GetWidgetFactorySupplier();  // the loader makes FreeCAD's own widgets
        auto loader = Gui::UiLoader::newInstance();
        QFile file(QString::fromUtf8(FW_TEST_UI_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QWidget* form = loader->load(&file);
        QVERIFY(form);
        auto box = new Gui::TaskView::TaskBox(QStringLiteral("Ortho array"), true, nullptr);
        box->groupLayout()->addWidget(form);
        // a box built by hand: a line edit, a combo with a stretch and an
        // alignment, a stack of two pages
        auto hand = new QWidget;
        auto vbox = new QVBoxLayout(hand);
        vbox->setObjectName(QStringLiteral("handLayout"));
        auto edit = new QLineEdit(hand);
        edit->setObjectName(QStringLiteral("handEdit"));
        auto combo = new QComboBox(hand);
        combo->setObjectName(QStringLiteral("handCombo"));
        combo->addItems({QStringLiteral("alpha"), QStringLiteral("beta")});
        auto stack = new QStackedWidget(hand);
        stack->setObjectName(QStringLiteral("handStack"));
        auto page0 = new QLabel(QStringLiteral("page zero"), stack);
        page0->setObjectName(QStringLiteral("page0"));
        stack->addWidget(page0);
        auto page1 = new QLabel(QStringLiteral("page one"), stack);
        page1->setObjectName(QStringLiteral("page1"));
        stack->addWidget(page1);
        vbox->addWidget(edit);
        vbox->addWidget(combo, 2, Qt::AlignRight);
        vbox->addWidget(stack);
        auto box2 = new Gui::TaskView::TaskBox(QStringLiteral("Hand"), true, nullptr);
        box2->groupLayout()->addWidget(hand);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        // shown (offscreen), so the widgets paint and the watch has its evidence
        QWidget host;
        auto hostLay = new QVBoxLayout(&host);
        hostLay->addWidget(box);
        hostLay->addWidget(box2);
        hostLay->addWidget(buttons);
        host.show();
        QCoreApplication::processEvents();

        mirror.show(QStringLiteral("TestDialog"), {box, box2}, buttons);
        QVERIFY(mirror.isRunning());
        const QString rootId = mirror.panelId();
        QVERIFY(rootId.startsWith(QLatin1String("panel:")));
        QVERIFY(store.isAdopted(QStringLiteral("panel")));

        // the root: a QDialog model with the dialog's class; its layout the
        // boxes then the button box
        QVariantMap root = store.snapshot(rootId);
        QCOMPARE(root.value(QStringLiteral("model")).toString(), QStringLiteral("QDialogModel"));
        QCOMPARE(root.value(QStringLiteral("qtClass")).toString(), QStringLiteral("TestDialog"));
        QCOMPARE(state(root, "windowTitle").toString(), QStringLiteral("Ortho array"));
        QCOMPARE(root.value(QStringLiteral("parent")).toString(), QStringLiteral("IPY_MODEL_panel"));
        QVariantList rootItems = items(root);
        QCOMPARE(rootItems.size(), 3);
        const QString boxId = ref(rootItems.at(0).toMap().value(QStringLiteral("widget")));
        QVariantMap boxSnap = store.snapshot(boxId);
        QCOMPARE(boxSnap.value(QStringLiteral("model")).toString(), QStringLiteral("QGroupBoxModel"));
        QCOMPARE(boxSnap.value(QStringLiteral("qtClass")).toString(),
                 QStringLiteral("Gui::TaskView::TaskBox"));
        QCOMPARE(state(boxSnap, "title").toString(), QStringLiteral("Ortho array"));
        QCOMPARE(state(boxSnap, "checkable").toBool(), true);
        QCOMPARE(state(boxSnap, "checked").toBool(), true);
        QCOMPARE(state(boxSnap, "flat").toBool(), false);
        QCOMPARE(boxSnap.value(QStringLiteral("parent")).toString(),
                 QStringLiteral("IPY_MODEL_") + rootId);
        const QString buttonsId = ref(rootItems.at(2).toMap().value(QStringLiteral("widget")));
        QVariantMap buttonsSnap = store.snapshot(buttonsId);
        QCOMPARE(buttonsSnap.value(QStringLiteral("model")).toString(),
                 QStringLiteral("QDialogButtonBoxModel"));
        QCOMPARE(state(buttonsSnap, "standardButtons").toInt(),
                 static_cast<int>(QDialogButtonBox::Ok | QDialogButtonBox::Cancel));
        QVariantList buttonItems = items(buttonsSnap);
        QCOMPARE(buttonItems.size(), 2);
        QSet<int> flags;
        for (const QVariant& item : buttonItems) {
            QVariantMap b = store.snapshot(ref(item.toMap().value(QStringLiteral("widget"))));
            QCOMPARE(b.value(QStringLiteral("model")).toString(), QStringLiteral("QPushButtonModel"));
            QVERIFY(!state(b, "text").toString().isEmpty());
            flags.insert(state(b, "standardButton").toInt());
        }
        QCOMPARE(flags, (QSet<int> {QDialogButtonBox::Ok, QDialogButtonBox::Cancel}));

        // every named child of the form is a model of the expected class,
        // the real class name kept as qtClass
        struct Expect
        {
            const char* name;
            const char* model;
            const char* qtClass;
        };
        for (const Expect& e : {Expect {"radiobutton_x_axis", "QRadioButtonModel", "QRadioButton"},
                                Expect {"spinbox_n_X", "QSpinBoxModel", "QSpinBox"},
                                Expect {"input_X_x", "InputFieldModel", "Gui::InputField"},
                                Expect {"group_X", "QGroupBoxModel", "QGroupBox"},
                                Expect {"label_n_X", "QLabelModel", "QLabel"},
                                Expect {"button_reset_X", "QPushButtonModel", "QPushButton"},
                                Expect {"DraftOrthoArrayTaskPanel", "QWidgetModel", "QWidget"}}) {
            Fw::Widget* w = named(QString::fromUtf8(e.name));
            QVERIFY2(w, e.name);
            QCOMPARE(w->modelName(), QString::fromUtf8(e.model));
            QCOMPARE(w->qtClass(), QString::fromUtf8(e.qtClass));
        }
        QCOMPARE(named(QStringLiteral("label_n_X"))->property("text").toString(),
                 form->findChild<QLabel*>(QStringLiteral("label_n_X"))->text());
        QCOMPARE(named(QStringLiteral("spinbox_n_X"))->property("value").toInt(),
                 form->findChild<QSpinBox*>(QStringLiteral("spinbox_n_X"))->value());

        // the layout spec matches the .ui: the form's grid by name and count,
        // a cell's position as the real grid has it
        Fw::Widget* formModel = named(QStringLiteral("DraftOrthoArrayTaskPanel"));
        QVERIFY(formModel->layout());
        QCOMPARE(formModel->layout()->className(), QStringLiteral("QGridLayout"));
        QCOMPARE(formModel->layout()->objectName(), QStringLiteral("gridLayout_3"));
        QCOMPARE(formModel->layout()->count(), form->layout()->count());
        // (grid_X is nested in the group's gridLayout_2, as the .ui has it)
        Fw::Widget* groupX = named(QStringLiteral("group_X"));
        QCOMPARE(groupX->layout()->objectName(), QStringLiteral("gridLayout_2"));
        auto realGrid = form->findChild<QGridLayout*>(QStringLiteral("grid_X"));
        QVERIFY(realGrid);
        Fw::Layout* modelGrid = groupX->findLayout(QStringLiteral("grid_X"));
        QVERIFY(modelGrid);
        QCOMPARE(modelGrid->parentLayout(), groupX->layout());
        Fw::Widget* inputXx = named(QStringLiteral("input_X_x"));
        int idx = modelGrid->indexOf(inputXx);
        QVERIFY(idx >= 0);
        int row = 0, col = 0, rs = 0, cs = 0;
        realGrid->getItemPosition(realGrid->indexOf(form->findChild<QWidget*>(
                                      QStringLiteral("input_X_x"))),
                                  &row, &col, &rs, &cs);
        QCOMPARE(modelGrid->itemAt(idx)->position, (QVariantList {row, col, rs, cs}));
        QCOMPARE(row, 0);
        QCOMPARE(col, 1);
        // the hand-built box: the stretch and the alignment on the combo,
        // the stack's pages, the hidden one sent as not visible
        Fw::Widget* handModel = named(QStringLiteral("handEdit"))->parentWidget();
        QCOMPARE(handModel->layout()->objectName(), QStringLiteral("handLayout"));
        QVariantMap handSpec = handModel->layout()->spec();
        QVariantList handItems = handSpec.value(QStringLiteral("items")).toList();
        QCOMPARE(handItems.size(), 3);
        QCOMPARE(handItems.at(1).toMap().value(QStringLiteral("stretch")).toInt(), 2);
        QCOMPARE(handItems.at(1).toMap().value(QStringLiteral("align")).toInt(),
                 static_cast<int>(Qt::AlignRight));
        QVERIFY(!handItems.at(0).toMap().contains(QStringLiteral("stretch")));
        QCOMPARE(named(QStringLiteral("handCombo"))->property("items").toStringList(),
                 (QStringList {QStringLiteral("alpha"), QStringLiteral("beta")}));
        const QString stackId = idOf(QStringLiteral("handStack"));
        QCOMPARE(items(store.snapshot(stackId)).size(), 2);
        QCOMPARE(named(QStringLiteral("page0"))->property("visible").toBool(), true);
        QCOMPARE(named(QStringLiteral("page1"))->property("visible").toBool(), false);

        // the opens: children before their container, the root last, every
        // widget with its parent
        QStringList opened;
        for (int i = 0; i < messages.count(); ++i) {
            if (messages.at(i).at(1).toString() == QLatin1String("open")) {
                opened.append(messages.at(i).at(0).toString());
                QVariantMap content = messages.at(i).at(2).toMap();
                if (opened.last() != QLatin1String("panel"))
                    QVERIFY2(content.contains(QStringLiteral("parent")), qPrintable(opened.last()));
            }
        }
        QCOMPARE(opened.last(), rootId);
        QVERIFY(opened.indexOf(idOf(QStringLiteral("input_X_x")))
                < opened.indexOf(idOf(QStringLiteral("group_X"))));
        QVERIFY(opened.indexOf(idOf(QStringLiteral("group_X")))
                < opened.indexOf(store.idOf(formModel)));
        QVERIFY(opened.indexOf(store.idOf(formModel)) < opened.indexOf(boxId));
        QVERIFY(opened.indexOf(buttonsId) < opened.indexOf(rootId));
        QVERIFY(!opened.contains(QString()));

        // a setText from code arrives as one update, on the widget's own
        // evidence (its repaint)
        messages.clear();
        QLabel* realLabel = form->findChild<QLabel*>(QStringLiteral("label_n_X"));
        realLabel->setText(QStringLiteral("Count X"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        mirror.flush();
        const QString labelId = idOf(QStringLiteral("label_n_X"));
        int labelUpdates = 0;
        for (int i = 0; i < messages.count(); ++i) {
            if (messages.at(i).at(0).toString() == labelId
                && messages.at(i).at(1).toString() == QLatin1String("update")) {
                ++labelUpdates;
                QCOMPARE(messages.at(i).at(2).toMap().value(QStringLiteral("q_text")).toString(),
                         QStringLiteral("Count X"));
            }
        }
        QCOMPARE(labelUpdates, 1);
        QCOMPARE(named(QStringLiteral("label_n_X"))->property("text").toString(),
                 QStringLiteral("Count X"));

        // a client's `text` write fires the form's textEdited slot
        QSignalSpy edited(edit, &QLineEdit::textEdited);
        QSignalSpy finished(edit, &QLineEdit::editingFinished);
        const QString editId = idOf(QStringLiteral("handEdit"));
        QVERIFY(store.applyUpdate(editId, QVariantMap {{QStringLiteral("q_text"),
                                                         QStringLiteral("typed")}}, 7));
        QCOMPARE(edit->text(), QStringLiteral("typed"));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.at(0).at(0).toString(), QStringLiteral("typed"));
        QVERIFY(store.applyCustom(editId, QVariantMap {{QStringLiteral("event"),
                                                         QStringLiteral("editingFinished")}}, 7));
        QCOMPARE(finished.count(), 1);
        // a combo index from a client fires `activated` as a pick would
        QSignalSpy activated(combo, qOverload<int>(&QComboBox::activated));
        QVERIFY(store.applyUpdate(idOf(QStringLiteral("handCombo")),
                                  QVariantMap {{QStringLiteral("q_currentIndex"), 1}}, 7));
        QCOMPARE(combo->currentIndex(), 1);
        QCOMPARE(activated.count(), 1);

        // a hidden page shows on the stack's currentIndex, and the change
        // comes back from the widgets under the desktop's origin
        messages.clear();
        QVERIFY(store.applyUpdate(stackId, QVariantMap {{QStringLiteral("q_currentIndex"), 1}}, 7));
        QCOMPARE(stack->currentIndex(), 1);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        mirror.flush();
        QCOMPARE(named(QStringLiteral("page1"))->property("visible").toBool(), true);
        QCOMPARE(named(QStringLiteral("page0"))->property("visible").toBool(), false);
        bool page1Shown = false;
        for (int i = 0; i < messages.count(); ++i) {
            if (messages.at(i).at(0).toString() == idOf(QStringLiteral("page1"))
                && messages.at(i).at(2).toMap().value(QStringLiteral("q_visible")).toBool()) {
                page1Shown = true;
                QCOMPARE(messages.at(i).at(3).toULongLong(), 0ULL);
            }
        }
        QVERIFY(page1Shown);

        // the dialog's buttons: a client's reject reaches the button box
        QSignalSpy rejected(buttons, &QDialogButtonBox::rejected);
        QVERIFY(store.applyCustom(rootId, QVariantMap {{QStringLiteral("event"),
                                                         QStringLiteral("reject")}}, 7));
        QCOMPARE(rejected.count(), 1);
        QSignalSpy accepted(buttons, &QDialogButtonBox::accepted);
        QVERIFY(store.applyCustom(rootId, QVariantMap {{QStringLiteral("event"), QStringLiteral("clicked")},
                                                       {QStringLiteral("args"), QVariantList {
                                                           static_cast<int>(QDialogButtonBox::Ok)}}},
                                  7));
        QCOMPARE(accepted.count(), 1);

        // structure: a widget added to the hand-built box arrives as an open
        // and one layout update of its container
        messages.clear();
        const int walks = mirror.rebuildCount();
        auto extra = new QLabel(QStringLiteral("extra"), hand);
        extra->setObjectName(QStringLiteral("handExtra"));
        vbox->addWidget(extra);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        if (mirror.rebuildCount() == walks)
            mirror.rebuild();
        QVERIFY(named(QStringLiteral("handExtra")));
        int opens = 0, layoutUpdates = 0;
        for (int i = 0; i < messages.count(); ++i) {
            const QString method = messages.at(i).at(1).toString();
            if (method == QLatin1String("open"))
                ++opens;
            if (messages.at(i).at(0).toString() == store.idOf(handModel)
                && messages.at(i).at(2).toMap().contains(QStringLiteral("layoutSpec")))
                ++layoutUpdates;
        }
        QCOMPARE(opens, 1);
        QCOMPARE(layoutUpdates, 1);
        QCOMPARE(handModel->layout()->count(), 4);

        // hide: one close for the root, none for the children; the real
        // widgets untouched
        messages.clear();
        mirror.hide();
        QStringList closes;
        for (int i = 0; i < messages.count(); ++i)
            if (messages.at(i).at(1).toString() == QLatin1String("close"))
                closes.append(messages.at(i).at(0).toString());
        QCOMPARE(closes, QStringList {rootId});
        QVERIFY(!store.object(editId));
        QVERIFY(!store.isAdopted(editId));
        QVERIFY(mirror.panelId().isEmpty());
        QCOMPARE(edit->text(), QStringLiteral("typed"));
        QCOMPARE(form->findChild<QLabel*>(QStringLiteral("label_n_X"))->text(),
                 QStringLiteral("Count X"));
        mirror.stop();
        QVERIFY(!store.object(QStringLiteral("panel")));
        QCOMPARE(store.count(), 0);
    }

    void test_panelMirrorItems()
    {
        // docs/Sandbox.md 7.19, M2: an item view's rows reflected (the
        // real model's rows as the model's tree, kept current as item ops,
        // a client's op landing in the real model), and the picture
        // fallback (a custom-painted leaf as a label's pixmap by image id,
        // re-sent on change only), button icons and label pixmaps by id
        Fw::Store& store = Fw::Store::instance();
        store.reset();
        Fw::ImageStore& images = Fw::ImageStore::instance();
        images.clear();
        Fw::PanelMirror& mirror = Fw::PanelMirror::instance();
        QSignalSpy messages(&store, &Fw::Store::message);
        auto state = [](const QVariantMap& snap, const char* key) {
            return snap.value(QStringLiteral("state")).toMap().value(QStringLiteral("q_")
                                                                    + QLatin1String(key));
        };
        auto customs = [&messages](const QString& id, const QString& kind) {
            QList<QVariantMap> out;
            for (int i = 0; i < messages.count(); ++i) {
                if (messages.at(i).at(0).toString() != id
                    || messages.at(i).at(1).toString() != QLatin1String("custom"))
                    continue;
                QVariantMap content = messages.at(i).at(2).toMap();
                if (kind.isEmpty() || content.value(QStringLiteral("item")).toString() == kind)
                    out.append(content);
            }
            return out;
        };
        auto updates = [&messages](const QString& id, const char* key) {
            QList<QVariantMap> out;
            for (int i = 0; i < messages.count(); ++i) {
                if (messages.at(i).at(0).toString() != id
                    || messages.at(i).at(1).toString() != QLatin1String("update"))
                    continue;
                QVariantMap content = messages.at(i).at(2).toMap();
                if (content.contains(QStringLiteral("q_") + QLatin1String(key)))
                    out.append(content);
            }
            return out;
        };
        auto settle = [&mirror]() {
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            mirror.flush();
        };

        // the box: a checkable list with an icon and a tool tip, a two-
        // column tree with an expanded parent, a painted leaf, a button
        // with an icon, a label with a pixmap
        auto hand = new QWidget;
        auto vbox = new QVBoxLayout(hand);
        auto list = new QListWidget(hand);
        list->setObjectName(QStringLiteral("list"));
        QPixmap red(16, 16);
        red.fill(Qt::red);
        for (const char* text : {"alpha", "beta", "gamma"}) {
            auto item = new QListWidgetItem(QString::fromUtf8(text), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        list->item(1)->setCheckState(Qt::Checked);
        list->item(2)->setIcon(QIcon(red));
        list->item(2)->setToolTip(QStringLiteral("third"));
        auto tree = new QTreeWidget(hand);
        tree->setObjectName(QStringLiteral("tree"));
        tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
        auto top = new QTreeWidgetItem(tree, {QStringLiteral("top"), QStringLiteral("1")});
        new QTreeWidgetItem(top, {QStringLiteral("child"), QStringLiteral("2")});
        tree->expandItem(top);
        auto painted = new PaintedLeaf(hand);
        auto button = new QPushButton(QStringLiteral("Pick"), hand);
        button->setObjectName(QStringLiteral("iconButton"));
        button->setIcon(QIcon(red));
        auto picture = new QLabel(hand);
        picture->setObjectName(QStringLiteral("pictureLabel"));
        picture->setPixmap(red);
        vbox->addWidget(list);
        vbox->addWidget(tree);
        vbox->addWidget(painted);
        vbox->addWidget(button);
        vbox->addWidget(picture);
        auto box = new Gui::TaskView::TaskBox(QStringLiteral("Items"), true, nullptr);
        box->groupLayout()->addWidget(hand);
        QWidget host;
        auto hostLay = new QVBoxLayout(&host);
        hostLay->addWidget(box);
        host.show();
        QCoreApplication::processEvents();

        mirror.show(QStringLiteral("ItemsDialog"), {box}, nullptr);
        const QString rootId = mirror.panelId();
        QVERIFY(!rootId.isEmpty());
        Fw::Widget* listModel = mirror.modelOf(list);
        Fw::Widget* treeModel = mirror.modelOf(tree);
        QVERIFY(listModel && treeModel);
        auto listView = qobject_cast<Fw::ItemView*>(listModel);
        auto treeView = qobject_cast<Fw::ItemView*>(treeModel);
        QVERIFY(listView && treeView);
        const QString listId = store.idOf(listModel);
        const QString treeId = store.idOf(treeModel);
        QCOMPARE(listModel->modelName(), QStringLiteral("QListWidgetModel"));
        QCOMPARE(listModel->qtClass(), QStringLiteral("QListWidget"));
        QVERIFY(Gui::FwQt::View::of(listModel)->isReflecting());

        // the rows: the real model's, in the snapshot as `items`
        QVariantMap listSnap = store.snapshot(listId);
        QVariantList rows = listSnap.value(QStringLiteral("items")).toList();
        QCOMPARE(rows.size(), 3);
        auto cell0 = [](const QVariant& row) {
            return row.toMap().value(QStringLiteral("cells")).toList().value(0).toMap();
        };
        QCOMPARE(cell0(rows.at(0)).value(QStringLiteral("text")).toString(), QStringLiteral("alpha"));
        QCOMPARE(cell0(rows.at(0)).value(QStringLiteral("check")).toInt(), int(Qt::Unchecked));
        QCOMPARE(cell0(rows.at(1)).value(QStringLiteral("check")).toInt(), int(Qt::Checked));
        const QString iconId = cell0(rows.at(2)).value(QStringLiteral("icon")).toString();
        QVERIFY2(iconId.startsWith(QLatin1String("img:")), qPrintable(iconId));
        QVERIFY(images.contains(iconId));
        QCOMPARE(cell0(rows.at(2)).value(QStringLiteral("toolTip")).toString(), QStringLiteral("third"));
        QVERIFY(rows.at(0).toMap().value(QStringLiteral("flags")).toInt() & Qt::ItemIsUserCheckable);
        const int id0 = rows.at(0).toMap().value(QStringLiteral("id")).toInt();
        const int id1 = rows.at(1).toMap().value(QStringLiteral("id")).toInt();
        QVERIFY(id0 > 0 && id1 > 0 && id0 != id1);
        QCOMPARE(Gui::FwQt::View::of(listModel)->indexOf(id1).row(), 1);
        // the tree: the header, a parent expanded with its child
        QVariantMap treeSnap = store.snapshot(treeId);
        QCOMPARE(state(treeSnap, "columns").toStringList(),
                 (QStringList {QStringLiteral("Name"), QStringLiteral("Value")}));
        QCOMPARE(state(treeSnap, "columnCount").toInt(), 2);
        QVariantList treeRows = treeSnap.value(QStringLiteral("items")).toList();
        QCOMPARE(treeRows.size(), 1);
        QVariantMap topRow = treeRows.at(0).toMap();
        QCOMPARE(topRow.value(QStringLiteral("cells")).toList().size(), 2);
        QCOMPARE(topRow.value(QStringLiteral("expanded")).toBool(), true);
        QVariantList kids = topRow.value(QStringLiteral("children")).toList();
        QCOMPARE(kids.size(), 1);
        QCOMPARE(cell0(kids.at(0)).value(QStringLiteral("text")).toString(), QStringLiteral("child"));
        const int topId = topRow.value(QStringLiteral("id")).toInt();
        const int childId = kids.at(0).toMap().value(QStringLiteral("id")).toInt();

        // the open carried the rows, and nothing about the list went out
        // before its open
        bool listOpened = false;
        for (int i = 0; i < messages.count(); ++i) {
            if (messages.at(i).at(0).toString() != listId)
                continue;
            if (!listOpened) {
                QCOMPARE(messages.at(i).at(1).toString(), QStringLiteral("open"));
                QCOMPARE(messages.at(i).at(2).toMap().value(QStringLiteral("items")).toList().size(),
                         3);
                listOpened = true;
            }
        }
        QVERIFY(listOpened);

        // a client's check lands in the real item and fires the panel's
        // itemChanged; the op fans out under the client's origin, once
        messages.clear();
        QSignalSpy changed(list, &QListWidget::itemChanged);
        QVariantMap setOp {{QStringLiteral("item"), QStringLiteral("set")},
                           {QStringLiteral("id"), id0},
                           {QStringLiteral("col"), 0},
                           {QStringLiteral("cell"), QVariantMap {{QStringLiteral("check"), 2}}}};
        QVERIFY(store.applyCustom(listId, setOp, 7));
        QCOMPARE(list->item(0)->checkState(), Qt::Checked);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(listView->checkState(id0, 0), int(Qt::Checked));
        QList<QVariantMap> sets = customs(listId, QStringLiteral("set"));
        QCOMPARE(sets.size(), 1);
        QCOMPARE(messages.at(0).at(3).toULongLong(), 7ULL);

        // the panel's own changes arrive as item ops: an added row, a
        // text set, a row taken, a child added under its parent
        messages.clear();
        list->addItem(QStringLiteral("delta"));
        QList<QVariantMap> inserts = customs(listId, QStringLiteral("insert"));
        QCOMPARE(inserts.size(), 1);
        QVariantList inserted = inserts.at(0).value(QStringLiteral("rows")).toList();
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(cell0(inserted.at(0)).value(QStringLiteral("text")).toString(),
                 QStringLiteral("delta"));
        QCOMPARE(inserts.at(0).value(QStringLiteral("index")).toInt(), 3);
        const int deltaId = inserted.at(0).toMap().value(QStringLiteral("id")).toInt();
        QCOMPARE(listView->rowCount(), 4);
        QCOMPARE(messages.at(0).at(3).toULongLong(), 0ULL);
        messages.clear();
        list->item(0)->setText(QStringLiteral("alpha2"));
        sets = customs(listId, QStringLiteral("set"));
        QCOMPARE(sets.size(), 1);
        QCOMPARE(sets.at(0).value(QStringLiteral("id")).toInt(), id0);
        QCOMPARE(sets.at(0).value(QStringLiteral("cell")).toMap().value(QStringLiteral("text"))
                     .toString(),
                 QStringLiteral("alpha2"));
        QCOMPARE(listView->text(id0, 0), QStringLiteral("alpha2"));
        messages.clear();
        delete list->takeItem(3);
        QList<QVariantMap> removes = customs(listId, QStringLiteral("remove"));
        QCOMPARE(removes.size(), 1);
        QCOMPARE(removes.at(0).value(QStringLiteral("id")).toInt(), deltaId);
        QCOMPARE(listView->rowCount(), 3);
        QVERIFY(!listView->row(deltaId));
        messages.clear();
        new QTreeWidgetItem(top, {QStringLiteral("child2"), QStringLiteral("3")});
        inserts = customs(treeId, QStringLiteral("insert"));
        QCOMPARE(inserts.size(), 1);
        QCOMPARE(inserts.at(0).value(QStringLiteral("parent")).toInt(), topId);
        QCOMPARE(treeView->rowCount(topId), 2);
        // a collapse from the widget reaches the model row; a client's
        // expand reaches the widget
        tree->collapseItem(top);
        QCOMPARE(treeView->isExpanded(topId), false);
        QVariantMap rowOp {{QStringLiteral("item"), QStringLiteral("row")},
                           {QStringLiteral("id"), topId},
                           {QStringLiteral("row"), QVariantMap {{QStringLiteral("expanded"), true}}}};
        QVERIFY(store.applyCustom(treeId, rowOp, 7));
        QVERIFY(top->isExpanded());
        QVERIFY(treeView->row(childId));
        // a row hidden by the view has no signal: the flush finds it
        messages.clear();
        list->setRowHidden(1, true);
        settle();
        QList<QVariantMap> rowOps = customs(listId, QStringLiteral("row"));
        QCOMPARE(rowOps.size(), 1);
        QCOMPARE(rowOps.at(0).value(QStringLiteral("id")).toInt(), id1);
        QCOMPARE(rowOps.at(0).value(QStringLiteral("row")).toMap().value(QStringLiteral("hidden"))
                     .toBool(),
                 true);
        QVERIFY(listView->row(id1)->hidden);
        // a clear is a reset: one clear op, then the refill
        messages.clear();
        list->clear();
        QCOMPARE(customs(listId, QStringLiteral("clear")).size(), 1);
        QCOMPARE(listView->rowCount(), 0);
        list->addItems({QStringLiteral("x"), QStringLiteral("y")});
        QCOMPARE(listView->rowCount(), 2);
        QCOMPARE(store.snapshot(listId).value(QStringLiteral("items")).toList().size(), 2);

        // the picture: a label model with the real class, its pixmap an
        // image id whose PNG is the widget's size
        Fw::Widget* paintedModel = mirror.modelOf(painted);
        QVERIFY(paintedModel);
        QVERIFY(mirror.isPicture(painted));
        QCOMPARE(mirror.pictureCount(), 1);
        QCOMPARE(paintedModel->modelName(), QStringLiteral("QLabelModel"));
        QCOMPARE(paintedModel->qtClass(), QStringLiteral("PaintedLeaf"));
        const QString paintedId = store.idOf(paintedModel);
        const QString pix1 = paintedModel->property("pixmap").toString();
        QVERIFY2(pix1.startsWith(QLatin1String("img:")), qPrintable(pix1));
        int w = 0, h = 0;
        const QByteArray png = images.png(pix1, &w, &h);
        QVERIFY(!png.isEmpty());
        QCOMPARE(QSize(w, h), painted->size());
        QImage decoded = QImage::fromData(png, "PNG");
        QCOMPARE(decoded.size(), painted->size());
        QCOMPARE(decoded.pixelColor(5, 5), QColor(Qt::red));
        // a repaint with nothing changed sends nothing; a change sends
        // one update with a new id (the rate cap defers, never drops)
        QTest::qWait(mirror.grabIntervalMs() + 20);
        messages.clear();
        const int grabs = mirror.grabCount();
        painted->update();
        QTest::qWait(mirror.grabIntervalMs() + 50);
        settle();
        QVERIFY(mirror.grabCount() > grabs);
        QCOMPARE(updates(paintedId, "pixmap").size(), 0);
        QCOMPARE(paintedModel->property("pixmap").toString(), pix1);
        painted->color = Qt::blue;
        painted->update();
        QTest::qWait(mirror.grabIntervalMs() + 50);
        settle();
        QList<QVariantMap> pixUpdates = updates(paintedId, "pixmap");
        QCOMPARE(pixUpdates.size(), 1);
        const QString pix2 = pixUpdates.at(0).value(QStringLiteral("q_pixmap")).toString();
        QVERIFY(pix2 != pix1);
        QCOMPARE(QImage::fromData(images.png(pix2), "PNG").pixelColor(5, 5), QColor(Qt::blue));

        // a button's icon and a label's pixmap travel by id, encoded once
        Fw::Widget* buttonModel = mirror.modelOf(button);
        const QString buttonIcon = buttonModel->property("icon").toString();
        QVERIFY2(buttonIcon.startsWith(QLatin1String("img:")), qPrintable(buttonIcon));
        Fw::Widget* labelModel = mirror.modelOf(picture);
        QCOMPARE(labelModel->modelName(), QStringLiteral("QLabelModel"));
        QVERIFY(labelModel->property("pixmap").toString().startsWith(QLatin1String("img:")));
        const int encoded = images.encoded();
        button->update();
        picture->update();
        settle();
        QCOMPARE(images.encoded(), encoded);
        QCOMPARE(buttonModel->property("icon").toString(), buttonIcon);

        // the image op: the PNG by id, UnknownImage for a stranger
        Gui::installSceneWidgetOps();
        auto control = [](const QString& json) {
            return QJsonDocument::fromJson(QByteArray(Gui::handleSceneControlRequest(
                                                          json.toStdString(), std::string(),
                                                          false, 7)
                                                          .c_str()))
                .object();
        };
        QJsonObject reply = control(QStringLiteral("{\"op\":\"widgets.image\",\"id\":1,\"name\":\"%1\"}")
                                        .arg(pix2));
        QVERIFY2(reply.value(QLatin1String("ok")).toBool(), qPrintable(QJsonDocument(reply).toJson()));
        QCOMPARE(reply.value(QLatin1String("format")).toString(), QStringLiteral("png"));
        QCOMPARE(reply.value(QLatin1String("width")).toInt(), painted->width());
        QCOMPARE(QByteArray::fromBase64(reply.value(QLatin1String("data")).toString().toLatin1()),
                 images.png(pix2));
        reply = control(QStringLiteral("{\"op\":\"widgets.image\",\"id\":2,\"name\":\"img:nope\"}"));
        QVERIFY(!reply.value(QLatin1String("ok")).toBool());

        mirror.hide();
        QCOMPARE(mirror.pictureCount(), 0);
        mirror.stop();
        QCOMPARE(store.count(), 0);
    }
};

// NOLINTEND(readability-magic-numbers)

QTEST_MAIN(testFormWidgets)

#include "FormWidgets.moc"
