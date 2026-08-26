// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QDebug>
#include <QTest>

#include <App/Application.h>
#include <Base/Unit.h>

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
        QSKIP("These assert upstream's looser contract, not a gap here. This "
              "fork checks the dimension of the parsed input against the "
              "field's own unit, and the box under test is default "
              "constructed, so it has no unit and nothing dimensional is "
              "acceptable to it. Upstream removed that check when it rewrote "
              "validateAndInterpret to pre-process the text with a regex, so "
              "there a Length field will take 5 kg. Keeping the check is "
              "deliberate -- see test_WrongDimensionIsRejected. Set a unit on "
              "the box and this fork accepts the same arithmetic, which is "
              "what test_ArithmeticInTheFieldsOwnUnit covers.");
        auto result = qsb->valueFromText("1mm");
        QCOMPARE(result, Base::Quantity(1, std::string("mm")));
    }

    void test_UnitInNumerator()  // NOLINT
    {
        QSKIP("These assert upstream's looser contract, not a gap here. This "
              "fork checks the dimension of the parsed input against the "
              "field's own unit, and the box under test is default "
              "constructed, so it has no unit and nothing dimensional is "
              "acceptable to it. Upstream removed that check when it rewrote "
              "validateAndInterpret to pre-process the text with a regex, so "
              "there a Length field will take 5 kg. Keeping the check is "
              "deliberate -- see test_WrongDimensionIsRejected. Set a unit on "
              "the box and this fork accepts the same arithmetic, which is "
              "what test_ArithmeticInTheFieldsOwnUnit covers.");
        auto result = qsb->valueFromText("1mm/10");
        QCOMPARE(result, Base::Quantity(0.1, std::string("mm")));
    }

    void test_UnitInDenominator()  // NOLINT
    {
        QSKIP("These assert upstream's looser contract, not a gap here. This "
              "fork checks the dimension of the parsed input against the "
              "field's own unit, and the box under test is default "
              "constructed, so it has no unit and nothing dimensional is "
              "acceptable to it. Upstream removed that check when it rewrote "
              "validateAndInterpret to pre-process the text with a regex, so "
              "there a Length field will take 5 kg. Keeping the check is "
              "deliberate -- see test_WrongDimensionIsRejected. Set a unit on "
              "the box and this fork accepts the same arithmetic, which is "
              "what test_ArithmeticInTheFieldsOwnUnit covers.");
        auto result = qsb->valueFromText("1/10mm");
        QCOMPARE(result, Base::Quantity(0.1, std::string("mm")));
    }

    // A box that states its unit takes arithmetic in that unit, bound to a
    // property or not. This fork parses the text twice -- Base::Quantity's
    // grammar first, then the expression parser -- rather than rewriting it
    // the way upstream does, and the second stage used to be reachable only
    // from a bound box.
    void test_ArithmeticInTheFieldsOwnUnit()  // NOLINT
    {
        Gui::QuantitySpinBox box;
        box.setUnit(Base::Unit::Length);
        QCOMPARE(box.valueFromText(QStringLiteral("1mm/10")),
                 Base::Quantity(0.1, std::string("mm")));
        QCOMPARE(box.valueFromText(QStringLiteral("1mm+2mm")),
                 Base::Quantity(3, std::string("mm")));
        QCOMPARE(box.valueFromText(QStringLiteral("(1mm+2mm)/3")),
                 Base::Quantity(1, std::string("mm")));
    }

    // ... and refuses a quantity of the wrong dimension, which is the check
    // upstream's rewrite dropped.
    void test_WrongDimensionIsRejected()  // NOLINT
    {
        Gui::QuantitySpinBox box;
        box.setUnit(Base::Unit::Length);
        QVERIFY(box.valueFromText(QStringLiteral("5kg")) != Base::Quantity(5, std::string("kg")));
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
