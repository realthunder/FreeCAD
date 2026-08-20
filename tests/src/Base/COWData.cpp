// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include <Base/COWData.h>

// The copy-on-write containers moved from Gui/Inventor to Base so App and
// Part can share storage the way the render cache does. These tests pin
// what "shared" and "detached" mean, and each of the defects the move
// found (docs/PythonValueBindings.md).

namespace
{

/// A value with the combine() a COWMap::combine expects of its mapped type
struct Combinable
{
    int value {0};

    void combine(const Combinable &other) { value += other.value; }

    bool operator==(const Combinable &other) const { return value == other.value; }
};

FC_COW_SCOPE(TestScopeA);
FC_COW_SCOPE(TestScopeB);

}  // namespace

// ---------------------------------------------------------------------
// COWValue: one shared value

TEST(COWValue, anEmptyHolderReadsAsTheDefault)
{
    Base::COWValue<std::vector<int>> value;
    EXPECT_TRUE(value.isNull());
    EXPECT_FALSE(value.isShared());
    // No allocation, and the read still answers something
    EXPECT_TRUE(value.get().empty());
}

TEST(COWValue, aCopySharesUntilOneOfThemWrites)
{
    Base::COWValue<std::vector<int>> first;
    first.edit().push_back(1);
    EXPECT_FALSE(first.isShared());

    Base::COWValue<std::vector<int>> second = first;
    EXPECT_TRUE(first.isShared());
    EXPECT_TRUE(second.isShared());
    EXPECT_EQ(first.useCount(), 2);

    // Reading never detaches, however often it is asked
    EXPECT_EQ(second.get().size(), 1U);
    EXPECT_EQ(first.useCount(), 2);

    second.edit().push_back(2);
    EXPECT_FALSE(first.isShared());
    EXPECT_FALSE(second.isShared());
    EXPECT_EQ(first.get().size(), 1U);
    EXPECT_EQ(second.get().size(), 2U);
}

TEST(COWValue, equalityLooksAtTheStorageFirstAndTheValueSecond)
{
    Base::COWValue<std::vector<int>> first(std::vector<int> {1, 2});
    Base::COWValue<std::vector<int>> shared = first;
    EXPECT_TRUE(first == shared);

    Base::COWValue<std::vector<int>> equal(std::vector<int> {1, 2});
    EXPECT_FALSE(first.isSameData(equal));
    EXPECT_TRUE(first == equal);

    Base::COWValue<std::vector<int>> other(std::vector<int> {3});
    EXPECT_TRUE(first != other);
}

// ---------------------------------------------------------------------
// COWVector

TEST(COWVector, aWriteDetachesAndLeavesTheOtherHolderAlone)
{
    Base::COWVector<int> first;
    first.append(1);
    first.append(2);

    Base::COWVector<int> second = first;
    EXPECT_TRUE(first.isShared());

    second.set(0, 42);
    EXPECT_FALSE(first.isShared());
    EXPECT_EQ(first.get(0), 1);
    EXPECT_EQ(second.get(0), 42);
}

TEST(COWVector, writingOutsideTheVectorIsANoOpRatherThanAWriteThroughNull)
{
    // use_count() is 0 on a holder with no storage, so the detach branch
    // is skipped: without the range guards these went through a null
    // pointer in any build with assertions compiled out.
    Base::COWVector<int> empty;
    empty.set(0, 7);
    empty.erase(0);
    EXPECT_EQ(empty.size(), 0);
    EXPECT_EQ(empty.at(0), nullptr);
    EXPECT_EQ(empty.get(0), 0);
    EXPECT_EQ(empty.back(), 0);
    EXPECT_EQ(empty.front(), 0);

    Base::COWVector<int> one;
    one.append(5);
    one.set(3, 7);
    one.erase(3);
    EXPECT_EQ(one.size(), 1);
    EXPECT_EQ(one.get(0), 5);
    EXPECT_EQ(one.at(3), nullptr);
}

TEST(COWVector, atDetachesBeforeHandingOutAPointer)
{
    Base::COWVector<int> first;
    first.append(1);
    Base::COWVector<int> second = first;

    *second.at(0) = 9;
    EXPECT_EQ(first.get(0), 1);
    EXPECT_EQ(second.get(0), 9);
}

TEST(COWVector, appendingToNothingTakesTheOtherStorageRatherThanCopyingIt)
{
    Base::COWVector<int> source;
    source.append(1);
    source.append(2);

    Base::COWVector<int> target;
    target.append(source);
    EXPECT_TRUE(source.isShared());
    EXPECT_EQ(target.size(), 2);

    Base::COWVector<int> second;
    second.append(3);
    second.append(source);
    EXPECT_EQ(second.size(), 3);
    EXPECT_EQ(second.get(2), 2);
}

TEST(COWVector, compareAndSetAppendsAtTheEndAndReportsRealChangesOnly)
{
    Base::COWVector<int> values;
    EXPECT_TRUE(values.compareAndSet(0, 5));
    EXPECT_EQ(values.size(), 1);
    EXPECT_FALSE(values.compareAndSet(0, 5));
    EXPECT_TRUE(values.compareAndSet(0, 6));
    EXPECT_FALSE(values.compareAndSet(9, 1));
}

TEST(COWVector, resizeAndReserveDetachBeforeTheyTouchAnything)
{
    Base::COWVector<int> first;
    first.append(1);
    Base::COWVector<int> second = first;

    second.resize(4);
    EXPECT_EQ(first.size(), 1);
    EXPECT_EQ(second.size(), 4);

    Base::COWVector<int> third = second;
    third.reserve(64);
    EXPECT_FALSE(second.isShared());
    EXPECT_EQ(second.size(), 4);
}

TEST(COWVector, movingAValueInAvoidsTheCopyAppendWouldMake)
{
    Base::COWVector<std::string> values;
    std::string moved = "a string long enough not to be stored inside itself";
    const std::string expected = moved;
    values.append(std::move(moved));
    EXPECT_EQ(values.size(), 1);
    EXPECT_EQ(values.get(0), expected);

    values.emplace_back(3, 'x');
    EXPECT_EQ(values.get(1), "xxx");
}

// ---------------------------------------------------------------------
// COWMap

TEST(COWMap, erasingFromASharedMapActuallyErases)
{
    // It did not: the rebuild inserted [begin, it) and then [it, end),
    // and the second range STARTS at the key being erased, so a shared
    // map kept every key it was asked to drop.
    Base::COWMap<int, int> first;
    first.set(1, 10);
    first.set(2, 20);
    first.set(3, 30);

    Base::COWMap<int, int> second = first;
    second.erase(2);

    EXPECT_EQ(second.size(), 2);
    EXPECT_EQ(second.get(2), nullptr);
    ASSERT_NE(second.get(1), nullptr);
    EXPECT_EQ(*second.get(1), 10);
    ASSERT_NE(second.get(3), nullptr);
    EXPECT_EQ(*second.get(3), 30);

    // and the holder it was shared with keeps what it had
    EXPECT_EQ(first.size(), 3);
    ASSERT_NE(first.get(2), nullptr);
    EXPECT_EQ(*first.get(2), 20);
}

TEST(COWMap, erasingTheOnlyEntryOfASharedMapLeavesNothing)
{
    Base::COWMap<int, int> first;
    first.set(1, 10);
    Base::COWMap<int, int> second = first;

    second.erase(1);
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(first.size(), 1);
}

TEST(COWMap, erasingWhatIsNotThereChangesNothing)
{
    Base::COWMap<int, int> values;
    values.erase(7);
    values.set(1, 10);
    Base::COWMap<int, int> shared = values;
    shared.erase(7);
    EXPECT_TRUE(values.isShared());
    EXPECT_EQ(shared.size(), 1);
}

TEST(COWMap, settingAnUnchangedValueDoesNotDetach)
{
    Base::COWMap<int, int> first;
    first.set(1, 10);
    Base::COWMap<int, int> second = first;

    second.set(1, 10);
    EXPECT_TRUE(first.isShared());

    second.set(1, 11, false);  // no overwrite
    EXPECT_TRUE(first.isShared());

    second.set(1, 11);
    EXPECT_FALSE(first.isShared());
    EXPECT_EQ(*first.get(1), 10);
    EXPECT_EQ(*second.get(1), 11);
}

TEST(COWMap, combiningIntoAKeyThatIsNotThereInsertsIt)
{
    // The branch that combined an absent key dereferenced end(): it read
    // "else if (!found) { it->second.combine(value); }".
    Base::COWMap<int, Combinable> values;
    values.combine(1, Combinable {5});
    ASSERT_NE(values.get(1), nullptr);
    EXPECT_EQ(values.get(1)->value, 5);

    values.combine(1, Combinable {3});
    EXPECT_EQ(values.get(1)->value, 8);

    values.combine(2, Combinable {7});
    EXPECT_EQ(values.size(), 2);
    EXPECT_EQ(values.get(2)->value, 7);
}

TEST(COWMap, combiningDetachesBeforeItAddsAnything)
{
    Base::COWMap<int, Combinable> first;
    first.combine(1, Combinable {5});
    Base::COWMap<int, Combinable> second = first;

    second.combine(1, Combinable {5});
    EXPECT_EQ(first.get(1)->value, 5);
    EXPECT_EQ(second.get(1)->value, 10);

    Base::COWMap<int, Combinable> third = first;
    third.combine(2, Combinable {1});
    EXPECT_EQ(first.size(), 1);
    EXPECT_EQ(third.size(), 2);
}

TEST(COWMap, combiningTwoMapsAddsEveryKey)
{
    Base::COWMap<int, Combinable> first;
    first.combine(1, Combinable {5});
    first.combine(2, Combinable {1});

    Base::COWMap<int, Combinable> second;
    second.combine(2, Combinable {2});
    second.combine(3, Combinable {3});

    first.combine(second);
    EXPECT_EQ(first.size(), 3);
    EXPECT_EQ(first.get(1)->value, 5);
    EXPECT_EQ(first.get(2)->value, 3);
    EXPECT_EQ(first.get(3)->value, 3);

    // Combining a map with itself is refused rather than doubled, which
    // is what keeps the loop from walking storage it is writing
    first.combine(first);
    EXPECT_EQ(first.get(1)->value, 5);
}

TEST(COWMap, addTakesOnlyWhatTheTargetDoesNotStateWhenToldNotToOverwrite)
{
    Base::COWMap<int, int> first;
    first.set(1, 10);

    Base::COWMap<int, int> second;
    second.set(1, 99);
    second.set(2, 20);

    first.add(second, false);
    EXPECT_EQ(*first.get(1), 10);
    EXPECT_EQ(*first.get(2), 20);

    first.add(second);
    EXPECT_EQ(*first.get(1), 99);
}

// ---------------------------------------------------------------------
// Ordering, which the render cache uses as a key

TEST(COWData, orderingIsBySizeFirstAndOnlyThenByContent)
{
    // Deliberate, and not std::lexicographical_compare: the common case
    // for a material key is two arrays of different length, and comparing
    // those without touching the elements is the whole point.
    Base::COWVector<int> shorter;
    shorter.append(9);

    Base::COWVector<int> longer;
    longer.append(1);
    longer.append(1);

    EXPECT_TRUE(shorter < longer);
    EXPECT_FALSE(longer < shorter);
    EXPECT_TRUE(longer > shorter);

    Base::COWVector<int> same;
    same.append(9);
    EXPECT_TRUE(shorter == same);
    EXPECT_FALSE(shorter < same);
}

// ---------------------------------------------------------------------
// Accounting

TEST(COWMemScope, aTaggedContainerCountsIntoItsOwnScopeAndNoOtherOnes)
{
    Base::COWMemScope &scopeA = Base::cowMemScope<TestScopeA>();
    Base::COWMemScope &scopeB = Base::cowMemScope<TestScopeB>();
    const std::int64_t beforeA = scopeA.bytes();
    const std::int64_t beforeB = scopeB.bytes();

    {
        Base::TrackedVector<int, TestScopeA> tracked;
        tracked.resize(1000);
        EXPECT_GE(scopeA.bytes() - beforeA, static_cast<std::int64_t>(1000 * sizeof(int)));
        EXPECT_EQ(scopeB.bytes(), beforeB);

        // An untagged container is not counted anywhere
        std::vector<int> plain(1000);
        EXPECT_EQ(plain.size(), 1000U);
        EXPECT_LT(scopeA.bytes() - beforeA, static_cast<std::int64_t>(2000 * sizeof(int)));
    }

    EXPECT_EQ(scopeA.bytes(), beforeA);
    EXPECT_GT(scopeA.maxBytes(), 0);
}

TEST(COWMemScope, aCowContainerAccountsItsStorageObjectInTheSameScope)
{
    Base::COWMemScope &scope = Base::cowMemScope<TestScopeB>();
    const std::int64_t before = scope.bytes();
    {
        Base::COWVector<int, Base::TrackedVector<int, TestScopeB>> values;
        values.append(1);
        EXPECT_GT(scope.bytes(), before);
    }
    EXPECT_EQ(scope.bytes(), before);
}

TEST(COWMemScope, theReportNamesEveryScopeThatHasBeenUsed)
{
    Base::cowMemScope<TestScopeA>();
    std::ostringstream out;
    Base::reportCOWMemStats(out);
    EXPECT_NE(out.str().find("TestScopeA"), std::string::npos);
}
