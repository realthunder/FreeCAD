// SPDX-License-Identifier: LGPL-2.1-or-later
/* The host widget layer (docs/Sandbox.md 7.12, H0): the property core,
 * the class set, the layouts, the .ui generator's output, and the Qt
 * backend binding uic's widgets to the models.  Needs a QApplication
 * (the widgets) and the App (Gui::InputField reads preferences); no
 * main window, no Gui::Application. */

#include <QDebug>
#include <QGridLayout>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QTest>

#include <QLabel>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Placement.h>
#include <src/App/InitApplication.h>

#include "Gui/Camera.h"
#include "Gui/Fw/FwQtView.h"
#include "Gui/Fw/FwWidgets.h"
#include "Gui/InputField.h"
#include "Gui/QuantitySpinBox.h"
#include "Gui/TaskView/TaskOrientation.h"
#include "fwui_TaskPanel_OrthoArray.h"

namespace Fw = Gui::Fw;

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
        QCOMPARE(w->modelName(), QStringLiteral("QWidgetModel"));
        QCOMPARE(w->qtClass(), QStringLiteral("QTreeWidget"));
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
};

// NOLINTEND(readability-magic-numbers)

QTEST_MAIN(testFormWidgets)

#include "FormWidgets.moc"
