#include <gtest/gtest.h>
#include <Arduino.h>

TEST(StringWrapper, ConstructAndCStr) {
    String s("hello");
    EXPECT_STREQ(s.c_str(), "hello");
    EXPECT_EQ(s.length(), 5u);
}

TEST(StringWrapper, ConcatPlusOperator) {
    String a("foo");
    String b("bar");
    String c = a + b;
    EXPECT_STREQ(c.c_str(), "foobar");
}

TEST(StringWrapper, ConcatWithCStrAndInt) {
    String s = "v=" + String(42);
    EXPECT_STREQ(s.c_str(), "v=42");
}

TEST(StringWrapper, ToIntAndToFloat) {
    EXPECT_EQ(String("123").toInt(), 123);
    EXPECT_EQ(String("not a number").toInt(), 0);
    EXPECT_FLOAT_EQ(String("3.5").toFloat(), 3.5f);
}

TEST(StringWrapper, IndexOfSubstringStartsWith) {
    String s("hello world");
    EXPECT_EQ(s.indexOf("world"), 6);
    EXPECT_EQ(s.indexOf("xyz"), -1);
    EXPECT_STREQ(s.substring(6).c_str(), "world");
    EXPECT_TRUE(s.startsWith("hello"));
    EXPECT_FALSE(s.startsWith("world"));
}
