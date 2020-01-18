#include "opj_includes.h"

/* <summary>                */
/* Get norm of 5-3 wavelet. */
/* </summary>               */
OPJ_FLOAT64 opj_dwt_getnorm(OPJ_UINT32 level, OPJ_UINT32 orient)
{
    /* FIXME ! This is just a band-aid to avoid a buffer overflow */
    /* but the array should really be extended up to 33 resolution levels */
    /* See https://github.com/uclouvain/openjpeg/issues/493 */
    if (orient == 0 && level >= 10) {
        level = 9;
    } else if (orient > 0 && level >= 9) {
        level = 8;
    }
    return opj_dwt_norms[orient][level];
}


/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
OPJ_FLOAT64 opj_dwt_getnorm_real(OPJ_UINT32 level, OPJ_UINT32 orient)
{
    /* FIXME ! This is just a band-aid to avoid a buffer overflow */
    /* but the array should really be extended up to 33 resolution levels */
    /* See https://github.com/uclouvain/openjpeg/issues/493 */
    if (orient == 0 && level >= 10) {
        level = 9;
    } else if (orient > 0 && level >= 9) {
        level = 8;
    }
    return opj_dwt_norms_real[orient][level];
}
