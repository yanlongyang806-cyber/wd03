#include "jira.h"

// Find a substring like "[COR-12345]" in a string.
const char *jiraFindIssueString(const char *string, size_t max_match_distance, const char **end, const char **project_key, const char **project_key_end, U64 *issue_number)
{
	size_t len = strlen(string);
	size_t pos;
	const char *project_key_result;
	const char *project_key_end_result;
	U64 issue_number_result;
	const char minimum[] = "[X-1]";

#define MAX_JIRA_PROJECT_KEY_LENGTH 255  // From http://confluence.atlassian.com/display/JIRA/Configuring+Project+Keys
#define MAX_JIRA_ISSUE_NUMBER_LENGTH 20  // U64 max, just guessing

	if (!string)
		return NULL;

	// Clamp to max_match_distance.
	if (max_match_distance)
		len = MIN(max_match_distance, len);

	// Search for matches in the string.
	for (pos = 0; pos + sizeof(minimum) - 1 <= len; ++pos)
	{
		const char *current = string + pos;
		int count;
		char *next;
		const char *i;

		// Consume [.
		if (*current != '[')
			continue;
		++current;

		// Consume project key.
		if (!isalnum(*current))
			continue;
		project_key_result = current;
		++current;
		count = 0;
		while (isalnum(*current))
		{
			++current;
			++count;
			if (count > MAX_JIRA_PROJECT_KEY_LENGTH)
				break;
		}
		if (count > MAX_JIRA_PROJECT_KEY_LENGTH)
			continue;
		project_key_end_result = current;

		// Consume -.
		if (*current != '-')
			continue;
		++current;

		// Consume issue number.
		errno = 0;
		issue_number_result = _strtoui64(current, &next, 10);
		if (!issue_number_result && errno)
			continue;
		for (i = current; i != next; ++i)
			if (!isdigit(*i))
				break;
		if (i != next)
			continue;
		current = next;

		// ] means we found a match.
		if (*current == ']')
		{
			if (end)
				*end = current + 1;
			if (project_key)
				*project_key = project_key_result;
			if (project_key_end)
				*project_key_end = project_key_end_result;
			if (issue_number)
				*issue_number = issue_number_result;

			return string + pos;
		}
	}

	return NULL;
}
