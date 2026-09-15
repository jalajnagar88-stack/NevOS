#include "nev_port/nev_types.h"

const char *nev_err_str(nev_err_t err) {
    switch (err) {
        case NEV_OK:
            return "OK";
        case NEV_ERR_INVALID_ARG:
            return "INVALID_ARG";
        case NEV_ERR_NO_MEM:
            return "NO_MEM";
        case NEV_ERR_NO_SPACE:
            return "NO_SPACE";
        case NEV_ERR_TIMEOUT:
            return "TIMEOUT";
        case NEV_ERR_NOT_FOUND:
            return "NOT_FOUND";
        case NEV_ERR_INVALID_STATE:
            return "INVALID_STATE";
        case NEV_ERR_DROPPED:
            return "DROPPED";
        case NEV_ERR_UNSUPPORTED:
            return "UNSUPPORTED";
    }
    return "UNKNOWN";
}
