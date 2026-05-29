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

// M2.C gap-5: Stream-like read interface for ArduinoJson 7.x deserializeJson.
// ArduinoJson's generic Reader<String> calls source_->read() to consume one
// byte at a time, returning -1 at EOF.

TEST(StringStream, ReadConsumesBytesInOrder) {
    String s("abc");
    EXPECT_EQ(s.read(), 'a');
    EXPECT_EQ(s.read(), 'b');
    EXPECT_EQ(s.read(), 'c');
    EXPECT_EQ(s.read(), -1);   // EOF
    EXPECT_EQ(s.read(), -1);   // still EOF
}

TEST(StringStream, ReadReturnsMinusOneOnEmptyString) {
    String s;
    EXPECT_EQ(s.read(), -1);
}

TEST(StringStream, ReadHandlesHighBytesAsUnsigned) {
    // 0xC3 0xA9 is the UTF-8 encoding of "é". Bytes above 0x7F must not
    // sign-extend through char → int.
    char raw[] = { (char)0xC3, (char)0xA9, 0 };
    String s(raw);
    EXPECT_EQ(s.read(), 0xC3);
    EXPECT_EQ(s.read(), 0xA9);
    EXPECT_EQ(s.read(), -1);
}

TEST(StringStream, AvailableReportsRemaining) {
    String s("hi");
    EXPECT_EQ(s.available(), 2);
    s.read();
    EXPECT_EQ(s.available(), 1);
    s.read();
    EXPECT_EQ(s.available(), 0);
    s.read();   // past EOF
    EXPECT_EQ(s.available(), 0);
}

TEST(StringStream, PeekDoesNotConsume) {
    String s("XY");
    EXPECT_EQ(s.peek(), 'X');
    EXPECT_EQ(s.peek(), 'X');   // still 'X' — not consumed
    EXPECT_EQ(s.read(), 'X');
    EXPECT_EQ(s.peek(), 'Y');
}

TEST(StringStream, PeekReturnsMinusOneAtEof) {
    String s("a");
    s.read();
    EXPECT_EQ(s.peek(), -1);
}

TEST(StringStream, AssignmentResetsReadCursor) {
    String s("abc");
    s.read();
    s.read();
    s = "xyz";
    EXPECT_EQ(s.read(), 'x');
    EXPECT_EQ(s.available(), 2);
}

TEST(StringStream, AssignmentFromStringResetsReadCursor) {
    String s("abc");
    s.read();
    String other("xy");
    s = other;
    EXPECT_EQ(s.read(), 'x');
    EXPECT_EQ(s.read(), 'y');
    EXPECT_EQ(s.read(), -1);
}

// Realistic ArduinoJson-style usage: feed bytes into a consumer template
// that calls read() in a loop until -1.
TEST(StringStream, FullDrainViaReadLoop) {
    String s("{\"k\":1}");
    std::string drained;
    int c;
    while ((c = s.read()) >= 0) drained.push_back(static_cast<char>(c));
    EXPECT_EQ(drained, "{\"k\":1}");
}
