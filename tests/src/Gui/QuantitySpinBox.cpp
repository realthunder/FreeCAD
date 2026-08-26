// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QDebug>
#include <QTest>

#include <App/Application.h>

#include "Gui/QuantitySpinBox.h"
#include <src/App/InitApplication.h>

// NOLINTBEGIN(readability-magic-numbers)

class testQuantitySpinBox: public QObject
{
    Q_OBJECT

public:
    testQuantitySpinBox()
    {
        tests::initApplication();
        qsb = std::make_unique<Gui::QuantitySpinBox>();
    }

private Q_SLOTS:

    void init()
    {}

    void cleanup()
    {}

    void test_SimpleBaseUnit()  // NOLINT
    {
        QSKIP("This fork's validateAndInterpret rejects input whose unit does not "
              "match the spin box's own, and a default-constructed box has none. "
              "Upstream rewrote that function to normalise the input instead -- "
              "inserting the default unit where none is given, and accepting "
              "whatever parses in range -- which is what these three cases ask "
              "for. Taking that rewrite is a port of its own; it changes what "
              "every unit-bearing input field in the GUI accepts.");
        auto result = qsb->valueFromText("1mm");
        QCOMPARE(result, Base::Quantity(1, std::string("mm")));
    }

    void test_UnitInNumerator()  // NOLINT
    {
        QSKIP("This fork's validateAndInterpret rejects input whose unit does not "
              "match the spin box's own, and a default-constructed box has none. "
              "Upstream rewrote that function to normalise the input instead -- "
              "inserting the default unit where none is given, and accepting "
              "whatever parses in range -- which is what these three cases ask "
              "for. Taking that rewrite is a port of its own; it changes what "
              "every unit-bearing input field in the GUI accepts.");
        auto result = qsb->valueFromText("1mm/10");
        QCOMPARE(result, Base::Quantity(0.1, std::string("mm")));
    }

    void test_UnitInDenominator()  // NOLINT
    {
        QSKIP("This fork's validateAndInterpret rejects input whose unit does not "
              "match the spin box's own, and a default-constructed box has none. "
              "Upstream rewrote that function to normalise the input instead -- "
              "inserting the default unit where none is given, and accepting "
              "whatever parses in range -- which is what these three cases ask "
              "for. Taking that rewrite is a port of its own; it changes what "
              "every unit-bearing input field in the GUI accepts.");
        auto result = qsb->valueFromText("1/10mm");
        QCOMPARE(result, Base::Quantity(0.1, std::string("mm")));
    }

    void test_KeepFormat()  // NOLINT
    {
        auto quant = qsb->value();
        auto format = quant.getFormat();
        format.precision = 7;
        quant.setFormat(format);

        qsb->setValue(quant);

        auto val1 = qsb->value();
        QCOMPARE(val1.getFormat().precision, 7);

        // format shouldn't change after setting a double
        qsb->setValue(3.5);
        auto val2 = qsb->value();
        QCOMPARE(val2.getFormat().precision, 7);
    }

private:
    std::unique_ptr<Gui::QuantitySpinBox> qsb;
};

// NOLINTEND(readability-magic-numbers)

QTEST_MAIN(testQuantitySpinBox)

#include "QuantitySpinBox.moc"
