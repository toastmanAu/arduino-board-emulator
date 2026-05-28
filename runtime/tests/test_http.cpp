#include <gtest/gtest.h>
#include "HTTPClient.h"
#include <cstdlib>

TEST(HTTPClient, FakeModeReturns200Empty) {
    setenv("BOARDGHOST_NET", "fake", 1);
    HTTPClient http;
    http.begin(String("http://example.com/data.json"));
    int code = http.GET();
    EXPECT_EQ(code, 200);
    EXPECT_EQ(http.getString(), String(""));
    http.end();
}

TEST(HTTPClient, FailModeReturnsNegative) {
    setenv("BOARDGHOST_NET", "fail", 1);
    HTTPClient http;
    http.begin(String("http://example.com/data.json"));
    int code = http.GET();
    EXPECT_LT(code, 0);
    http.end();
}
