#include <gtest/gtest.h>
#include "sim_mirror.h"

#include <cstdint>
#include <string>
#include <vector>

using boardghost::mirror::authorized;
using boardghost::mirror::build_info_json;
using boardghost::mirror::decide_bind;
using boardghost::mirror::frame_hash;
using boardghost::mirror::token_equal;

// ---------------------------------------------------------------------------
// decide_bind — the loopback-vs-LAN gate. The security invariant of Phase 1:
// we bind 0.0.0.0 ONLY for mode "lan" WITH a token. Everything else stays on
// loopback; asking for LAN without a token must warn (and stay loopback).
// ---------------------------------------------------------------------------

TEST(MirrorBind, DefaultsToLoopbackWhenModeUnset) {
    auto d = decide_bind(nullptr, /*token_present=*/false);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, EmptyModeIsLoopback) {
    auto d = decide_bind("", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, ExplicitOffIsLoopback) {
    auto d = decide_bind("off", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, LanWithTokenBindsLan) {
    auto d = decide_bind("lan", /*token_present=*/true);
    EXPECT_TRUE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

TEST(MirrorBind, LanWithoutTokenStaysLoopbackAndWarns) {
    auto d = decide_bind("lan", /*token_present=*/false);
    EXPECT_FALSE(d.bind_lan) << "must NOT open the LAN socket without a token";
    EXPECT_TRUE(d.warn_no_token);
}

TEST(MirrorBind, UnknownModeTreatedAsOff) {
    auto d = decide_bind("wan", /*token_present=*/true);
    EXPECT_FALSE(d.bind_lan);
    EXPECT_FALSE(d.warn_no_token);
}

// ---------------------------------------------------------------------------
// token_equal — constant-time compare. We can't directly assert timing in a
// unit test, but we CAN assert the functional contract: exact match only,
// length mismatch is false, empty handling is well-defined.
// ---------------------------------------------------------------------------

TEST(MirrorToken, EqualTokensMatch) {
    EXPECT_TRUE(token_equal("s3cret-abc", "s3cret-abc"));
}

TEST(MirrorToken, DifferentTokensDoNotMatch) {
    EXPECT_FALSE(token_equal("s3cret-abc", "s3cret-xyz"));
}

TEST(MirrorToken, LengthMismatchIsFalse) {
    EXPECT_FALSE(token_equal("short", "short-and-then-some"));
}

TEST(MirrorToken, SingleByteDifferenceIsFalse) {
    EXPECT_FALSE(token_equal("aaaaaaaa", "aaaaaaab"));
}

TEST(MirrorToken, BothEmptyMatch) {
    EXPECT_TRUE(token_equal("", ""));
}

// ---------------------------------------------------------------------------
// authorized — the per-request gate built on token_equal.
// No token configured → loopback dev convenience, always allowed.
// Token configured → must be presented in the X-BoardGhost-Mirror header and
// match exactly. Header-only by design: a ?token= query channel would leak the
// secret into logs/history/proxies (see sim_mirror.h).
// ---------------------------------------------------------------------------

TEST(MirrorAuth, NoTokenConfiguredAllowsEverything) {
    EXPECT_TRUE(authorized(/*configured=*/"", /*header=*/""));
    EXPECT_TRUE(authorized("", "whatever"));
}

TEST(MirrorAuth, ConfiguredTokenRequiresAMatch) {
    EXPECT_FALSE(authorized("tok", /*header=*/""));
    EXPECT_FALSE(authorized("tok", "wrong"));
}

TEST(MirrorAuth, HeaderTokenAuthorizes) {
    EXPECT_TRUE(authorized("tok", /*header=*/"tok"));
}

// ---------------------------------------------------------------------------
// frame_hash — change detection for the MJPEG stream.
// ---------------------------------------------------------------------------

TEST(MirrorFrameHash, IdenticalBuffersHashEqual) {
    std::vector<uint16_t> a(256, 0xF800);
    std::vector<uint16_t> b(256, 0xF800);
    EXPECT_EQ(frame_hash(a), frame_hash(b));
}

TEST(MirrorFrameHash, SinglePixelChangeChangesHash) {
    std::vector<uint16_t> a(256, 0xF800);
    std::vector<uint16_t> b = a;
    b[123] = 0x07E0;
    EXPECT_NE(frame_hash(a), frame_hash(b));
}

TEST(MirrorFrameHash, DifferentLengthChangesHash) {
    std::vector<uint16_t> a(256, 0);
    std::vector<uint16_t> b(255, 0);
    EXPECT_NE(frame_hash(a), frame_hash(b));
}

// ---------------------------------------------------------------------------
// build_info_json — feature-detection payload.
// ---------------------------------------------------------------------------

TEST(MirrorInfo, CarriesDimsFpsAudioAndEndpoints) {
    std::string j = build_info_json(320, 480, 15);
    EXPECT_NE(j.find("\"w\":320"), std::string::npos);
    EXPECT_NE(j.find("\"h\":480"), std::string::npos);
    EXPECT_NE(j.find("\"fps\":15"), std::string::npos);
    EXPECT_NE(j.find("\"rate\":44100"), std::string::npos);
    EXPECT_NE(j.find("/mirror/screen.png"), std::string::npos);
    EXPECT_NE(j.find("/mirror/display"), std::string::npos);
}
