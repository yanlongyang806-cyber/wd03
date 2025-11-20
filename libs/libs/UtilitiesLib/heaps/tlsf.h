// This can be used:
//   a) as a heap replacement (call tlsf_init_memory_pool() once,
//   and never call it again, and use the tlsf_malloc() interface),
//   - need to enable TLSF_USE_LOCKS in the .c file
// OR
//   b) for custom heaps (call tlsf_init_memory_pool(), and use the
//   tlsf_malloc_ex() interface)
// But not both (shouldn't be hard to fix if needed).  Right now
//   we're not doing (a) so feel free to use it for (b)

/*
 * Two Levels Segregate Fit memory allocator (TLSF)
 * Version 2.4.6
 *
 * Written by Miguel Masmano Tello <mimastel@doctor.upv.es>
 *
 * Thanks to Ismael Ripoll for his suggestions and reviews
 *
 * Copyright (C) 2008, 2007, 2006, 2005, 2004
 *
 * This code is released using a dual license strategy: GPL/LGPL
 * You can choose the license that better fits your requirements.
 *
 * Released under the terms of the GNU General Public License Version 2.0
 * Released under the terms of the GNU Lesser General Public License Version 2.1
 *

 For the GPL license, the following exception applies.

 TLSF is free software; you can redistribute it and/or modify it under terms of
 the GNU General Public License as published by the Free Software Foundation;
 either version 2, or (at your option) any later version. TLSF is distributed
 in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 the GNU General Public License for more details. You should have received a
 copy of the GNU General Public License along with TLSF; see file COPYING. If
 not, write to the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.

 As a special exception, including TLSF header files in a file, or linking with
 other files objects to produce an executable application, is merely considered
 normal use of the library, and does *not* fall under the heading of "derived
 work". Therfore does not by itself cause the resulting executable application
 to be covered by the GNU General Public License. This exception does not
 however invalidate any other reasons why the executable file might be covered
 by the GNU Public License.

 */

#ifndef _TLSF_H_
#define _TLSF_H_

#include <sys/types.h>

extern size_t tlsf_init_memory_pool(size_t mem_pool_size, void *mem_pool);
extern size_t tlsf_get_used_size(void *mem_pool);
extern size_t tlsf_get_max_size(void *mem_pool);
extern void tlsf_destroy_memory_pool(void *mem_pool);
extern size_t tlsf_add_new_area(void *area, size_t area_size, void *mem_pool);
extern void *tlsf_malloc_ex(size_t size, void *mem_pool);
extern void tlsf_free_ex(void *ptr, void *mem_pool);
extern void *tlsf_realloc_ex(void *ptr, size_t new_size, void *mem_pool);
extern void *tlsf_calloc_ex(size_t nelem, size_t elem_size, void *mem_pool);

extern void *tlsf_malloc(size_t size);
extern void tlsf_free(void *ptr);
extern void *tlsf_realloc(void *ptr, size_t size);
extern void *tlsf_calloc(size_t nelem, size_t elem_size);

#endif
