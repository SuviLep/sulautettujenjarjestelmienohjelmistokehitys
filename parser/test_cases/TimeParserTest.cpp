#include <gtest/gtest.h>
#include "../TimeParser.h"


TEST(TimeParserTest, TestNullPointer) {
    ASSERT_EQ(time_parse(nullptr), TIME_ARRAY_ERROR);   // null
    
    char ok[] = "000001";                               // valid
    EXPECT_EQ(time_parse(ok), 1);

}

TEST(TimeParserTest, TestToSeconds) {
    char t[] = "000120";
    EXPECT_EQ(time_parse(t), 80);                       // 1 min 20 s = 80s
}

TEST(TimeParserTest, TestUpperBoundary) {
    char t[] = "235959";
    EXPECT_EQ(time_parse(t), 23*3600 + 59*60 + 59);
}

TEST(TimeParserTest, TestHours23) {
    char t[] = "240000";                       // 24:00:00
    EXPECT_EQ(time_parse(t), TIME_VALUE_ERROR);
}

TEST(TimeParserTest, TestMinutes59) {
    char t[] = "126000";                       // 12:60:00
    EXPECT_EQ(time_parse(t), TIME_VALUE_ERROR);
}

TEST(TimeParserTest, TestSeconds59) {
    char t[] = "120060";                       // 12:00:60
    EXPECT_EQ(time_parse(t), TIME_VALUE_ERROR);
}

TEST(TimeParserTest, TestNonDigits) {       // extra-pojo. Vain numeroita. 
    char t[] = "12a045";
    EXPECT_EQ(time_parse(t), TIME_LEN_ERROR);
}

TEST(TimeParserTest, TestLenght) {          // extra pojo pituuden tarkistukseen.

    char s[] = "12345"; //lyhyt
    EXPECT_EQ(time_parse(s), TIME_LEN_ERROR);
    
    char l[] = "1234567"; //pitkä
    EXPECT_EQ(time_parse(l), TIME_LEN_ERROR);
}
TEST(TimeParserTest, RejectsMinusSign) {    //extrapojo ei neg arvoja
    char t[] = "-10010"; // '-'
    EXPECT_EQ(time_parse(t), TIME_LEN_ERROR);
}

// https://google.github.io/googletest/reference/testing.html
// https://google.github.io/googletest/reference/assertions.html
