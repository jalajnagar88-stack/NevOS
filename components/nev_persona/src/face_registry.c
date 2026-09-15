/*
 * The face designs.
 *
 * Three candidates were rendered side by side to choose a direction; the
 * eyes-only design won and the other two were deleted rather than kept around
 * as dead alternatives. The registry stays because the indirection costs one
 * pointer and is what made the comparison possible in the first place.
 */
#include "nev_persona/face.h"
#include <string.h>

extern const nev_face_renderer_t nev_face_renderer_vector;

static const nev_face_renderer_t *const kRenderers[] = {
    &nev_face_renderer_vector,
};

size_t nev_face_renderer_count(void) {
    return sizeof(kRenderers) / sizeof(kRenderers[0]);
}

const nev_face_renderer_t *nev_face_renderer_at(size_t index) {
    return index < nev_face_renderer_count() ? kRenderers[index] : NULL;
}

const nev_face_renderer_t *nev_face_renderer_get(const char *id) {
    if (!id) return NULL;
    for (size_t i = 0; i < nev_face_renderer_count(); i++) {
        if (strcmp(kRenderers[i]->id, id) == 0) return kRenderers[i];
    }
    return NULL;
}
