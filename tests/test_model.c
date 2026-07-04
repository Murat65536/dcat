#include "graphics/model.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#ifndef SPOT_MODEL_DIR
#define SPOT_MODEL_DIR "fixtures/spot"
#endif

static const char *fixture_path(const char *name) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "%s/%s", SPOT_MODEL_DIR, name);
    return buf;
}

static Mesh g_mesh;

void setUp(void) {
    mesh_init(&g_mesh);
}
void tearDown(void) {
    mesh_free(&g_mesh);
}

static void load_ok(const char *name, bool *out_has_uvs, MaterialInfo **out_mats,
                    size_t *out_count) {
    bool has_uvs = false;
    MaterialInfo *mats = NULL;
    size_t count = 0;
    TEST_ASSERT_TRUE_MESSAGE(load_model(fixture_path(name), &g_mesh, &has_uvs, &mats, &count),
                             name);
    *out_has_uvs = has_uvs;
    *out_mats = mats;
    *out_count = count;
}

static void test_load_triangulated_geometry(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_triangulated.obj", &has_uvs, &mats, &count);

    TEST_ASSERT_TRUE(has_uvs);
    TEST_ASSERT_GREATER_THAN_UINT(0, g_mesh.vertices.count);
    TEST_ASSERT_GREATER_THAN_UINT(0, g_mesh.indices.count);
    // Triangulated topology: index buffer is whole triangles.
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.indices.count % 3);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, g_mesh.submeshes.count);
    // Every index must point at a real vertex.
    for (size_t i = 0; i < g_mesh.indices.count; i++) {
        TEST_ASSERT_LESS_THAN_UINT(g_mesh.vertices.count, g_mesh.indices.data[i]);
    }
    // GenNormals filled these in; each should be normalized (length ~1).
    for (size_t i = 0; i < g_mesh.vertices.count; i++) {
        const float *n = g_mesh.vertices.data[i].normal;
        float len = sqrtf((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
        TEST_ASSERT_FLOAT_WITHIN(1e-3F, 1.0F, len);
    }

    materials_free(mats, count);
}

static void test_load_triangulated_is_static(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_triangulated.obj", &has_uvs, &mats, &count);

    TEST_ASSERT_FALSE(g_mesh.has_animations);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.animations.count);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.skeleton.bones.count);
    // A successful load bumps the generation counter off zero.
    TEST_ASSERT_EQUAL_UINT64(1, g_mesh.generation);
    // At least the default material is always present.
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(1, count);
    TEST_ASSERT_NOT_NULL(mats);

    materials_free(mats, count);
}

static void test_load_quadrangulated_is_triangulated(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_quadrangulated.obj", &has_uvs, &mats, &count);

    TEST_ASSERT_TRUE(has_uvs);
    TEST_ASSERT_GREATER_THAN_UINT(0, g_mesh.indices.count);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.indices.count % 3);

    materials_free(mats, count);
}

// The coarse Catmull-Clark control cage also loads and triangulates cleanly.
static void test_load_control_mesh(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_control_mesh.obj", &has_uvs, &mats, &count);

    TEST_ASSERT_GREATER_THAN_UINT(0, g_mesh.vertices.count);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.indices.count % 3);
    // The control cage is far coarser than the tessellation.
    TEST_ASSERT_LESS_THAN_UINT(5000, g_mesh.indices.count);

    materials_free(mats, count);
}

// mesh_free must return the mesh to the empty state and drop all buffers.
static void test_mesh_free_resets_state(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_triangulated.obj", &has_uvs, &mats, &count);
    materials_free(mats, count);

    TEST_ASSERT_GREATER_THAN_UINT(0, g_mesh.vertices.count);
    mesh_free(&g_mesh);

    TEST_ASSERT_EQUAL_UINT(0, g_mesh.vertices.count);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.indices.count);
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.submeshes.count);
    TEST_ASSERT_NULL(g_mesh.vertices.data);
    TEST_ASSERT_EQUAL_UINT64(0, g_mesh.generation);
}

// A path that does not resolve to a model must fail without touching outputs
// beyond the reset the loader always performs.
static void test_load_missing_file_fails(void) {
    bool has_uvs = true;
    MaterialInfo *mats = (MaterialInfo *)0x1;
    size_t count = 99;
    TEST_ASSERT_FALSE(
        load_model(fixture_path("does_not_exist.obj"), &g_mesh, &has_uvs, &mats, &count));
    TEST_ASSERT_EQUAL_UINT(0, g_mesh.vertices.count);
}

// The camera framing derived from the loaded mesh should be finite and sit
// outside the model (positive scale, target within the bounding volume).
static void test_camera_setup_from_loaded_model(void) {
    bool has_uvs;
    MaterialInfo *mats;
    size_t count;
    load_ok("spot_triangulated.obj", &has_uvs, &mats, &count);
    materials_free(mats, count);

    CameraSetup setup;
    calculate_camera_setup(&g_mesh.vertices, &setup);

    TEST_ASSERT_TRUE(setup.model_scale > 0.0F);
    TEST_ASSERT_TRUE(isfinite(setup.model_scale));
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(isfinite(setup.position[i]));
        TEST_ASSERT_TRUE(isfinite(setup.target[i]));
    }
    // The camera is pushed back from its target by at least the model's extent.
    float dist =
        sqrtf((setup.position[0] - setup.target[0]) * (setup.position[0] - setup.target[0]) +
              (setup.position[1] - setup.target[1]) * (setup.position[1] - setup.target[1]) +
              (setup.position[2] - setup.target[2]) * (setup.position[2] - setup.target[2]));
    TEST_ASSERT_TRUE(dist > setup.model_scale);
}

// With no geometry the framing falls back to a fixed default view.
static void test_camera_setup_empty_defaults(void) {
    VertexArray empty = {0};
    CameraSetup setup;
    calculate_camera_setup(&empty, &setup);

    TEST_ASSERT_EQUAL_FLOAT(0.0F, setup.position[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, setup.position[1]);
    TEST_ASSERT_EQUAL_FLOAT(3.0F, setup.position[2]);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, setup.target[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, setup.target[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, setup.target[2]);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, setup.model_scale);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_load_triangulated_geometry);
    RUN_TEST(test_load_triangulated_is_static);
    RUN_TEST(test_load_quadrangulated_is_triangulated);
    RUN_TEST(test_load_control_mesh);
    RUN_TEST(test_mesh_free_resets_state);
    RUN_TEST(test_load_missing_file_fails);
    RUN_TEST(test_camera_setup_from_loaded_model);
    RUN_TEST(test_camera_setup_empty_defaults);
    return UNITY_END();
}
