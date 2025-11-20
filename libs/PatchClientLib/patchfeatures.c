#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "patchcommonutils.h"
#include "patchtrivia.h"

#include "trivia.h"
#include "file.h"
#include "timing.h"

#include "pcl_client_struct_h_ast.h"

PCL_ErrorCode pclCheckCurrentLink(const char * dir, char * ip_buf, int ip_buf_size, int * port, char * project_buf, int project_buf_size,
								  int * branch, bool * no_upload, char * link_dir_buf, int link_dir_buf_size)
{
	char path[MAX_PATH], link_file[MAX_PATH], * found;
	int len;

	PERFINFO_AUTO_START_FUNC();

	strcpy(path, dir);
	forwardSlashes(path);

	len = (int)strlen(path);
	if(path[len - 1] == '/')
		path[--len] = '\0';

	while(len > 0)
	{
		sprintf(link_file, "%s/%s/%s", path, PATCH_DIR, CONNECTION_FILE_NAME);
		if(fileExists(link_file))
		{
			LinkInfo link_info = {0};

			ParserReadTextFile(link_file, parse_LinkInfo, &link_info, 0);

			if(branch)
				*branch = link_info.branch;

			if(ip_buf)
			{
				if(link_info.ip)
					strcpy_s(ip_buf, ip_buf_size, link_info.ip);
				else
					ip_buf[0] = '\0';
			}

			if(no_upload)
				*no_upload = link_info.no_upload;

			if(port)
				*port = link_info.port;

			if(project_buf)
			{
				if(link_info.project)
					strcpy_s(project_buf, project_buf_size, link_info.project);
				else
					project_buf[0] = '\0';
			}

			if(link_dir_buf)
			{
				strcpy_s(link_dir_buf, link_dir_buf_size, path);
			}

			StructDeInit(parse_LinkInfo, &link_info);

			return PCL_SUCCESS;
		}

		found = strrchr(path, '/');
		if(found)
			found[0] = '\0';
		else
			path[0] = '\0';
		len = (int)strlen(path);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_NO_CONNECTION_FILE;
}

PCL_ErrorCode pclCheckCurrentView(const char * dir, char * label_buf, int label_buf_size, U32 * view_time, int * branch, char * sandbox_buf,
								  int sandbox_buf_size, char * view_dir_buf, int view_dir_buf_size)
{
	char fname[MAX_PATH];
	TriviaList *trivia;

	PERFINFO_AUTO_START_FUNC();

	machinePath(fname, dir);
	trivia = triviaListGetPatchTriviaForFile(fname, view_dir_buf, view_dir_buf_size);
	if(!trivia)
		return PCL_NO_VIEW_FILE;

	if(label_buf)
	{
		const char *label_trivia = triviaListGetValue(trivia, "PatchName");
		strcpy_s(label_buf, label_buf_size, NULL_TO_EMPTY(label_trivia));
	}

	if(view_time)
	{
		const char *time_trivia = triviaListGetValue(trivia, "PatchTime");
		*view_time = time_trivia ? atoi(time_trivia) : 0;
	}

	if(branch)
	{
		const char *branch_trivia = triviaListGetValue(trivia, "PatchBranch");
		*branch = branch_trivia ? atoi(branch_trivia) : 0;
	}

	if(sandbox_buf)
	{
		const char *sandbox_trivia = triviaListGetValue(trivia, "PatchSandbox");
		strcpy_s(sandbox_buf, sandbox_buf_size, NULL_TO_EMPTY(sandbox_trivia));
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}
