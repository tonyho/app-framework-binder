/*
 Copyright 2015 IoT.bzh

 author: José Bollo <jose.bollo@iot.bzh>

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "locale-root.h"

/*
 * Implementation of folder based localisation as described here:
 *
 *    https://www.w3.org/TR/widgets/#folder-based-localization
 */

#define LRU_COUNT 3

static const char locales[] = "locales/";

struct locale_folder {
	struct locale_folder *parent;
	size_t length;
	char name[1];
};

struct locale_container {
	size_t maxlength;
	size_t count;
	struct locale_folder **folders;
};

struct locale_search_node {
	struct locale_search_node *next;
	struct locale_folder *folder;
};

struct locale_root;

struct locale_search {
	struct locale_root *root;
	struct locale_search_node *head;
	int refcount;
	char definition[1];
};

struct locale_root {
	int refcount;
	int refcount2;
	int rootfd;
	struct locale_container container;
	struct locale_search *lru[LRU_COUNT];
};

/* a valid subpath is a relative path not looking deeper than root using .. */
static int validsubpath(const char *subpath)
{
	int l = 0, i = 0;

	/* absolute path is not valid */
	if (subpath[i] == '/')
		return 0;

	/* inspect the path */
	while(subpath[i]) {
		switch(subpath[i++]) {
		case '.':
			if (!subpath[i])
				break;
			if (subpath[i] == '/') {
				i++;
				break;
			}
			if (subpath[i++] == '.') {
				if (!subpath[i]) {
					l--;
					break;
				}
				if (subpath[i++] == '/') {
					l--;
					break;
				}
			}
		default:
			while(subpath[i] && subpath[i] != '/')
				i++;
			if (l >= 0)
				l++;
		case '/':
			break;
		}
	}
	return l >= 0;
}

/*
 * Normalizes and checks the 'subpath'.
 * Removes any starting '/' and checks that 'subpath'
 * does not contains sequence of '..' going deeper than
 * root.
 * Returns the normalized subpath or NULL in case of
 * invalid subpath.
 */
static const char *normalsubpath(const char *subpath)
{
	while(*subpath == '/')
		subpath++;
	return validsubpath(subpath) ? subpath : NULL;
}

/*
 * Clear a container content
 */
static void clear_container(struct locale_container *container)
{
	while(container->count)
		free(container->folders[--container->count]);
	free(container->folders);
}

/*
 * Adds a folder of name for the container
 */
static int add_folder(struct locale_container *container, const char *name)
{
	size_t count, length;
	struct locale_folder **folders;

	count = container->count;
	folders = realloc(container->folders, (1 + count) * sizeof *folders);
	if (folders != NULL) {
		container->folders = folders;
		length = strlen(name);
		folders[count] = malloc(sizeof **folders + length);
		if (folders[count] != NULL) {
			folders[count]->parent = NULL;
			folders[count]->length = length;
			memcpy(folders[count]->name, name, 1 + length);
			container->count = count + 1;
			if (length > container->maxlength)
				container->maxlength = length;
			return 0;
		}
	}
	clear_container(container);
	errno = ENOMEM;
	return -1;
}

/*
 * Compare two folders for qsort
 */
static int compare_folders_for_qsort(const void *a, const void *b)
{
	const struct locale_folder * const *fa = a, * const *fb = b;
	return strcasecmp((*fa)->name, (*fb)->name);
}

/*
 * Search for a folder
 */
static struct locale_folder *search_folder(struct locale_container *container, const char *name, size_t length)
{
	size_t low, high, mid;
	struct locale_folder *f;
	int c;

	low = 0;
	high = container->count;
	while (low < high) {
		mid = (low + high) >> 1;
		f = container->folders[mid];
		c = strncasecmp(f->name, name, length);
		if (c == 0 && f->name[length] == 0)
			return f;
		if (c >= 0)
			high = mid;
		else	
			low = mid + 1;
	}
	return NULL;
}

/*
 * Init a container
 */
static int init_container(struct locale_container *container, int dirfd)
{
	int rc, sfd;
	DIR *dir;
	struct dirent dent, *e;
	struct stat st;
	size_t i, j;
	struct locale_folder *f;

	/* init the container */
	container->maxlength = 0;
	container->count = 0;
	container->folders = NULL;

	/* scan the subdirs */
	sfd = openat(dirfd, locales, O_DIRECTORY|O_RDONLY);
	if (sfd == -1)
		return (errno == ENOENT) - 1;

	/* get the directory data */
	dir = fdopendir(sfd);
	if (dir == NULL) {
		close(sfd);
		return -1;
	}

	/* enumerate the entries */
	for(;;) {
		/* next entry */
		rc = readdir_r(dir, &dent, &e);
		if (rc < 0) {
			/* error */
			closedir(dir);
			return rc;
		}
		if (e == NULL) {
			/* end of entries */
			closedir(dir);
			break;
		}
		if (dent.d_type == DT_DIR || (dent.d_type == DT_UNKNOWN && fstatat(sfd, dent.d_name, &st, 0) == 0 && S_ISDIR(st.st_mode))) {
			/* directory aka folder */
			if (dent.d_name[0] == '.' && (dent.d_name[1] == 0 || (dent.d_name[1] == '.' && dent.d_name[2] == 0))) {
				/* nothing to do for special directories, basic detection, improves if needed */
			} else {
				rc = add_folder(container, dent.d_name);
				if (rc < 0) {
					closedir(dir);
					return rc;
				}
			}
		}
	}

	/* sort the folders */
	qsort(container->folders, container->count, sizeof *container->folders, compare_folders_for_qsort);

	/* build the parents links */
	i = container->count;
	while (i != 0) {
		f = container->folders[--i];
		j = strlen(f->name);
		while (j != 0 && f->parent == NULL) {
			if (f->name[--j] == '-')
				f->parent = search_folder(container, f->name, j);
		}
	}

	return rc;
}

/*
 * Creates a locale root handler and returns it or return NULL
 * in case of memory depletion.
 */
struct locale_root *locale_root_create(int dirfd, const char *pathname)
{
	int rfd;
	struct locale_root *root;
	size_t i;

	rfd = (pathname && *pathname) ? openat(dirfd, pathname, O_PATH|O_DIRECTORY) : dirfd >= 0 ? dup(dirfd) : dirfd;
	if (rfd >= 0 || (!(pathname && *pathname) && dirfd < 0)) {
		root = calloc(1, sizeof * root);
		if (root == NULL)
			errno = ENOMEM;
		else {
			if (init_container(&root->container, rfd) == 0) {
				root->rootfd = rfd;
				root->refcount = 1;
				root->refcount2 = 1;
				for(i = 0 ; i < LRU_COUNT ; i++)
					root->lru[i] = NULL;
				return root;
			}
			free(root);
		}
		close(rfd);
	}
	return NULL;
}

/*
 * Adds a reference to 'root'
 */
struct locale_root *locale_root_addref(struct locale_root *root)
{
	root->refcount++;
	return root;
}

/*
 * Drops a reference to 'root' and destroys it
 * if not more referenced
 */
static void locale_root_unref2(struct locale_root *root)
{
	if (!--root->refcount2) {
		clear_container(&root->container);
		close(root->rootfd);
		free(root);
	}
}

/*
 * Drops a reference to 'root' and destroys it
 * if not more referenced
 */
void locale_root_unref(struct locale_root *root)
{
	size_t i;

	if (root != NULL && !--root->refcount) {
		/* clear circular references through searchs */
		for (i = 0 ; i < LRU_COUNT ; i++)
			locale_search_unref(root->lru[i]);
		/* finalize if needed */
		locale_root_unref2(root);
	}
}

/*
 * append, if needed, a folder to the search
 */
static int search_append_folder(struct locale_search *search, struct locale_folder *folder)
{
	struct locale_search_node **p, *n;

	/* search an existing node */
	p = &search->head;
	n = search->head;
	while(n != NULL) {
		if (n->folder == folder)
			return 0;
		p = &n->next;
		n = n->next;
	}

	/* allocates a new node */
	n = malloc(sizeof *n);
	if (n == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* init the node */
	*p = n;
	n->folder = folder;
	n->next = NULL;
	return 1;
}

/*
 * construct a search for the given root and definition of length
 */
static struct locale_search *create_search(struct locale_root *root, const char *definition, size_t length, int immediate)
{
	struct locale_search *search;
	size_t stop, back;
	struct locale_folder *folder;
	struct locale_search_node *node;

	/* allocate the structure */
	search = malloc(sizeof *search + length);
	if (search != NULL) {
		/* init */
		root->refcount2++;
		search->root = root;
		search->head = NULL;
		search->refcount = 1;
		memcpy(search->definition, definition, length);
		search->definition[length] = 0;

		/* build the search from the definition */
		while(length > 0) {
			stop = 0;
			while(stop < length && definition[stop] != ',' && definition[stop] != ';')
				stop++;
			back = stop;
			while (back > 0 && (definition[back] == ' ' || definition[back] == '\t'))
				back--;
			while (back > 0) {
				folder = search_folder(&root->container, definition, back);
				if (folder) {
					if (search_append_folder(search, folder) < 0) {
						locale_search_unref(search);
						return NULL;
					}
					if (!immediate)
						break;
				}
				while(back > 0 && definition[--back] != '-');
				while(back > 0 && definition[back] == '-' && definition[back-1] == '-') back--;
			}
			while (stop < length && definition[stop] != ',')
				stop++;
			while (stop < length && (definition[stop] == ',' || definition[stop] == ' ' || definition[stop] == '\t'))
				stop++;
			definition += stop;
			length -= stop;
		}

		/* fullfills the search */
		node = search->head;
		while(node != NULL) {
			folder = node->folder->parent;
			if (folder != NULL) {
				if (search_append_folder(search, node->folder->parent) < 0) {
					locale_search_unref(search);
					return NULL;
				}
			}
			node = node->next;
		}
	}
	return search;
}

/*
 * Check if a possibly NUUL search matches the definition of length
 */
static inline int search_matches(struct locale_search *search, const char *definition, size_t length)
{
	return search != NULL && strncasecmp(search->definition, definition, length) == 0 && search->definition[length] == '\0';
}

/*
 * Get an instance of search for the given root and definition
 * The flag immediate affects how the search is built.
 * For example, if the definition is "en-US,en-GB,en", the result differs depending on
 * immediate or not:
 *  when immediate==0 the search becomes "en-US,en-GB,en"
 *  when immediate==1 the search becomes "en-US,en,en-GB" because en-US is immediately downgraded to en
 */
struct locale_search *locale_root_search(struct locale_root *root, const char *definition, int immediate)
{
	char c;
	size_t i, length;
	struct locale_search *search;

	/* normalize the definition */
	c = definition != NULL ? *definition : 0;
	while (c == ' ' || c == '\t' || c == ',')
		c = *++definition;
	length = 0;
	while (c)
		c = definition[++length];
	if (length) {
		c = definition[length - 1];
		while ((c == ' ' || c == '\t' || c == ',') && length)
			c = definition[--length - 1];
	}

	/* search lru entry */
	i = 0;
	while (i < LRU_COUNT && !search_matches(root->lru[i], definition, length))
		i++;

	/* get the entry */
	if (i < LRU_COUNT) {
		/* use an existing one */
		search = root->lru[i];
	} else {
		/* create a new one */
		search = create_search(root, definition, length, immediate);
		if (search == NULL)
			return NULL;
		/* drop the oldest reference and update i */
		locale_search_unref(root->lru[--i]);
	}

	/* manage the LRU */
	while (i > 0) {
		root->lru[i] = root->lru[i - 1];
		i = i - 1;
	}
	root->lru[0] = search;

	/* returns a new instance */
	return locale_search_addref(search);
}

/*
 * Adds a reference to the search
 */
struct locale_search *locale_search_addref(struct locale_search *search)
{
	search->refcount++;
	return search;
}

/*
 * Removes a reference from the search
 */
void locale_search_unref(struct locale_search *search)
{
	struct locale_search_node *it, *nx;

	if (search && !--search->refcount) {
		it = search->head;
		while(it != NULL) {
			nx = it->next;
			free(it);
			it = nx;
		}
		locale_root_unref2(search->root);
		free(search);
	}
}

/*
 * Opens 'filename' after search.
 *
 * Returns the file descriptor as returned by openat
 * system call or -1 in case of error.
 */
int locale_search_open(struct locale_search *search, const char *filename, int mode)
{
	size_t maxlength, length;
	char *buffer, *p;
	struct locale_search_node *node;
	struct locale_folder *folder;
	int rootfd, fd;

	/* no creation mode accepted */
	if ((mode & O_CREAT) != 0)
		goto inval;

	/* check the path and normalize it */
	filename = normalsubpath(filename);
	if (filename == NULL)
		goto inval;

	/* search for folders */
	rootfd = search->root->rootfd;
	node = search->head;
	if (node != NULL) {
		/* allocates a buffer big enough */
		maxlength = search->root->container.maxlength;
		length = strlen(filename);
		if (length > PATH_MAX)
			goto inval;

		/* initialise the end of the buffer */
		buffer = alloca(length + maxlength + sizeof locales + 1);
		buffer[maxlength + sizeof locales - 1] = '/';
		memcpy(buffer + sizeof locales + maxlength, filename, length + 1);

		/* iterate the searched folder */
		while (node != NULL) {
			folder = node->folder;
			p = buffer + maxlength - folder->length;
			memcpy(p, locales, sizeof locales - 1);
			memcpy(p + sizeof locales - 1, folder->name, folder->length);
			fd = openat(rootfd, p, mode);
			if (fd >= 0)
				return fd;
			node = node->next;
		}
	}

	/* default search */
	return openat(rootfd, filename, mode);

inval:
	errno = EINVAL;
	return -1;
}

/*
 * Resolves 'filename' after search.
 *
 * returns a copy of the filename after search or NULL if not found.
 * the returned string MUST be freed by the caller (using free).
 */
char *locale_search_resolve(struct locale_search *search, const char *filename)
{
	size_t maxlength, length;
	char *buffer, *p;
	struct locale_search_node *node;
	struct locale_folder *folder;
	int rootfd;

	/* check the path and normalize it */
	filename = normalsubpath(filename);
	if (filename == NULL)
		goto inval;

	/* search for folders */
	rootfd = search->root->rootfd;
	node = search->head;
	if (node != NULL) {
		/* allocates a buffer big enough */
		maxlength = search->root->container.maxlength;
		length = strlen(filename);
		if (length > PATH_MAX)
			goto inval;

		/* initialise the end of the buffer */
		buffer = alloca(length + maxlength + sizeof locales + 1);
		buffer[maxlength + sizeof locales - 1] = '/';
		memcpy(buffer + sizeof locales + maxlength, filename, length + 1);

		/* iterate the searched folder */
		while (node != NULL) {
			folder = node->folder;
			p = buffer + maxlength - folder->length;
			memcpy(p, locales, sizeof locales - 1);
			memcpy(p + sizeof locales - 1, folder->name, folder->length);
			if (0 == faccessat(rootfd, p, F_OK, 0)) {
				filename = p;
				goto found;
			}
			node = node->next;
		}
	}

	/* default search */
	if (0 != faccessat(rootfd, filename, F_OK, 0)) {
		errno = ENOENT;
		return NULL;
	}

found:
	p = strdup(filename);
	if (p == NULL)
		errno = ENOMEM;
	return p;

inval:
	errno = EINVAL;
	return NULL;
}

#if defined(TEST_locale_root_validsubpath)
#include <stdio.h>
void t(const char *subpath, int validity) {
  printf("%s -> %d = %d, %s\n", subpath, validity, validsubpath(subpath), validsubpath(subpath)==validity ? "ok" : "NOT OK");
}
int main() {
  t("/",0);
  t("..",0);
  t(".",1);
  t("../a",0);
  t("a/..",1);
  t("a/../////..",0);
  t("a/../b/..",1);
  t("a/b/c/..",1);
  t("a/b/c/../..",1);
  t("a/b/c/../../..",1);
  t("a/b/c/../../../.",1);
  t("./..a/././..b/..c/./.././.././../.",1);
  t("./..a/././..b/..c/./.././.././.././..",0);
  t("./..a//.//./..b/..c/./.././/./././///.././.././a/a/a/a/a",1);
  return 0;
}
#endif

#if defined(TEST_locale_root)
int main(int ac,char**av)
{
	struct locale_root *root = locale_root_create(AT_FDCWD, NULL);
	struct locale_search *search = NULL;
	int fd, rc, i;
	char buffer[256];
	char *subpath;
	while (*++av) {
		if (**av == '@') {
			locale_search_unref(search);
			search = NULL;
			locale_root_unref(root);
			root = locale_root_create(AT_FDCWD, *av + 1);
			if (root == NULL)
				fprintf(stderr, "can't create root at %s: %m\n", *av + 1);
			else
				printf("root: %s\n", *av + 1);
		} else {
			if (root == NULL) {
				fprintf(stderr, "no valid root for %s\n", *av);
			} else if (**av == '-' || **av == '+') {
				locale_search_unref(search);
				search = locale_root_search(root, *av + 1, **av == '+');
				if (search == NULL)
					fprintf(stderr, "can't create search for %s: %m\n", *av + 1);
				else
					printf("search: %s\n", *av + 1);
			} else if (search == NULL) {
				fprintf(stderr, "no valid search for %s\n", *av);
			} else {
				fd = locale_search_open(search, *av, O_RDONLY);
				if (fd < 0)
					fprintf(stderr, "can't open file %s: %m\n", *av);
				else {
					subpath = locale_search_resolve(search, *av);
					if (subpath == NULL)
						fprintf(stderr, "can't resolve file %s: %m\n", *av);
					else {
						rc = (int)read(fd, buffer, sizeof buffer - 1);
						if (rc < 0)
							fprintf(stderr, "can't read file %s: %m\n", *av);
						else {
							buffer[rc] = 0;
							*strchrnul(buffer, '\n') = 0;
							printf("%s -> %s [%s]\n", *av, subpath, buffer);
						}
						free(subpath);
					}
					close(fd);
				}
			}
		}
	}
	locale_search_unref(search); search = NULL;
	locale_root_unref(root); root = NULL;
}
#endif

