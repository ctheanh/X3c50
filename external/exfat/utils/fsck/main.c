/*
	main.c (02.09.09)
	exFAT file system checker.

	Copyright (C) 2011-2013  Andrew Nayenko

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <exfat.h>
#include <exfatfs.h>
#include <inttypes.h>

#define exfat_debug(format, ...)

#define EXIT_SUCCESS		0
#define EXIT_ERRORS_CORRECTED	(1 << 0)
#define EXIT_ERRORS_UNCORRECTED	(1 << 2)
#define EXIT_OPERATIONAL_ERROR	(1 << 3)
#define EXIT_SYNTAX_ERROR	(1 << 4)

uint64_t files_count, directories_count;

static int nodeck(struct exfat* ef, struct exfat_node* node)
{
	const cluster_t cluster_size = CLUSTER_SIZE(*ef->sb);
	cluster_t clusters = (node->size + cluster_size - 1) / cluster_size;
	cluster_t c = node->start_cluster;
	int rc = 0;

	while (clusters--)
	{
		if (CLUSTER_INVALID(c))
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("file `%s' has invalid cluster 0x%x", name, c);
			rc = 1;
			break;
		}
		if (BMAP_GET(ef->cmap.chunk, c - EXFAT_FIRST_DATA_CLUSTER) == 0)
		{
			char name[EXFAT_NAME_MAX + 1];

			exfat_get_name(node, name, EXFAT_NAME_MAX);
			exfat_error("cluster 0x%x of file `%s' is not allocated", c, name);
			rc = 1;
		}
		c = exfat_next_cluster(ef, node, c);
	}
	return rc;
}

static int dirck(struct exfat* ef, const char* path)
{
	struct exfat_node* parent;
	struct exfat_node* node;
	struct exfat_iterator it;
	int rc;
	size_t path_length;
	char* entry_path;

	if (exfat_lookup(ef, &parent, path) != 0)
		exfat_bug("directory `%s' is not found", path);
	if (!(parent->flags & EXFAT_ATTRIB_DIR))
		exfat_bug("`%s' is not a directory (0x%x)", path, parent->flags);
	if (nodeck(ef, parent) != 0)
		return 0;

	path_length = strlen(path);
	entry_path = malloc(path_length + 1 + EXFAT_NAME_MAX);
	if (entry_path == NULL)
	{
		exfat_error("out of memory");
		return -ENOMEM;
	}
	strcpy(entry_path, path);
	strcat(entry_path, "/");

	rc = exfat_opendir(ef, parent, &it);
	if (rc != 0)
	{
		free(entry_path);
		exfat_put_node(ef, parent);
		exfat_error("failed to open directory `%s'", path);
		return 0;
	}
	while ((node = exfat_readdir(ef, &it)))
	{
		exfat_get_name(node, entry_path + path_length + 1, EXFAT_NAME_MAX);
		exfat_debug("%s: %s, %"PRIu64" bytes, cluster %u", entry_path,
				IS_CONTIGUOUS(*node) ? "contiguous" : "fragmented",
				node->size, node->start_cluster);
		if (node->flags & EXFAT_ATTRIB_DIR)
		{
			directories_count++;
			dirck(ef, entry_path);
			/* TODO: handle errors in deep directory tree */
		}
		else
		{
			files_count++;
			nodeck(ef, node);
			/* TODO: handle errors of file's nodeck() */
		}
		exfat_put_node(ef, node);
	}
	exfat_closedir(ef, &it);
	exfat_put_node(ef, parent);
	free(entry_path);
	return 0;
}

static int fsck(struct exfat* ef)
{
	exfat_print_info(ef->sb, exfat_count_free_clusters(ef));
	return dirck(ef, "");
}

static void usage(const char* prog)
{
	fprintf(stderr, "Usage: %s [-v] [-n|-y] <device>\n", prog);
	exit(EXIT_SYNTAX_ERROR);
}

int main(int argc, char* argv[])
{
	char** pp;
	const char* spec = NULL;
	char *options = "fsck,ro";	/* FIXME: interactive mode? */
	struct exfat ef;
	int result = EXIT_SUCCESS;

	printf("exfatfsck %u.%u.%u\n",
			EXFAT_VERSION_MAJOR, EXFAT_VERSION_MINOR, EXFAT_VERSION_PATCH);

	for (pp = argv + 1; *pp; pp++)
	{
		if (strcmp(*pp, "-n") == 0)
			options = "fsck,ro";
		else if (strcmp(*pp, "-y") == 0)
			options = "fsck,rw";
		else if (strcmp(*pp, "-v") == 0)
		{
			puts("Copyright (C) 2011-2013  Andrew Nayenko");
			return 0;
		}
		else if (spec == NULL)
			spec = *pp;
		else
			usage(argv[0]);
	}
	if (spec == NULL)
		usage(argv[0]);

	if (exfat_mount(&ef, spec, options) != 0)
		return EXIT_OPERATIONAL_ERROR;

	printf("Checking file system on %s.\n", spec);
	if (fsck(&ef) != 0)
		result |= EXIT_OPERATIONAL_ERROR;
	exfat_unmount(&ef);
	printf("Totally %"PRIu64" directories and %"PRIu64" files.\n",
			directories_count, files_count);

	fputs("File system checking finished. ", stdout);
	if (exfat_errors > 0)
	{
		printf("ERRORS FOUND/FIXED: %d/%d.\n", exfat_errors, exfat_fixes);
		if (exfat_fixes > 0)
			result |= EXIT_ERRORS_CORRECTED;
		if (exfat_fixes < exfat_errors)
			result |= EXIT_ERRORS_UNCORRECTED;
	}
	if (result == EXIT_SUCCESS)
		puts("No errors found.");
	return result;
}
