#include "platform/filemanager.h"
#include "core/egnstring.h"
#include "core/logger.h"
#include "core/memo.h"
#include "memory/linear_allocator.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

/**
 * Resolves a path used for reading.
 *
 * Absolute paths are used as-is. Relative paths are first looked up from the
 * current working directory. If they do not exist there, they are resolved
 * against the directory that contains the executable. This lets the game read
 * `assets/...` and `shaders/...` regardless of where it was launched from.
 */
b8 filemanager_resolve_path(const char *path, char *out_path, u64 out_size)
{
  if (!path || !out_path || out_size == 0)
    return false;

  snprintf(out_path, out_size, "%s", path);

  FILE *file = fopen(out_path, "rb");
  if (file)
  {
    fclose(file);
    return true;
  }

  if (path[0] == '/')
    return false;

  char executable_path[1024];
  ssize_t length = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
  if (length <= 0)
    return false;

  executable_path[length] = '\0';

  char *last_slash = strrchr(executable_path, '/');
  if (!last_slash)
    return false;

  *last_slash = '\0';
  snprintf(out_path, out_size, "%s/%s", executable_path, path);

  file = fopen(out_path, "rb");
  if (!file)
    return false;

  fclose(file);
  return true;
}

b8 filemanager_exist(const char *path)
{
  struct stat st;
  if ((stat(path, &st) == 0))
    return true;

  return false;
}

EGNAPI b8 filemanager_check(const char *path, PathObjectType type)
{
  struct stat st;
  if (stat(path, &st) == 0)
  {
    if (type == PATH_OBJECT_TYPE_DIR)
    {
      if (S_ISDIR(st.st_mode))
        return true;
    }
    else if (type == PATH_OBJECT_TYPE_FILE)
    {
      if (S_ISREG(st.st_mode))
        return true;
    }
  }

  return false;
}

b8 filemanager_create_directory(const char *path, b8 sub_dir)
{
  if (!path || path[0] == '\0')
    return false;

  // The original test was inverted (it reported success when mkdir failed).
  if (mkdir(path, S_IRWXU) == 0 || filemanager_check(path, PATH_OBJECT_TYPE_DIR))
    return true;
  if (!sub_dir)
    return false;

  // Create every missing parent, then the directory itself.
  char tmp[1024];
  u64 length = string_length(path);
  if (length >= sizeof(tmp))
    return false;
  MEMOCPY(tmp, path, length + 1);

  for (u64 i = 1; i < length; i++)
  {
    if (tmp[i] != '/')
      continue;
    tmp[i] = '\0';
    if (mkdir(tmp, S_IRWXU) != 0 && !filemanager_check(tmp, PATH_OBJECT_TYPE_DIR))
      return false;
    tmp[i] = '/';
  }

  return mkdir(path, S_IRWXU) == 0 || filemanager_check(path, PATH_OBJECT_TYPE_DIR);
}

b8 filemanager_open_file(const char *path, b8 create_dir, b8 force_new, FileHandle *out_file)
{
  if (!path || !out_file)
    return false;

  if (create_dir)
  {
    // Create the directory that will contain the file. The path itself used
    // to be created as a directory, which then made opening the file fail.
    char parent[1024];
    u64 length = string_length(path);
    if (length < sizeof(parent))
    {
      MEMOCPY(parent, path, length + 1);
      char *last_slash = strrchr(parent, '/');
      if (last_slash && last_slash != parent)
      {
        *last_slash = '\0';
        filemanager_create_directory(parent, true);
      }
    }
  }

  FILE *file = 0;
  if (filemanager_exist(path) && !force_new)
    file = fopen(path, "r+b");
  else
    file = fopen(path, "w+b");

  if (!file)
    return false;

  out_file->handle = file;
  out_file->path = path;

  return true;
}

void filemanager_close_file(FileHandle *file)
{
  fclose(file->handle);
}

u64 filemanager_get_file_length(FileHandle *handle)
{
  fseek(handle->handle, 0, SEEK_END);
  return (u64)ftell(handle->handle);
}

u64 filemanager_read(FileHandle *handle, u8 *dest, u64 start, u64 end)
{
  if (!handle || !handle->handle || !dest || end <= start)
    return 0;
  if (fseek(handle->handle, (long)start, SEEK_SET) != 0)
    return 0;
  // [start, end) is a byte range; the count used to be `end`, which read past
  // the caller's buffer whenever start was not zero.
  return fread(dest, 1, end - start, handle->handle);
}

b8 filemanager_read_all(const char *path, u8 **out_buffer_ptr, u64 *out_length)
{
  if (!out_buffer_ptr || !out_length)
    return false;

  char resolved_path[2048];
  if (!filemanager_resolve_path(path, resolved_path, sizeof(resolved_path)))
    return false;

  FILE *file = fopen(resolved_path, "rb");
  if (file == 0)
    return false;

  long size = -1;
  if (fseek(file, 0, SEEK_END) == 0)
    size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
  {
    fclose(file);
    return false;
  }

  u64 len = (u64)size;
  // Never hand out a null buffer for an empty file.
  u8 *buffer = MEMOALLOC(len > 0 ? len : 1, MEMO_FILE);
  if (!buffer)
  {
    fclose(file);
    return false;
  }

  u64 bytes_read = len > 0 ? fread(buffer, 1, len, file) : 0;
  fclose(file);
  if (bytes_read != len)
  {
    MEMOFREE(buffer);
    return false;
  }

  *out_buffer_ptr = buffer;
  *out_length = len;
  return true;
}

u64 filemanager_write(FileHandle *handle, u8 *buffer, u64 lenght, u64 start)
{
  fseek(handle->handle, start, SEEK_SET);
  return fwrite(buffer, 1, lenght, handle->handle);
}

b8 filemanager_delete_file(const char *path)
{
  return remove(path) == 0;
}

b8 force_delete_dir(const char* path, LinearAllocator* lia) {
  u32 count = 0;
  filemanager_list_objects(path, &count, 0);

  u32 objects_mem_len = sizeof(PathObject) * count;
  u32 path_mem_len = 256;

  u32 required_memory = objects_mem_len + path_mem_len;
  u32 remaining = lia->total_size - lia->allocated;

  if (remaining < required_memory){
    EGNERROR("force_delete_dir: insufficient memory!");
    return false;
  }

  PathObject* objects = LIALLOC(lia, objects_mem_len);
  char* object_path = LIALLOC(lia, path_mem_len);

  filemanager_list_objects(path, 0, objects);

  for (u32 i = 0; i < count; i++)
  {
    string_format2(object_path, path_mem_len, 0, "%s/%s", path, objects[i].name);

    if (objects[i].type == PATH_OBJECT_TYPE_FILE) {
      if (!filemanager_delete_file(object_path))
        return false;
    }else {
      if (!force_delete_dir(object_path, lia))
        return false;
    }
  }
  
  return rmdir(path) == 0;
}

b8 filemanager_delete_dir(const char *path, b8 force)
{
  if (rmdir(path) == 0)
    return true;
  else if (!force)
    return false;
  
  LinearAllocator lia;
  linear_allocator_create(102400, &lia);

  if(force_delete_dir(path, &lia)){
    linear_allocator_destroy(&lia);
    return true;
  }

  linear_allocator_destroy(&lia);
  return false;
}

b8 filemanager_list_objects(const char *path, u32 *out_count, PathObject *dest_objects)
{
  DIR *dir = opendir(path);
  if (dir == 0)
    return false;

  b8 result = false;

  struct dirent *entry;
  u32 count = 0;
  char object_path[256];

  if (dest_objects == 0)
  {
    while ((entry = readdir(dir)) != 0)
    {
      if (string_equals(entry->d_name, ".") || string_equals(entry->d_name, ".."))
        continue;

      struct stat st;
      string_format2(object_path, 256, 0, "%s/%s", path, entry->d_name);

      if (stat(object_path, &st) == 0)
      {
        if (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))
          count++;
      }
      else
        break;
    }

    *out_count = count;
    result = true;
  }
  else
  {
    while ((entry = readdir(dir)) != 0)
    {
      if (string_equals(entry->d_name, ".") || string_equals(entry->d_name, ".."))
        continue;

      struct stat st;
      PathObjectType type;
      string_format2(object_path, 256, 0, "%s/%s", path, entry->d_name);

      if (stat(object_path, &st) == 0)
      {
        if (S_ISREG(st.st_mode))
          type = PATH_OBJECT_TYPE_FILE;
        else if (S_ISDIR(st.st_mode))
          type = PATH_OBJECT_TYPE_DIR;
        else
          continue;
      }
      else
        break;

      MEMOCPY(dest_objects[count].name, entry->d_name, STRLEN(entry->d_name) + 1);
      dest_objects[count].type = type;
      count++;
      result = true;
    }
  }

  closedir(dir);
  return result;
}
