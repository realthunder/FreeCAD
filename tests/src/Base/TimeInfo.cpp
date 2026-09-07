#include "gtest/gtest.h"
#include <Base/TimeInfo.h>

TEST(TimeInfo, TestDefault)
{
    Base::TimeInfo ti;
    EXPECT_EQ(ti.isNull(), false);
}

TEST(TimeInfo, TestNull)
{
    Base::TimeInfo ti(Base::TimeInfo::null());
    EXPECT_EQ(ti.isNull(), true);
}

TEST(TimeInfo, TestCompare)
{
    Base::TimeInfo ti1;
    Base::TimeInfo ti2;
    ti2.setTime_t(ti1.getSeconds() + 1);
    EXPECT_EQ(ti1 == ti1, true);
    EXPECT_EQ(ti1 != ti2, true);
    EXPECT_EQ(ti1 < ti2, true);
    EXPECT_EQ(ti1 > ti2, false);
    EXPECT_EQ(ti1 <= ti1, true);
    EXPECT_EQ(ti1 >= ti1, true);
}

TEST(TimeInfo, TestDiffTime)
{
    Base::TimeInfo ti1;
    // A copy, not a second default construction: setTime_t() writes only
    // the seconds, so a fresh TimeInfo keeps its own milliseconds and the
    // difference is a whole second only when the two happen to be
    // constructed inside the same millisecond. On a slow enough box they
    // are not, and the test failed by 0.001.
    Base::TimeInfo ti2(ti1);
    ti2.setTime_t(ti1.getSeconds() + 1);
    EXPECT_EQ(Base::TimeInfo::diffTimeF(ti1, ti2), 1.0);
}
