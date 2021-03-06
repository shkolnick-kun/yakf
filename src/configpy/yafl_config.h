/*******************************************************************************
    Copyright 2020 anonimous <shkolnick-kun@gmail.com> and contributors.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing,
    software distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

    See the License for the specific language governing permissions
    and limitations under the License.
******************************************************************************/

#ifndef YAFL_CONFIG_H
#define YAFL_CONFIG_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define YAFL_LOG(...) fprintf(stderr, __VA_ARGS__)

typedef double  yaflFloat;
typedef int32_t yaflInt;

/*TODO: уточнить*/
#define YAFL_EPS  (1.0e-15)

#define YAFL_SQRT sqrt
#define YAFL_ABS  abs

/* WARNING!!!
Fast UKF SSR updates may give dramatically incorrect results in case of adaptive Bierman filter
*/
//#define YAFL_USE_FAST_UKF

#endif // YAFL_CONFIG_H
