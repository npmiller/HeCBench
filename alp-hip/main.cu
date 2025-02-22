#include <stdio.h>
#include <math.h>
#include <chrono>
#include <hip/hip_runtime.h>
#include "reference.h"
#include "kernels.h"

int main()
{
  double costheta = 0.3;
  int lmax = LMAX;
  #ifdef CHECK
  double h_plm[NDLM];
  #endif

  auto start = std::chrono::steady_clock::now();

  double *d_plm;
  hipMallocManaged(&d_plm, NDLM * sizeof(double));

  for (int l = 0; l <= lmax; l++) { 
    hipLaunchKernelGGL(associatedLegendre, 1, LMAX, 0, 0, costheta,l,d_plm);

    hipDeviceSynchronize();
    
    #ifdef CHECK
    // compute on host
    associatedLegendreFunctionNormalized<double>(costheta,l,h_plm);

    for(int i = 0; i <= l; i++)
    {
      if(fabs(h_plm[i] - d_plm[i]) > 1e-6)
      {
        fprintf(stderr, "%d: %lf != %lf\n", i, h_plm[i], d_plm[i]);
        break;
      }
    }
    #endif
  }
  hipFree(d_plm);

  auto end = std::chrono::steady_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  printf("Total execution time %f (s)\n", time * 1e-9f);

  return 0;
}
