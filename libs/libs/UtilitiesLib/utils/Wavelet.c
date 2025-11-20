
#include "Wavelet.h"
#include "mathutil.h"


/**
 *  Forward biorthogonal 9/7 wavelet transform (lifting implementation)
 *
 *  x is an input signal, which will be replaced by its output transform.
 *  n is the length of the signal, and must be a power of 2.
 *
 *  The first half part of the output signal contains the approximation coefficients.
 *  The second half part contains the detail coefficients (aka. the wavelets coefficients).
 *
 */
void fastWaveletTransform(const F32* pfIn,int n, F32* pfOut)
{
  F32 a;
  F32 *pfTemp;
  int i;

  assert(isPower2(n));
  assert(n>4);

  pfTemp=_alloca(n*sizeof(F32));
  memcpy(pfTemp, pfIn, n*sizeof(F32));

  // Predict 1
  a=-1.586134342;
  for (i=1;i<n-2;i+=2)
    pfTemp[i]+=a*(pfTemp[i-1]+pfTemp[i+1]);
  pfTemp[n-1]+=2*a*pfTemp[n-2];

  // Update 1
  a=-0.05298011854;
  for (i=2;i<n;i+=2)
    pfTemp[i]+=a*(pfTemp[i-1]+pfTemp[i+1]);
  pfTemp[0]+=2*a*pfTemp[1];

  // Predict 2
  a=0.8829110762;
  for (i=1;i<n-2;i+=2)
    pfTemp[i]+=a*(pfTemp[i-1]+pfTemp[i+1]);
  pfTemp[n-1]+=2*a*pfTemp[n-2];

  // Update 2
  a=0.4435068522;
  for (i=2;i<n;i+=2)
    pfTemp[i]+=a*(pfTemp[i-1]+pfTemp[i+1]);
  pfTemp[0]+=2*a*pfTemp[1];

  // Scale
  a=1/1.149604398;
  for (i=0;i<n;i++)
  {
    if (i%2)
		pfTemp[i]*=a;
    else
		pfTemp[i]/=a;
  }

  // Pack
  for (i=0;i<n;i++)
  {
    if (i%2==0)
		pfOut[i/2]=pfTemp[i];
    else
		pfOut[n/2+i/2]=pfTemp[i];
  }
}

/**
 *  Inverse biorthogonal 9/7 wavelet transform
 *
 *  This is the inverse of freeWaveletTransform so that inverseWaveletTransform(freeWaveletTransform(x,n),n)=x for every signal x of length n.
 *
 */
void inverseWaveletTransform(const F32* pfIn,int n, F32* pfOut)
{
  F32 a;
  int i;
  assert(isPower2(n));
  assert(n>3);

  // Unpack
  for (i=0;i<n/2;i++)
  {
    pfOut[i*2]=pfIn[i];
    pfOut[i*2+1]=pfIn[i+n/2];
  }

  // Undo scale
  a=1.149604398;
  for (i=0;i<n;i++)
  {
    if (i%2)
		pfOut[i]*=a;
    else
		pfOut[i]/=a;
  }

  // Undo update 2
  a=-0.4435068522;
  for (i=2;i<n;i+=2)
    pfOut[i]+=a*(pfOut[i-1]+pfOut[i+1]);
  pfOut[0]+=2*a*pfOut[1];

  // Undo predict 2
  a=-0.8829110762;
  for (i=1;i<n-2;i+=2)
    pfOut[i]+=a*(pfOut[i-1]+pfOut[i+1]);
  pfOut[n-1]+=2*a*pfOut[n-2];

  // Undo update 1
  a=0.05298011854;
  for (i=2;i<n;i+=2)
    pfOut[i]+=a*(pfOut[i-1]+pfOut[i+1]);
  pfOut[0]+=2*a*pfOut[1];

  // Undo predict 1
  a=1.586134342;
  for (i=1;i<n-2;i+=2)
    pfOut[i]+=a*(pfOut[i-1]+pfOut[i+1]);
  pfOut[n-1]+=2*a*pfOut[n-2];
}