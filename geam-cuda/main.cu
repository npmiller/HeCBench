#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <utility>
#include <cublas_v2.h>

#define uS_PER_SEC 1000000
#define uS_PER_mS 1000

int main(int argc, char* argv[]) {
  if (argc != 4) {
    printf("Usage %s <matrix col> <matrix row> <repeat>\n", argv[0]);
    return 1;
  }

  int M = atoi(argv[1]); 
  int N = atoi(argv[2]); 
  const int repeat = atoi(argv[3]); 
  if (M <= 0 || N <= 0 || repeat < 0) {
    printf("Error: invalid inputs\n");
    return 1;
  }

  int error = 0;
  const size_t size = N * M;
  const size_t size_byte = size * sizeof(float);

  // N rows and M columns
  float *matrix = (float *) malloc (size_byte);
  for (size_t i = 0; i < size; i++) matrix[i] = rand() % 13; 

  timeval t1, t2;
  gettimeofday(&t1, NULL);

  // M rows and N columns
  float *matrixT = (float *) malloc (size_byte);
  for (int i = 0; i < N; i++)
    for (int j = 0; j < M; j++)
      matrixT[(j*N)+i] = matrix[(i*M)+j];

  gettimeofday(&t2, NULL);
  float et1 = (((t2.tv_sec*uS_PER_SEC)+t2.tv_usec) - ((t1.tv_sec*uS_PER_SEC)+t1.tv_usec))/(float)uS_PER_mS;
  printf("CPU time = %fms\n", et1);

  cublasHandle_t handle;
  cublasCreate(&handle);

  const float alpha = 1.f;
  const float beta  = 0.f;

  // store host and device results
  float *h_matrixT , *d_matrixT , *d_matrix;
  h_matrixT = (float *) malloc (size_byte);
  cudaMalloc((void**)&d_matrixT , size_byte);
  cudaMalloc((void**)&d_matrix , size_byte);

  cudaMemcpy(d_matrix , matrix , size_byte, cudaMemcpyHostToDevice);

  // start the device timing
  gettimeofday(&t1, NULL);

  for (int i = 0; i < repeat; i++) {
    auto status = cublasSgeam(handle, CUBLAS_OP_T, CUBLAS_OP_N, N, M, &alpha, 
                              d_matrix, M, &beta, d_matrix, N, d_matrixT, N);

    if (status != CUBLAS_STATUS_SUCCESS) {
      error = 1;
      printf("Error: cublasSgeam failed to complete\n"):
      break;
    }

    std::swap(d_matrix, d_matrixT);
    std::swap(N, M);
  }

  gettimeofday(&t2, NULL);
  float et2 = (((t2.tv_sec*uS_PER_SEC)+t2.tv_usec) - 
               ((t1.tv_sec*uS_PER_SEC)+t1.tv_usec)) / (float)uS_PER_mS;

  printf("Average device execution time = %fms\n", et2 / repeat);

  cudaMemcpy(h_matrixT , d_matrix , size_byte , cudaMemcpyDeviceToHost);

  cudaFree(d_matrix);
  cudaFree(d_matrixT);

  if (error == 0) {

    // check host and device results
    if (repeat % 2)
      error = memcmp(h_matrixT, matrixT, size_byte);
    else
      error = memcmp(h_matrixT, matrix, size_byte);
  }

  printf("%s\n", error ? "FAIL" : "PASS");

  cublasDestroy(handle);
  free(matrixT);
  free(matrix);
  return 0;
}
