typedef struct Model Model;

bool vrmlDumpStart(const char *filename);
void vrmlDumpSub(const Model *model, Mat4 world_mat, Vec3 model_scale, Vec3 color, void *uid);
void vrmlDumpFinish(void);
