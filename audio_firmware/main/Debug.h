#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdio.h>
#include "esp_log.h"

#define USE_DEBUG 0
#if USE_DEBUG
    #define Debug(__info) printf("%s", __info)
#else
    #define Debug(__info)  
#endif

#endif // __DEBUG_H
