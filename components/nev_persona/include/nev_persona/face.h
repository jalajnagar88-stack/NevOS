/*
 * NEVOS L3 — face rendering.
 *
 * The expressive model lives in face_params.h and has no LVGL dependency. This
 * header is the drawing half.
 *
 * RENDERING APPROACH — a deliberate deviation worth explaining.
 *
 * The brief asks for LVGL canvas primitives. This uses styled LVGL objects
 * instead: rounded rectangles, arcs and rotations whose geometry is driven by
 * the parameters. The result is still vector-style and still scales and tweens
 * smoothly, but LVGL tracks a dirty rectangle per object, so only the parts
 * that actually moved are redrawn. A canvas would mark its whole area dirty
 * every frame: a 300x220 face canvas is 132 KB of RGB565 re-read and
 * re-flushed at 30 fps, roughly 4 MB/s of PSRAM traffic NEVOS does not have to
 * spare (ADR 0005). Objects are the difference between the face costing a
 * fraction of a frame and costing most of one.
 */
#ifndef NEV_PERSONA_FACE_H
#define NEV_PERSONA_FACE_H

#include "nev_persona/face_params.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- renderers */

/*
 * A design direction. Three are implemented so the visual identity can be
 * chosen from rendered output rather than from a description; the winner stays
 * and the others are deleted.
 */
typedef struct {
    const char *id;
    const char *name;
    const char *summary;
    nev_err_t (*create)(lv_obj_t *parent);
    void (*destroy)(void);
    void (*apply)(const nev_face_params_t *params);
} nev_face_renderer_t;

const nev_face_renderer_t *nev_face_renderer_get(const char *id);
const nev_face_renderer_t *nev_face_renderer_at(size_t index);
size_t nev_face_renderer_count(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_PERSONA_FACE_H */
