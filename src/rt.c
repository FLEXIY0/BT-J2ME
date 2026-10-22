/* Встроенная библиотека классов (собирается tools/build-rt.sh). */
#include "rt.h"
#include "rt_jar.h"

const unsigned char *bt_rt_jar = rt_jar;
const size_t bt_rt_jar_len = sizeof(rt_jar);
