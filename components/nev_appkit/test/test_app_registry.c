/*
 * The app registry: registration, lookup, and the home-grid ordering policy.
 *
 * No display is opened. This is the bookkeeping that decides what appears on
 * the home screen and in what order, which is worth testing separately from
 * anything that draws.
 */
#include "nev_appkit/app.h"
#include "unity.h"
#include <string.h>

static nev_err_t stub_launch(lv_obj_t *root) {
    (void)root;
    return NEV_OK;
}

static nev_app_desc_t make(const char *id, nev_app_category_t cat) {
    nev_app_desc_t d = {0};
    d.id = id;
    d.name = id;
    d.category = cat;
    d.on_launch = stub_launch;
    return d;
}

void setUp(void) { nev_app_registry_reset(); }
void tearDown(void) { nev_app_registry_reset(); }

static void test_registration_and_lookup(void) {
    static nev_app_desc_t a, b;
    a = make("snake", NEV_APP_CAT_GAME);
    b = make("settings", NEV_APP_CAT_SYSTEM);

    nev_app_register(&a);
    nev_app_register(&b);

    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)nev_app_count());
    TEST_ASSERT_EQUAL_PTR(&a, nev_app_find("snake"));
    TEST_ASSERT_EQUAL_PTR(&b, nev_app_find("settings"));
    TEST_ASSERT_NULL(nev_app_find("nonexistent"));
    TEST_ASSERT_NULL(nev_app_find(NULL));
}

/*
 * A duplicate id would make nev_app_find ambiguous and would corrupt the
 * "last app" the shell restores on boot, so it is refused rather than shadowed.
 */
static void test_duplicate_ids_are_refused(void) {
    static nev_app_desc_t first, second;
    first = make("clock", NEV_APP_CAT_SYSTEM);
    second = make("clock", NEV_APP_CAT_GAME);

    nev_app_register(&first);
    nev_app_register(&second);

    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)nev_app_count());
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&first, nev_app_find("clock"), "the duplicate won");
}

/* Registration runs from constructors before anything is initialised, so a
 * malformed descriptor has to be survivable rather than fatal. */
static void test_malformed_descriptors_are_ignored(void) {
    static nev_app_desc_t no_id, no_launch, no_name;
    no_id = make("x", NEV_APP_CAT_GAME);
    no_id.id = NULL;
    no_launch = make("y", NEV_APP_CAT_GAME);
    no_launch.on_launch = NULL;
    no_name = make("z", NEV_APP_CAT_GAME);
    no_name.name = NULL;

    nev_app_register(NULL);
    nev_app_register(&no_id);
    nev_app_register(&no_launch);
    nev_app_register(&no_name);

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)nev_app_count());
}

/*
 * Games first, then productivity, then system. A grid whose icons move between
 * boots is a grid you cannot learn, so order within a category follows
 * registration order, which is link order and therefore stable per build.
 */
static void test_ordering_groups_by_category_and_is_stable(void) {
    static nev_app_desc_t s1, g1, p1, g2, s2;
    s1 = make("settings", NEV_APP_CAT_SYSTEM);
    g1 = make("snake", NEV_APP_CAT_GAME);
    p1 = make("focus", NEV_APP_CAT_PRODUCTIVITY);
    g2 = make("breakout", NEV_APP_CAT_GAME);
    s2 = make("clock", NEV_APP_CAT_SYSTEM);

    nev_app_register(&s1);
    nev_app_register(&g1);
    nev_app_register(&p1);
    nev_app_register(&g2);
    nev_app_register(&s2);

    const nev_app_desc_t *out[8];
    const size_t          n = nev_app_ordered(out, 8);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)n);

    TEST_ASSERT_EQUAL_STRING("snake", out[0]->id);
    TEST_ASSERT_EQUAL_STRING("breakout", out[1]->id);
    TEST_ASSERT_EQUAL_STRING("focus", out[2]->id);
    TEST_ASSERT_EQUAL_STRING("settings", out[3]->id);
    TEST_ASSERT_EQUAL_STRING("clock", out[4]->id);

    /* And asking twice gives the same answer. */
    const nev_app_desc_t *again[8];
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)nev_app_ordered(again, 8));
    TEST_ASSERT_EQUAL_MEMORY(out, again, sizeof(out[0]) * 5);
}

static void test_ordering_respects_the_output_limit(void) {
    static nev_app_desc_t a, b, c;
    a = make("a", NEV_APP_CAT_GAME);
    b = make("b", NEV_APP_CAT_GAME);
    c = make("c", NEV_APP_CAT_GAME);
    nev_app_register(&a);
    nev_app_register(&b);
    nev_app_register(&c);

    const nev_app_desc_t *out[2];
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)nev_app_ordered(out, 2));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)nev_app_ordered(NULL, 2));
}

static void test_index_access_is_bounded(void) {
    static nev_app_desc_t a;
    a = make("only", NEV_APP_CAT_GAME);
    nev_app_register(&a);

    TEST_ASSERT_EQUAL_PTR(&a, nev_app_at(0));
    TEST_ASSERT_NULL(nev_app_at(1));
    TEST_ASSERT_NULL(nev_app_at(9999));
}

static void test_every_category_has_a_name(void) {
    for (int i = 0; i < NEV_APP_CAT_COUNT; i++) {
        const char *n = nev_app_category_name((nev_app_category_t)i);
        TEST_ASSERT_NOT_NULL(n);
        TEST_ASSERT_NOT_EQUAL('?', n[0]);
    }
    TEST_ASSERT_EQUAL_STRING("?", nev_app_category_name((nev_app_category_t)99));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_registration_and_lookup);
    RUN_TEST(test_duplicate_ids_are_refused);
    RUN_TEST(test_malformed_descriptors_are_ignored);
    RUN_TEST(test_ordering_groups_by_category_and_is_stable);
    RUN_TEST(test_ordering_respects_the_output_limit);
    RUN_TEST(test_index_access_is_bounded);
    RUN_TEST(test_every_category_has_a_name);
    return UNITY_END();
}
