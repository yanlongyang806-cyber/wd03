//// An implementation of the fast particle vertex shader done
//// entirely on the CPU.

typedef struct RdrDeviceDX RdrDeviceDX;
typedef struct RdrDrawableFastParticles RdrDrawableFastParticles;
typedef struct RdrPrimitiveTexturedVertex RdrPrimitiveTexturedVertex;

void rxbxFastParticlesVertexShader(RdrPrimitiveTexturedVertex *out_verts, const RdrDrawableFastParticles *particle_set);
