#include <string.h>

#include "destination_manager.h"

bool route_path_valid(const char *path, const char **error)
{
	const size_t len = strlen(path);

	if (len == 0U) {
		*error = "empty code";
		return false;
	}

	if (len > ROUTE_CODE_MAX_LEN) {
		*error = "code too long";
		return false;
	}

	if (path[len - 1] != '0') {
		*error = "code must end with 0";
		return false;
	}

	for (size_t i = 0; i + 1 < len; i++) {
		const char c = path[i];

		if (c == '0') {
			*error = "0 only allowed at the end";
			return false;
		}

		if (c != 'L' && c != 'R' && c != 'S') {
			*error = "only L R S 0 allowed";
			return false;
		}
	}

	return true;
}
