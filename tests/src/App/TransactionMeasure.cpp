// SPDX-License-Identifier: LGPL-2.1-or-later

// The phase-0 measurement of the transaction log (docs/TransactionLog.md
// sec 15): a commit walks its transaction the way the log writer will and
// writes one CSV row per value. These tests pin the walk -- the op each
// change becomes, the two hashes of a set, the dedup of a value seen twice
// -- so the numbers the harness reports are about the right things.

#include "gtest/gtest.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/FeatureTest.h"
#include "App/TransactionMeasure.h"
#include "Base/FileInfo.h"
#include <src/App/InitApplication.h>

namespace {

struct Row
{
    std::vector<std::string> cols;
    const std::string& operator[](size_t i) const
    {
        static const std::string empty;
        return i < cols.size() ? cols[i] : empty;
    }
};

std::vector<Row> readCsv(const std::string& path)
{
    std::vector<Row> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        Row row;
        std::string cell;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (quoted) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        cell += '"';
                        ++i;
                    }
                    else {
                        quoted = false;
                    }
                }
                else {
                    cell += c;
                }
            }
            else if (c == '"') {
                quoted = true;
            }
            else if (c == ',') {
                row.cols.push_back(cell);
                cell.clear();
            }
            else {
                cell += c;
            }
        }
        row.cols.push_back(cell);
        rows.push_back(row);
    }
    return rows;
}

size_t column(const Row& header, const char* name)
{
    for (size_t i = 0; i < header.cols.size(); ++i) {
        if (header.cols[i] == name)
            return i;
    }
    return static_cast<size_t>(-1);
}

class TransactionMeasureTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("txnmeasure");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _doc->setUndoMode(1);
        _csv = Base::FileInfo::getTempFileName("txnmeasure", nullptr) + ".csv";
        ASSERT_TRUE(App::TransactionMeasure::start(_csv.c_str()));
    }

    void TearDown() override
    {
        App::TransactionMeasure::stop();
        App::GetApplication().closeDocument(_docName.c_str());
        Base::FileInfo(_csv).deleteFile();
    }

    std::vector<Row> rows()
    {
        App::TransactionMeasure::stop();
        return readCsv(_csv);
    }

    App::Document* doc() { return _doc; }

private:
    std::string _docName;
    App::Document* _doc {};
    std::string _csv;
};

TEST_F(TransactionMeasureTest, createThenSetThenRemove)
{
    doc()->openTransaction("create");
    auto obj = static_cast<App::FeatureTest*>(doc()->addObject("App::FeatureTest", "Obj"));
    ASSERT_TRUE(obj);
    doc()->commitTransaction();

    doc()->openTransaction("set");
    obj->Integer.setValue(42);
    obj->Integer.setValue(43);  // two writes, one op
    doc()->commitTransaction();

    doc()->openTransaction("remove");
    doc()->removeObject("Obj");
    doc()->commitTransaction();

    auto all = rows();
    ASSERT_GT(all.size(), 1u);
    const Row& header = all[0];
    const size_t cOp = column(header, "op");
    const size_t cProp = column(header, "prop");
    const size_t cBefore = column(header, "before_hash");
    const size_t cAfter = column(header, "after_hash");
    const size_t cAfterSeen = column(header, "after_seen");
    const size_t cAfterBytes = column(header, "after_bytes");

    std::vector<Row> ops, txns;
    for (size_t i = 1; i < all.size(); ++i) {
        if (all[i][0] == "op")
            ops.push_back(all[i]);
        else if (all[i][0] == "txn")
            txns.push_back(all[i]);
    }
    ASSERT_EQ(txns.size(), 3u);

    // create: one op, an after snapshot and no before.
    ASSERT_GE(ops.size(), 3u);
    EXPECT_EQ(ops[0][cOp], "create");
    EXPECT_TRUE(ops[0][cBefore].empty());
    EXPECT_EQ(ops[0][cAfter].size(), 40u);  // sha1 hex
    EXPECT_GT(std::stoul(ops[0][cAfterBytes]), 0u);

    // set: exactly one op for Integer, both hashes present and different.
    size_t nSet = 0;
    for (auto& r : ops) {
        if (r[cOp] == "set" && r[cProp] == "Integer") {
            ++nSet;
            EXPECT_EQ(r[cBefore].size(), 40u);
            EXPECT_EQ(r[cAfter].size(), 40u);
            EXPECT_NE(r[cBefore], r[cAfter]);
            EXPECT_EQ(r[cAfterSeen], "0");
        }
    }
    EXPECT_EQ(nSet, 1u);

    // remove: a before snapshot and no after.
    const Row& last = ops.back();
    EXPECT_EQ(last[cOp], "remove");
    EXPECT_EQ(last[cBefore].size(), 40u);
    EXPECT_TRUE(last[cAfter].empty());

    // The txn summary row counts the ops of its commit.
    EXPECT_EQ(txns[0][4], "1");
    EXPECT_EQ(txns[2][4], "1");
}

TEST_F(TransactionMeasureTest, valueSeenTwiceIsDedupedAndNoopSetHashesEqual)
{
    doc()->openTransaction("create");
    auto obj = static_cast<App::FeatureTest*>(doc()->addObject("App::FeatureTest", "Obj"));
    doc()->commitTransaction();

    const long initial = obj->Integer.getValue();
    doc()->openTransaction("set");
    obj->Integer.setValue(initial + 7);
    doc()->commitTransaction();

    // Back to the value the first set started from: the after hash of
    // this set is the before hash of the previous one, already seen.
    doc()->openTransaction("set back");
    obj->Integer.setValue(initial);
    doc()->commitTransaction();

    // A write that leaves the value as it was: both hashes equal.
    doc()->openTransaction("noop");
    obj->Integer.setValue(initial);
    doc()->commitTransaction();

    auto all = rows();
    const Row& header = all[0];
    const size_t cOp = column(header, "op");
    const size_t cProp = column(header, "prop");
    const size_t cBefore = column(header, "before_hash");
    const size_t cAfter = column(header, "after_hash");
    const size_t cAfterSeen = column(header, "after_seen");

    std::vector<Row> sets;
    for (size_t i = 1; i < all.size(); ++i) {
        if (all[i][0] == "op" && all[i][cOp] == "set" && all[i][cProp] == "Integer")
            sets.push_back(all[i]);
    }
    ASSERT_EQ(sets.size(), 3u);
    EXPECT_EQ(sets[0][cAfterSeen], "0");
    EXPECT_EQ(sets[1][cAfter], sets[0][cBefore]);
    EXPECT_EQ(sets[1][cAfterSeen], "1");
    EXPECT_EQ(sets[2][cBefore], sets[2][cAfter]);
}

}  // namespace
