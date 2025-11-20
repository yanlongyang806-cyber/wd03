#include "earray.h"
#include "file.h"
#include "patchdb.h"
#include "pcl_client_internal.h"
#include "pcl_client_struct.h"
#include "StringUtil.h"
#include "timing.h"
#include "utils.h"

bool isLockedByClient(PCL_Client *client, DirEntry *dir)
{
	return dir->lockedby && !stricmp(dir->lockedby, NULL_TO_EMPTY(client->author));
}

void searchDbForDeletionsCB(DirEntry *dir, PatchFilescanData *userdata)
{
	PERFINFO_AUTO_START_FUNC_L2();

	if(dir->parent && !dir->name)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if( eaSize(&dir->versions) > 0 &&
		!(dir->versions[0]->flags & FILEVERSION_DELETED) &&
		(userdata->forceIfNotLockedByClient || isLockedByClient(userdata->client, dir)) )
	{
		int i;
		bool found_match = false;
		bool file_on_disk = false;

		for(i = 0; i < userdata->str_count; i++)
		{
			int j;
			char on_disk_buf[MAX_PATH], *on_disk, matchStr[MAX_PATH];
			const char *sub_path;
			bool ignored;

			if(!strStartsWith(dir->path, userdata->counts_as_strings[i]))
			{
				continue;
			}

			sub_path = dir->path + strlen(userdata->counts_as_strings[i]);
			if(sub_path[0] == '/')
			{
				sub_path++;
			}
			else if(sub_path[0] &&
					userdata->counts_as_strings[i][0])
			{
				// This isn't the root and the first subpath character wasn't a slash, so
				// it must just share a prefix.  Example:
				// Checking in 1 should ignore 2.
				// 1. data/stuff/things      subpath="/things"
				// 2. data/stuff 2/whatever  subpath=" 2/whatever"
				continue;
			}

			on_disk = on_disk_buf;
			strcpy(on_disk_buf, userdata->on_disk_strings[i]);
			if(sub_path[0])
			{
				if(on_disk[0] && on_disk[strlen(on_disk) - 1] != '/')
					strcat(on_disk_buf, "/");
				strcat(on_disk_buf, sub_path);
			}

			strcpy(matchStr, on_disk);
			string_toupper(matchStr);

			ignored = false;
			for(j = 0; j < eaSize(&userdata->hide); j++)
			{
				if(matchExact(userdata->hide[j], matchStr))
				{
					ignored = true;
					break;
				}
			}

			if(matchExact(userdata->wildcard_strings[i], matchStr))
			{
				found_match = true;
				if(!ignored && fileExists(on_disk))
				{
					file_on_disk = true;
					break;
				}
			}
		}

		if( (userdata->force_delete || found_match) && !file_on_disk)
		{
			pclMSpf("[not found] as %s will be deleted", dir->path);
			if(userdata->disk_names)
				eaPush(userdata->disk_names, NULL);
			if(userdata->db_names)
				eaPush(userdata->db_names, strdup(dir->path));
			if(userdata->diff_types)
				eaiPush(userdata->diff_types, PCLDIFF_DELETED);
		}
	}

	PERFINFO_AUTO_STOP_L2();
}
