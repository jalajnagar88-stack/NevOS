/*
 * The candidate designs. Two of these get deleted once a direction is chosen;
 * the registry exists so the choice can be made from rendered output instead
 * of from a description.
 */
#include "nev_persona/face.h"
#include <string.h>

extern const nev_face_renderer_t nev_face_renderer_vector;
extern const nev_face_renderer_t nev_face_renderer_full;
extern const nev_face_renderer_t nev_face_renderer_orb;

static const nev_face_renderer_t *const kRenderers[] = {
    &nev_face_renderer_vector,
    &nev_face_renderer_full,
    &nev_face_renderer_orb,
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
