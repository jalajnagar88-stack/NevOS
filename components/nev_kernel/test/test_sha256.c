/*
 * SHA-256, against FIPS 180-4's own vectors.
 *
 * This one decides whether a firmware image is the one the daemon offered, so
 * it is checked against the standard's published answers rather than against
 * itself. A hash that is subtly wrong still produces consistent results, which
 * is exactly why a round-trip test would not catch it.
 */
#include <string.h>

#include "nev_kernel/nev_sha256.h"
#include "unity.h"

void setUp(void) {
}
void tearDown(void) {
}

static void expect(const char *input, const char *hex) {
    uint8_t digest[NEV_SHA256_DIGEST_LEN];
    nev_sha256((const uint8_t *)input, strlen(input), digest);

    char got[NEV_SHA256_DIGEST_LEN * 2 + 1];
    for (int i = 0; i < NEV_SHA256_DIGEST_LEN; i++) {
        static const char kHex[] = "0123456789abcdef";
        got[i * 2] = kHex[digest[i] >> 4];
        got[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    got[sizeof(got) - 1] = '\0';
    TEST_ASSERT_EQUAL_STRING(hex, got);
}

static void test_the_empty_string(void) {
    expect("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

static void test_abc(void) {
    expect("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_the_two_block_vector(void) {
    /* 56 bytes: the length lands exactly where the padding has to spill into a
     * second block, which is the case a hand-written final() gets wrong. */
    expect("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_a_million_a_s(void) {
    /* The standard's long vector, fed in awkward pieces: this is how a firmware
     * image arrives, and a streaming bug shows up as a digest that is right for
     * one chunking and wrong for another. */
    nev_sha256_t s;
    nev_sha256_init(&s);
    uint8_t buf[1000];
    memset(buf, 'a', sizeof(buf));
    for (int i = 0; i < 1000; i++) {
        nev_sha256_update(&s, buf, sizeof(buf));
    }
    uint8_t digest[NEV_SHA256_DIGEST_LEN];
    nev_sha256_final(&s, digest);

    static const uint8_t kExpected[32] = {0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92,
                                          0x81, 0xa1, 0xc7, 0xe2, 0x84, 0xd7, 0x3e, 0x67,
                                          0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97, 0x20, 0x0e,
                                          0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kExpected, digest, 32);
}

static void test_the_chunking_does_not_matter(void) {
    /* An image arrives in whatever pieces the network delivers. */
    const char *text = "the quick brown fox jumps over the lazy dog, repeatedly and at length";
    uint8_t whole[NEV_SHA256_DIGEST_LEN];
    nev_sha256((const uint8_t *)text, strlen(text), whole);

    for (size_t split = 1; split < strlen(text); split++) {
        nev_sha256_t s;
        nev_sha256_init(&s);
        nev_sha256_update(&s, (const uint8_t *)text, split);
        nev_sha256_update(&s, (const uint8_t *)text + split, strlen(text) - split);
        uint8_t got[NEV_SHA256_DIGEST_LEN];
        nev_sha256_final(&s, got);
        TEST_ASSERT_TRUE_MESSAGE(nev_sha256_equal(whole, got), "a split changed the digest");
    }
}

static void test_comparison_rejects_a_single_bit(void) {
    uint8_t a[NEV_SHA256_DIGEST_LEN];
    uint8_t b[NEV_SHA256_DIGEST_LEN];
    nev_sha256((const uint8_t *)"firmware", 8, a);
    memcpy(b, a, sizeof(b));
    TEST_ASSERT_TRUE(nev_sha256_equal(a, b));

    b[31] ^= 0x01;
    TEST_ASSERT_FALSE_MESSAGE(nev_sha256_equal(a, b), "a flipped bit was accepted");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_the_empty_string);
    RUN_TEST(test_abc);
    RUN_TEST(test_the_two_block_vector);
    RUN_TEST(test_a_million_a_s);
    RUN_TEST(test_the_chunking_does_not_matter);
    RUN_TEST(test_comparison_rejects_a_single_bit);
    return UNITY_END();
}
