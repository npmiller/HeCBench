/* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of NVIDIA CORPORATION nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <CL/sycl.hpp>

long long get_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000) + tv.tv_usec;
}

// Add two vectors on the GPU
void vectorAddGPU(float *__restrict a,
                  float *__restrict b,
                  float *__restrict c,
                  int N,
                  sycl::nd_item<1> &item) 
{
  int idx = item.get_global_id(0);
  if (idx < N) {
    c[idx] = a[idx] + b[idx];
  }
}

// Macro to aligned up to the memory size in question
#define MEMORY_ALIGNMENT 4096
#define ALIGN_UP(x, size) (((size_t)x + (size - 1)) & (~(size - 1)))

void eval(sycl::queue &q, bool bGenericSharedMemory) {
  int n, nelem;
  size_t bytes;
  float *a, *b, *c;           // Pinned memory allocated on the CPU
  float *a_UA, *b_UA, *c_UA;  // Non-4K Aligned Pinned memory on the CPU
  float errorNorm, refNorm, ref, diff;

  if (bGenericSharedMemory) {
    printf("> Using Generic Shared Memory (malloc_shared)\n");
  } else {
    printf("> Using Aligned Allocated Shared Memory (aligned_alloc_shared)\n");
  }

  // Allocate mapped CPU memory.

  for (nelem = 1024*1024; nelem <= (1024*1024*128); nelem = nelem*2) {
    bytes = nelem * sizeof(float);

    auto start = get_time();

    if (bGenericSharedMemory) {
      a_UA = (float *)malloc_shared(bytes + MEMORY_ALIGNMENT, q);
      b_UA = (float *)malloc_shared(bytes + MEMORY_ALIGNMENT, q);
      c_UA = (float *)malloc_shared(bytes + MEMORY_ALIGNMENT, q);

      // We need to ensure memory is aligned to 4K,
      // so we will need to pad memory accordingly
      a = (float *)ALIGN_UP(a_UA, MEMORY_ALIGNMENT);
      b = (float *)ALIGN_UP(b_UA, MEMORY_ALIGNMENT);
      c = (float *)ALIGN_UP(c_UA, MEMORY_ALIGNMENT);
    } else {
      a = (float *)sycl::aligned_alloc_shared(MEMORY_ALIGNMENT, bytes, q);
      b = (float *)sycl::aligned_alloc_shared(MEMORY_ALIGNMENT, bytes, q);
      c = (float *)sycl::aligned_alloc_shared(MEMORY_ALIGNMENT, bytes, q);
    }

    // Initialize the vectors
    for (n = 0; n < nelem; n++) {
      a[n] = rand() / (float)RAND_MAX;
      b[n] = rand() / (float)RAND_MAX;
    }

    // Call the GPU kernel using the pointers residing in shared memory.
    sycl::range<1> lws (256);
    sycl::range<1> gws (256 * (unsigned int)ceil(nelem / 256.f));

    q.parallel_for(sycl::nd_range<1>(gws, lws), [=](sycl::nd_item<1> item) {
      vectorAddGPU(a, b, c, nelem, item);
    }).wait();

    // Compare the results

    errorNorm = 0.f;
    refNorm = 0.f;

    for (n = 0; n < nelem; n++) {
      ref = a[n] + b[n];
      diff = c[n] - ref;
      errorNorm += diff * diff;
      refNorm += ref * ref;
    }

    errorNorm = (float)sqrt((double)errorNorm);
    refNorm = (float)sqrt((double)refNorm);

    printf("%s ", (errorNorm / refNorm < 1.e-6f) ? "SUCCESS" : "FAILURE");

    // Memory clean up

    if (bGenericSharedMemory) {
      sycl::free(a_UA, q);
      sycl::free(b_UA, q);
      sycl::free(c_UA, q);
    } else {
      sycl::free(a, q);
      sycl::free(b, q);
      sycl::free(c, q);
    }

    auto end = get_time();
    printf("Total elapsed time (vector length = %d): %.2f s\n", 
           nelem, (end - start) / 1e6f);
  }
}

int main(int argc, char **argv) {

#ifdef USE_GPU
  sycl::gpu_selector dev_sel;
#else
  sycl::cpu_selector dev_sel;
#endif
  sycl::queue q(dev_sel);

  bool bGenericSharedMemory;

  bGenericSharedMemory = false;
  eval(q, bGenericSharedMemory); 

  bGenericSharedMemory = true;
  eval(q, bGenericSharedMemory); 
  return 0;
}
