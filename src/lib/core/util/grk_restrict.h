#pragma once

#if defined(__GNUC__)
#define GRK_RESTRICT __restrict__
#else
#define GRK_RESTRICT /* GRK_RESTRICT */
#endif