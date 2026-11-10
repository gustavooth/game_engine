#pragma once

#include "defines.h"

typedef struct FileHandle
{
  void *handle; // internal handle
  const char *path;
} FileHandle;

typedef enum PathObjectType {
  PATH_OBJECT_TYPE_FILE,
  PATH_OBJECT_TYPE_DIR
} PathObjectType;

typedef struct PathObject
{
  char name[256];
  PathObjectType type;
} PathObject;

/**
 * @brief Check if path exist.
 *
 * @param path File or Directory path
 * @return Return true if path exist.
 */
EGNAPI b8 filemanager_exist(const char *path);

EGNAPI b8 filemanager_check(const char *path, PathObjectType type);

/**
 * Resolves a file path for reading. Relative paths are first tried from the
 * current directory and then from the executable directory.
 *
 * @param path Input path.
 * @param out_path Output buffer.
 * @param out_size Output buffer size.
 * @return true if the path exists and is readable.
 */
EGNAPI b8 filemanager_resolve_path(const char *path, char *out_path, u64 out_size);

/**
 * @brief Create directory
 *
 * @param path Path to new directory
 * @param sub_dir Create necessary directories
 * @return Return true if success.
 */
EGNAPI b8 filemanager_create_directory(const char *path, b8 sub_dir);

/**
 * @brief Open the file
 *
 * Also create file if does not exist.
 *
 * @param path Path to file
 * @param create_dir Create necessary directories
 * @param force_new If file exist, clear all data
 * @param out_file Storage internal file handle
 * @return Return true if success
 */
EGNAPI b8 filemanager_open_file(const char *path, b8 create_dir, b8 force_new, FileHandle *out_file);

/**
 * @brief Close file.
 *
 * Usage after open.... :D
 *
 * @param file Pointer to file handle
 */
EGNAPI void filemanager_close_file(FileHandle *file);

/**
 * @brief Get file length
 *
 * @param handle Pointer to file handle
 * @return Return true if success
 */
EGNAPI u64 filemanager_get_file_length(FileHandle *handle);

/**
 * @brief Read bytes from file
 *
 * @param handle Pointer to file handle
 * @param dest Destination to get bytes from file
 * @param start Initial cursor position to read
 * @param end Final cursor position
 * @return Return true if there is not error
 */
EGNAPI u64 filemanager_read(FileHandle *handle, u8 *dest, u64 start, u64 end);

/**
 * @brief Get all bytes from file
 *
 * If success release memory after usage. Use MEMOFREE.
 *
 * @param handle Pointer to file handle.
 * @param out_buffer_ptr Out buffer pointer
 * @param out_length Out buffer length
 * @return Return true if success
 */
EGNAPI b8 filemanager_read_all(const char *path, u8 **out_buffer_ptr, u64 *out_length);

/**
 * @brief Write bytes from file
 *
 * @param handle Pointer to file handle
 * @param buffer Bytes to set
 * @param lenght Buffer length
 * @param start Initial cursor position to write
 * @return Return bytes read
 */
EGNAPI u64 filemanager_write(FileHandle *handle, u8 *buffer, u64 length, u64 start);

EGNAPI b8 filemanager_delete_file(const char* path);

EGNAPI b8 filemanager_delete_dir(const char* path, b8 force);

EGNAPI b8 filemanager_list_objects(const char* path, u32* out_count, PathObject* dest_objects);
