#pragma once

#ifdef CO_DEBUG_LOG

#include "co/log.h"

#define CO_DLOG log::debug

#else

#define CO_DLOG(...)

#endif
