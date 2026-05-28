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

TEST(HTTPClient, RealModeWhenCurlAvailable) {
#ifdef BOARDGHOST_WITH_CURL
    setenv("BOARDGHOST_NET", "real", 1);
    HTTPClient http;
    // example.com is a stable test target maintained by IANA.
    http.begin(String("http://example.com/"));
    int code = http.GET();
    EXPECT_EQ(code, 200);
    EXPECT_GT(http.getString().length(), 100u);
    http.end();
#else
    GTEST_SKIP() << "BOARDGHOST_WITH_CURL not enabled";
#endif
}
