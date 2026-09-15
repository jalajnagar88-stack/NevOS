/*
 * The persistence backend behind nev_store. One implementation per target.
 *
 * Text values throughout: NVS stores them natively, the host file is readable
 * and editable by a person, and neither side needs a serialisation format that
 * could go out of sync with the schema.
 */
#ifndef NEV_KERNEL_NEV_STORE_BACKEND_H
#define NEV_KERNEL_NEV_STORE_BACKEND_H

#include "nev_port/nev_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nev_err_t (*open)(const char *path);
    void (*close)(void);
    /* NEV_ERR_NOT_FOUND when the key has never been written. */
    nev_err_t (*load)(const char *key, char *out, size_t out_len);
    nev_err_t (*save)(const char *key, const char *value);
    nev_err_t (*flush)(void);
    nev_err_t (*erase_all)(void);
} nev_store_backend_t;

/* Provided by the target's backend translation unit. */
const nev_store_backend_t *nev_store_backend(void);

#ifdef __cplusplus
}
#endif
#endif /* NEV_KERNEL_NEV_STORE_BACKEND_H */
