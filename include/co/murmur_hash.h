/*
 * Murmurhash from http://sites.google.com/site/murmurhash.
 * Written by Austin Appleby, and is placed to the public domain.
 * For business purposes, Murmurhash is under the MIT license.
 */

#pragma once

#include "def.h"

namespace co {

uint32 murmur_hash32(const void* s, size_t n, uint32 seed=0);

uint64 murmur_hash64(const void* s, size_t n, uint64 seed=0);

#if __arch64
inline size_t murmur_hash(const void* s, size_t n, size_t seed=0) {
    return murmur_hash64(s, n, seed);
}

#else
inline size_t murmur_hash(const void* s, size_t n, size_t seed=0) {
    return murmur_hash32(s, n, seed);
}
#endif

} // co
