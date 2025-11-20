typedef struct ShaderCompileResponseData ShaderCompileResponseData;
typedef struct ShaderCompileRequestData ShaderCompileRequestData;

typedef enum ShaderServerRequestStatus {
	SHADER_SERVER_NOT_RUNNING,
	SHADER_SERVER_GOT_RESPONSE,
} ShaderServerRequestStatus;

typedef void (*ShaderServerCallback)(ShaderServerRequestStatus status, ShaderCompileResponseData *response, void *userData);

// Uses a reference to the data pointed to in request, caller must not free until the callback is called
void shaderServerRequestCompile(ShaderCompileRequestData *request, ShaderServerCallback callback, void *userData);
