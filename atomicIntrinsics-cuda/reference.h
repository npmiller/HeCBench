#define min(a,b) (a) < (b) ? (a) : (b)
#define max(a,b) (a) > (b) ? (a) : (b)

////////////////////////////////////////////////////////////////////////////////
//! Compute reference data set
//! Each element is multiplied with the number of threads / array length
//! @param reference  reference data, computed but preallocated
//! @param idata      input data as provided to device
//! @param len        number of elements in reference / idata
////////////////////////////////////////////////////////////////////////////////
template <class T>
void computeGold(T *gpuData, const int len)
{
  T val = 0;

  for (int i = 0; i < len; ++i)
  {
    val += (T)10;
  }

  if (val != gpuData[0])
  {
    printf("Add failed %d %d\n", val, gpuData[0]);
  }

  val = 0;

  for (int i = 0; i < len; ++i)
  {
    val -= (T)10;
  }

  if (val != gpuData[1])
  {
    printf("Sub failed: %d %d\n", val, gpuData[1]);
  }

  val = (T)(-256);

  for (int i = 0; i < len; ++i)
  {
    val = max(val, (T)i);
  }

  if (val != gpuData[2])
  {
    printf("Max failed: %d %d\n", val, gpuData[2]);
  }

  val = (T)256;

  for (int i = 0; i < len; ++i)
  {
    val = min(val, (T)i);
  }

  if (val != gpuData[3])
  {
    printf("Min failed: %d %d\n", val, gpuData[3]);
  }

  val = 0xff;

  for (int i = 0; i < len; ++i)
  {
    val &= (T)(2 * i + 7);
  }

  if (val != gpuData[4])
  {
    printf("And failed: %d %d\n", val, gpuData[4]);
  }

  val = 0;

  for (int i = 0; i < len; ++i)
  {
    val |= (T)(1 << i);
  }

  if (val != gpuData[5])
  {
    printf("Or failed: %d %d\n", val, gpuData[5]);
  }

  val = 0xff;

  for (int i = 0; i < len; ++i)
  {
    val ^= (T)i;
  }

  if (val != gpuData[6])
  {
    printf("Xor failed %d %d\n", val, gpuData[6]);
  }

  printf("PASS\n");
}
