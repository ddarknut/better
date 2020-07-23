#ifndef BETTER_H
#define BETTER_H

#include <winsock2.h>
#include <cstdint>

#include "better_types.h"
#include "better_const.h"
#include "better_App.h"

#define better_internal static
#define better_persist  static

#define BETTER_ARRAYC(ary) ((i32)(sizeof(ary) / sizeof(ary[0])))

#ifdef NDEBUG
#define BETTER_ASSERT(val)
#else
#define BETTER_ASSERT(val) if(!(val)) {*(u8*)0 = 0;}
#endif

#define BETTER_MAX(x,y) ((x) > (y)? (x) : (y))
#define BETTER_MIN(x,y) ((x) < (y)? (x) : (y))
#define BETTER_ABS(x) ((x) < 0? -(x) : (x))
#define BETTER_CLAMP(x, a, b) (((x) < (a))? (a) : (((x) > (b))? (b) : (x)))
#define BETTER_LERP(a, b, f) ((a) + (f) * ((b) - (a)))

enum BETTER_WM
{
    BETTER_WM_DNS_COMPLETE = WM_USER,
    BETTER_WM_DNS_FAILED,
    BETTER_WM_SOCK_MSG
};

#endif // BETTER_H
