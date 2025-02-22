#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <random>
#include "common.h"
#include "kernels.h"

// transpose
double* t(const double *idata, const int width, const int height)
{
  double *odata = (double*) malloc (sizeof(double) * width * height); 
  for (int yIndex = 0; yIndex < height; yIndex++) {
    for (int xIndex = 0; xIndex < width; xIndex++) {
      int index_in  = xIndex + width * yIndex;
      int index_out = yIndex + height * xIndex;
      odata[index_out] = idata[index_in];
    }
  }
  return odata;
}

int main(int argc, char* argv[]) {

  char *filename = argv[1];

  // n and K should match the dimension of the dataset in the csv file
  const int n = 26280, K = 21, M = 10000;

  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf("Error: failed to open file alphas.csv. Exit\n");
    return 1;
  }

  int alphas_size = n * K; // n rows and K cols
  int alphas_size_byte = n * K * sizeof(double);

  int rands_size = M * K;  // M rows and K cols
  int rands_size_byte = M * K * sizeof(double);

  double *alphas, *rands, *probs;
  alphas = (double*) malloc (alphas_size_byte);
  rands = (double*) malloc (rands_size_byte);
  probs = (double*) malloc (alphas_size_byte);

  // load the csv file 
  for (int i = 0; i < alphas_size; i++)
    fscanf(fp, "%lf", &alphas[i]);
  fclose(fp);

  // normal distribution (mean: 0 and var: 1)
  std::mt19937 gen(19937);
  std::normal_distribution<double> norm_dist(0.0,1.0);
  for (int i = 0; i < rands_size; i++) rands[i] = norm_dist(gen); 

#ifdef USE_GPU
  gpu_selector dev_sel;
#else
  cpu_selector dev_sel;
#endif
  queue q(dev_sel);

  double *d_alphas, *d_rands, *d_probs;
  d_rands = (double*) sycl::malloc_device(rands_size_byte, q);
  d_alphas = (double*) sycl::malloc_device(alphas_size_byte, q);
  d_probs = (double*) sycl::malloc_device(alphas_size_byte, q);
  auto e1 = q.memcpy(d_rands, rands, rands_size_byte);
  auto e2 = q.memcpy(d_alphas, alphas, alphas_size_byte);

  // kernel 1
  int threads_per_block = 192;
  range<1> lws (threads_per_block);
  range<1> gws (ceil(n/threads_per_block) * threads_per_block);

  auto e3 = q.memset(d_probs, 0.0, alphas_size_byte);
  auto e_k1 = q.submit([&] (handler &cgh) {
    cgh.depends_on({e1, e2, e3});
    cgh.parallel_for<class k1>(nd_range<1>(gws, lws), [=] (nd_item<1> item) {
      compute_probs(d_alphas, d_rands, d_probs, n, K, M, item);
    });
  });
  q.memcpy(probs, d_probs, alphas_size_byte, e_k1).wait();

  double s = 0.0;
  for (int i = 0; i < alphas_size; i++) s += probs[i];
  printf("compute_probs: checksum = %lf\n", s);

  // kernel 2
  double *t_rands = t(rands, K, M);
  double *t_alphas = t(alphas, K, n);
  e1 = q.memcpy(d_rands, t_rands, rands_size_byte);
  e2 = q.memcpy(d_alphas, t_alphas, alphas_size_byte);

  e3 = q.memset(d_probs, 0.0, alphas_size_byte);
  auto e_k2 = q.submit([&] (handler &cgh) {
    cgh.depends_on({e1, e2, e3});
    cgh.parallel_for<class k2>(nd_range<1>(gws, lws), [=] (nd_item<1> item) {
      compute_probs_unitStrides(d_alphas, d_rands, d_probs, n, K, M, item);
    });
  });
  q.memcpy(probs, d_probs, alphas_size_byte, e_k2).wait();

  s = 0.0;
  for (int i = 0; i < alphas_size; i++) s += probs[i];
  printf("compute_probs_unitStrides: checksum = %lf\n", s);

  // kernel 3
  threads_per_block = 96;
  range<1> lws2 (threads_per_block);
  range<1> gws2 (ceil(n/threads_per_block) * threads_per_block);

  e3 = q.memset(d_probs, 0.0, alphas_size_byte);
  auto e_k3 = q.submit([&] (handler &cgh) {
    cgh.depends_on(e3);
    accessor<double, 1, sycl_read_write, sycl_lmem> sm (K * threads_per_block * 2, cgh);
    cgh.parallel_for<class k3>(nd_range<1>(gws2, lws2), [=] (nd_item<1> item) {
      compute_probs_unitStrides_sharedMem(d_alphas, d_rands, d_probs, n, K, M, sm.get_pointer(), item);
    });
  });
  q.memcpy(probs, d_probs, alphas_size_byte, e_k3).wait();

  s = 0.0;
  for (int i = 0; i < alphas_size; i++) s += probs[i];
  printf("compute_probs_unitStrides_sharedMem: checksum = %lf\n", s);

  // free memory 
  sycl::free(d_alphas, q);
  sycl::free(d_rands, q);
  sycl::free(d_probs, q);
  free(alphas);
  free(rands);
  free(t_alphas);
  free(t_rands);
  free(probs);
  return 0;
}
