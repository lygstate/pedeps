/*****************************************************************************
Copyright (C)  2019  Brecht Sanders  All Rights Reserved
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*****************************************************************************/

#define _CRT_INTERNAL_NONSTDC_NAMES 1

#include "pestructs.h"
#include "pedeps.h"
#include "pedeps_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <io.h>
#else
#include <utime.h>
#include <unistd.h>
#endif

#include <set>
#include <string>

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
  #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
  #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifndef S_IRUSR
#define S_IRUSR 0
#endif
#ifndef S_IWUSR
#define S_IWUSR 0
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif

#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp stricmp
#define strncasecmp strnicmp
typedef SSIZE_T ssize_t;
#endif
#define PATHCMP strcasecmp
#define PATHNCMP strncasecmp
#define PATHSEPARATOR '\\'
#define ISPATHSEPARATOR(c) ((c) == '\\' || (c) == '/')
#define PATHLISTSEPARATOR ';'
#define PATH_MAX 65536
#else
#ifndef PATH_MAX
#include <linux/limits.h>
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define PATHCMP strcmp
#define PATHNCMP strncmp
#define PATHSEPARATOR '/'
#define ISPATHSEPARATOR(c) ((c) == '/')
#define PATHLISTSEPARATOR ':'
#endif

#define APPLICATION_NAME "copypedeps"

int file_exists (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode))
    return 1;
  return 0;
}

int folder_exists (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    return 1;
  return 0;
}

const char* get_filename_from_path (const char* path)
{
  size_t i;
  if (!path || !*path)
    return NULL;
  i = strlen(path);
  while (i-- > 0) {
    if (ISPATHSEPARATOR(path[i]))
      return (path[i + 1] ? path + i + 1 : NULL);
  }
  return path;
}

uint64_t get_block_size (const char* path)
{
#ifdef _WIN32
  char* rootpath;
  DWORD sectors_per_cluster;
  DWORD bytes_per_sector;
  DWORD number_of_free_clusters;
  DWORD total_number_of_clusters;
  if ((rootpath = strdup(path)) == NULL)
    return 0;
  if (!PathStripToRootA(rootpath)) {
    free(rootpath);
    return 0;
  }
  if (!GetDiskFreeSpaceA(rootpath, &sectors_per_cluster, &bytes_per_sector, &number_of_free_clusters, &total_number_of_clusters))
    return 0;
  free(rootpath);
  return sectors_per_cluster * bytes_per_sector;
#else
  struct stat statbuf;
  if (stat(path, &statbuf) != 0)
    return 0; /////TO DO: strip filename and try with directory in case of error
  return statbuf.st_blksize;
#endif
}

int copy_file (const char* srcfile, const char* dstfile, int overwrite)
{
  void* buf;
  uint64_t buflen;
  uint64_t blocksize;
  int srchandle;
  int dsthandle;
  ssize_t n;
  int result = 0;
  //open source file
  if ((srchandle = open(srcfile, O_RDONLY | O_BINARY)) == -1) {
    return 1;
  }
  //open destination file
  if ((dsthandle = open(dstfile, O_WRONLY | O_BINARY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) == -1) {
    close(srchandle);
    return 2;
  }
  //determine buffer size based on largest block size
  buflen = 4096;
  if ((blocksize = get_block_size(srcfile)) > buflen)
    buflen = blocksize;
  if ((blocksize = get_block_size(dstfile)) > buflen)
    buflen = blocksize;
  //allocate buffer
  if ((buf = malloc(buflen)) == NULL) {
    result = -1;
  } else {
    //copy data
    while ((n = read(srchandle, buf, buflen)) > 0) {
      if (write(dsthandle, buf, n) < n) {
        result = 3;
        break;
      }
    }
  }
  //close files
  close(srchandle);
  close(dsthandle);
  //delete destination file on error
  if (result != 0)
    unlink(dstfile);
  //deallocate buffer
  free(buf);
  //copy create/access/write timestamps
  if (result == 0) {
#if _WIN32
    HANDLE handle;
    FILETIME creationtime;
    FILETIME accesstime;
    FILETIME writetime;
    if ((handle = CreateFileA(srcfile, GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE) {
      if (GetFileTime(handle, &creationtime, &accesstime, &writetime)) {
        CloseHandle(handle);
        if ((handle = CreateFileA(dstfile, GENERIC_READ | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE) {
          //SYSTEMTIME st;
          //GetSystemTime(&st);
          //SystemTimeToFileTime(&st, &writetime);
          SetFileTime(handle, &creationtime, &accesstime, &writetime);
          CloseHandle(handle);
        }
      }
    }
#else
    struct stat srctimes;
    struct utimbuf dsttimes;
    //get source file date and time values
    if (stat(srcfile, &srctimes) == 0) {
      //set destination file date and time values
      dsttimes.actime = srctimes.st_atime;
      dsttimes.modtime = srctimes.st_mtime;//or use time(NULL)
      utime(dstfile, &dsttimes);
    }
#endif
  }
  return result;
}

struct string_list_struct {
  char* data;
  struct string_list_struct* next;
};

void string_list_free (struct string_list_struct** list)
{
  struct string_list_struct* next;
  struct string_list_struct* p = *list;
  while (p) {
    next = p->next;
    free(p->data);
    free(p);
    p = next;
  }
  *list = NULL;
}

struct string_list_struct* string_list_append_allocated (struct string_list_struct** list, char* data)
{
  struct string_list_struct** p = list;
  while (*p) {
    p = &((*p)->next);
  }
  if ((*p = (struct string_list_struct*)malloc(sizeof(struct string_list_struct))) != NULL) {
    (*p)->data = data;
    (*p)->next = NULL;
  }
  return *p;
}

struct string_list_struct* string_list_append (struct string_list_struct** list, const char* data)
{
  return string_list_append_allocated(list, (data ? strdup(data) : NULL));
}

struct dependancy_info_struct {
  int recursive;
  int overwrite;
  int dryrun;
  std::set<std::string> *filelist;
  const char* windir;
  char* preferredpath;
  struct string_list_struct* pathlist;
};

char* get_base_path (const char* path)
{
  char* result = NULL;
  if (path) {
    size_t pos = strlen(path);
    while (pos > 0) {
      pos--;
      if (ISPATHSEPARATOR(path[pos]))
        break;
    }
    if ((result = (char*)malloc(pos + 1)) != NULL) {
      memcpy(result, path, pos);
      result[pos] = 0;
    }
  }
  return result;
}

typedef int (*iterate_path_list_callback_fn)(const char* path, void* callbackdata);

size_t iterate_path_list (const char* pathlist, char pathseparator, iterate_path_list_callback_fn callbackfunction, void* callbackdata)
{
  char* path;
  int status;
  size_t count = 0;
  size_t startpos = 0;
  size_t endpos = 0;
  if (pathseparator == 0)
    pathseparator = PATHLISTSEPARATOR;
  while (pathlist[startpos]) {
    endpos = startpos;
    while (pathlist[endpos] && pathlist[endpos] != pathseparator) {
      endpos++;
    }
    if (endpos > startpos && (path = (char*)malloc(endpos - startpos + 1)) != NULL) {
      memcpy(path, pathlist + startpos, endpos - startpos);
      path[endpos - startpos] = 0;
      status = (*callbackfunction)(path, callbackdata);
      free(path);
      if (status != 0)
        break;
    }
    startpos = endpos;
    while (pathlist[startpos] && pathlist[startpos] == pathseparator)
      startpos++;
  }
  return count;
}

//returns non-zero path1 is under path2
int is_in_path (const char* path1, const char* path2)
{
  char full_path1[PATH_MAX];
  char full_path2[PATH_MAX];
  if (realpath(path1, full_path1) && realpath(path2, full_path2)) {
    size_t len1 = strlen(full_path1);
    size_t len2 = strlen(full_path2);
    if (len1 >= len2) {
      if (PATHNCMP(full_path1, full_path2, len2) == 0) {
        if (full_path2[len2] == 0 || ISPATHSEPARATOR(full_path2[len2]))
          return 1;
      }
    }
  }
  return 0;
}

int iterate_path_add (const char* path, void* callbackdata)
{
  string_list_append((struct string_list_struct**)callbackdata, path);
  return 0;
}

char* search_path (const char* preferredpath, struct string_list_struct* pathlist, const char* filename)
{
  size_t filenamelen;
  struct string_list_struct* p;
  char* s;
  size_t l;
  if (!filename || !*filename)
    return NULL;
  if (file_exists(filename))
    return strdup(filename);
  filenamelen = strlen(filename);
  //check preferred path
  if ((s = (char*)malloc((l = strlen(preferredpath)) + filenamelen + 2)) != NULL) {
    memcpy(s, preferredpath, l);
    s[l] = PATHSEPARATOR;
    memcpy(s + l + 1, filename, filenamelen + 1);
    if (file_exists(s))
      return s;
    free(s);
  }
  //check search path
  p = pathlist;
  while (p) {
    if ((s = (char*)malloc((l = strlen(p->data)) + filenamelen + 2)) != NULL) {
      memcpy(s, p->data, l);
      s[l] = PATHSEPARATOR;
      memcpy(s + l + 1, filename, filenamelen + 1);
      if (file_exists(s))
        return s;
      free(s);
    }
    p = p->next;
  }
  return NULL;
}

int add_dependancies (struct dependancy_info_struct* depinfo, const char* filename);

int iterate_dependancies_add (const char* modulename, const char* functionname, void* callbackdata)
{
  struct dependancy_info_struct* depinfo = (struct dependancy_info_struct*)callbackdata;
  if (modulename) {
    char* path;
    if ((path = search_path(depinfo->preferredpath, depinfo->pathlist, modulename)) != NULL) {
      //new module, recursively add dependancies if wanted
      if (depinfo->filelist->insert(path).second) {
        if (depinfo->recursive) {
          if (!is_in_path(path, depinfo->windir)) {
            add_dependancies(depinfo, path);
          }
        }
      } else {
        //module already listed, no further action needed
        free(path);
      }
    }
  }
  return 0;
}

int add_dependancies (struct dependancy_info_struct* depinfo, const char* filename)
{
  pefile_handle pehandle;
  char* path;
  //determine path
  if ((path = search_path(depinfo->preferredpath, depinfo->pathlist, filename)) == NULL) {
    fprintf(stderr, "Error: unable to locate %s in PATH\n", filename);
    return 1;
  }
  //create PE object
  if ((pehandle = pefile_create()) == NULL) {
    fprintf(stderr, "Error creating PE handle\n");
    return 2;
  }
  //open PE file
  if (pefile_open_file(pehandle, path) == 0) {
    //check all dependancies
    pefile_list_imports(pehandle, iterate_dependancies_add, depinfo);
    //close PE file
    pefile_close(pehandle);
  }
  //destroy PE object
  pefile_destroy(pehandle);
  //clean up
  free(path);
  return 0;
}

void show_help ()
{
  printf(
    "Usage: " APPLICATION_NAME " [-h|-?] [-r] srcfile [...] dstfolder\n"
    "Parameters:\n"
    "  -h -?       \tdisplay command line help\n"
    "  -r          \trecursively copy dependancies\n"
    "  -n          \tdon't overwrite existing files\n"
    "  -d          \tdry run: don't actually copy, just display copy actions\n"
    "Description:\n"
    "Copies .exe and .dll files and all their dependancies to the destination folder.\n"
    "Version: " PEDEPS_VERSION_STRING "\n"
    ""
  );
}

int main (int argc, char* argv[])
{
  int i;
  char* dst;
  size_t dstlen;
  struct dependancy_info_struct depinfo;
  //show help page if no parameters were given or help was requested
  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && (argv[i][1] == 'h' || argv[i][1] == '?') && argv[i][2] == 0)
      break;
  }
  if (argc <= 2 || i < argc) {
    show_help();
    return 0;
  }
  //check if last parameter is an existing directory
  if ((dstlen = strlen(argv[argc - 1])) == 0) {
    fprintf(stderr, "Empty destination folder name not allowed\n");
    return 1;
  }
  if ((dst = (char*)malloc(dstlen + 2)) == NULL) {
    fprintf(stderr, "Memory allocation error\n");
    return 1;
  }
  memcpy(dst, argv[argc - 1], dstlen + 1);
  if (!ISPATHSEPARATOR(dst[dstlen - 1])) {
    dst[dstlen++] = PATHSEPARATOR;
    dst[dstlen] = 0;
  }
  if (!folder_exists(argv[argc - 1])) {
    fprintf(stderr, "Destination folder not found: %s\n", dst);
    return 2;
  }
  //initialize
  depinfo.recursive = 0;
  depinfo.overwrite = 1;
  depinfo.dryrun = 0;
  depinfo.filelist = new std::set<std::string>();
  depinfo.preferredpath = NULL;
  depinfo.pathlist = NULL;
  //determine search path
  std::string windir = getenv("windir");
  depinfo.windir = windir.c_str();
  std::string system32 = windir + "/System32";
  std::string system32_downlevel = system32 + "/downlevel";
  iterate_path_add(system32.c_str(), &depinfo.pathlist);
  iterate_path_add(system32_downlevel.c_str(), &depinfo.pathlist);
  iterate_path_list(getenv("PATH"), 0, iterate_path_add, &depinfo.pathlist);
  //process all parameters and get dependancies of the requested files
  std::set<std::string> filelist_argv;
  for (i = 1; i < argc - 1; i++) {
    if (argv[i][0] == '-' && argv[i][1] == 'r' && argv[i][2] == 0) {
      depinfo.recursive = 1;
    } else if (argv[i][0] == '-' && argv[i][1] == 'n' && argv[i][2] == 0) {
      depinfo.overwrite = 0;
    } else if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == 0) {
      depinfo.dryrun = 1;
    } else {
      if (!file_exists(argv[i])) {
        fprintf(stderr, "File not found: %s\n", argv[i]);
      } else {
        //add current file
        depinfo.filelist->insert(argv[i]);
        filelist_argv.insert(argv[i]);
        //determine preferred path (same folder as current file)
        depinfo.preferredpath = get_base_path(argv[i]);
        //add dependancies
        add_dependancies(&depinfo, argv[i]);
        //clean up preferred path
        free(depinfo.preferredpath);
        depinfo.preferredpath = NULL;
      }
    }
  }
  for (auto it = filelist_argv.begin(); it != filelist_argv.end(); ++it) {
    depinfo.filelist->erase(*it);
  }
  //free search path
  string_list_free(&depinfo.pathlist);
  //copy dependancies
  unsigned int entryindex = 0;
  for (auto it = depinfo.filelist->begin(); it != depinfo.filelist->end(); ++it) {
    const char* full_path = it->c_str();
    const char* filename;
    char* dstpath;
    if (is_in_path(full_path, depinfo.windir)) {
      continue;
    }
    if ((filename = get_filename_from_path(full_path)) != NULL) {
      if (StrStrIA(filename, "api-ms-win") == filename) {
        continue;
      }
      if ((dstpath = (char*)malloc(dstlen + strlen(filename) + 1)) != NULL) {
        memcpy(dstpath, dst, dstlen);
        strcpy(dstpath + dstlen, filename);
        if (!depinfo.overwrite && file_exists(dstpath)) {
          fprintf(stderr, "Not overwriting existing file: %s\n", dstpath);
        } else {
          if (depinfo.dryrun) {
            printf("%s -> %s\n", full_path, dstpath);
          } else {
            if (copy_file(full_path, dstpath, depinfo.overwrite) != 0)
              fprintf(stderr, "Error copying %s to %s\n", full_path, dstpath);
          }
        }
        free(dstpath);
      }
    }
    entryindex++;
  }
  //clean up
  delete depinfo.filelist;
  free(dst);
  return 0;
}
