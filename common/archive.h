#pragma once

#include <stdbool.h>

typedef struct SupportedExtensionInfo {
    char* extension;
    char* handler_name;
} SupportedExtensionInfo;

typedef struct ArchiveVTable {
    //Returns true if the handler supports this file extension
    bool (*is_supported)(const char *filename);

    /*List the contents of an archive.
    Return dynamically allocated array of strings (file names).
    The caller is responsible for freeing the array and the contents.*/
    char** (*list_contents)(const char *archive_path, int *count);

    /*Extract a single file from the archive to a temporary directory.
    Return the full path to the extracted file on success, NULL on failure.
    The caller is responsible for freeing the returned path string.

    file_inside_archive can be null if using index

    If using file_name to extract, set file_index to -1*/
    char* (*extract_file)(
        const char *archive_path,
        const char *file_inside_archive,
        int file_index,
        const char *temp_dir
    );

    const char* (*get_handler_name)(void);
    const char** (*get_supported_extensions)(void);
} ArchiveVTable;

/**
 * @brief Registers an archive handler with the system.
 *
 * @details This function adds a new `ArchiveVTable` to the list of available
 * archive handlers. It performs several checks to ensure robustness:
 * - Prevents registration of `NULL` handlers, which would lead to crashes.
 * - Ensures the maximum number of handlers (`MAX_HANDLERS`) is not exceeded.
 * - Prevents the same handler from being registered multiple times, avoiding redundancy.
 *
 * Handlers that fail these checks are silently ignored.
 *
 * @param handler A pointer to the `ArchiveVTable` to be registered.
 */
void register_archive_handler(ArchiveVTable* handler);

/** @brief Finds the first registered handler that supports a given file.
 *
 * @details This function iterates through the list of registered archive handlers
 * and calls the `is_supported` method for each one. It returns the first
 * handler that returns `true`.
 *
 * @param filename The path to the archive file to check. Must be a valid,
 *                 non-NULL string.
 * @return A pointer to the appropriate `ArchiveVTable` if a supported
 *         handler is found, otherwise `NULL`. Returns `NULL` if `filename`
 *         is `NULL`.
 */
ArchiveVTable* get_handler_for_file(const char *filename);

/**
 * @brief Registers all known archive handlers with the system.
 *
 * @details This function serves as a central point to initialize and register
 * all available archive format handlers. It calls `register_archive_handler`
 * for each specific archive type (e.g., SSMC, ZIP).
 *
 * The order of registration can be important if multiple handlers could
 * potentially support the same file extension, as `get_handler_for_file`
 * returns the first matching handler.
 */
void register_all_archive_handlers(void);

/**
 * @brief Checks if a filename's extension is in a list of supported extensions.
 *
 * @details This is a helper function that performs a case-insensitive check
 * of a file's extension against a NULL-terminated list of extensions.
 * It correctly handles files with no extension or files that start with a dot.
 *
 * @param filename The filename to check.
 * @param extensions A NULL-terminated array of strings, where each string is a
 *                   supported extension (e.g., ".zip").
 * @return `true` if the file's extension is supported, `false` otherwise.
 */
bool archive_helper_is_ext_supported(
    const char *filename,
    const char **extensions
);

/**
 * @brief Lists the contents of a given archive file.
 * @param archive_path The path to the archive.
 * @param count A pointer to an integer to store the number of files.
 * @return An array of strings on success, NULL if the format is unsupported or
 *         an error occurs.
 */
char** archive_list_contents(const char *archive_path, int *count);

/**
 * @brief Extracts a file from an archive.
 * @param archive_path The path to the archive.
 * @param file_inside_archive The name of the file to extract (if index is not used).
 * @param file_index The index of the file to extract.
 * @param temp_dir The directory to extract the file into.
 * @return The full path to the extracted file on success, NULL on failure.
 */
char* archive_extract_file(
    const char *archive_path,
    const char *file_inside_archive,
    int file_index,
    const char *temp_dir
);

/**
 * @brief Resets the archive handler system.
 * @details This function clears the list of registered handlers. It should be
 *          called on application shutdown if cleanup is required.
 */
void archive_system_shutdown(void);

/**
 * @brief Gets a list of all supported extensions and their corresponding
 *        handlers.
 *
 * @param count A pointer to an integer where the total number of entries
 *              will be stored.
 * @return A dynamically allocated array of SupportedExtensionInfo structs.
 *         The caller is responsible for freeing this array and also for
 *         freeing the `extension` and `handler_name` string within each
 *         struct.
 *         Returns NULL on failure or if no handlers are registered.
 */
SupportedExtensionInfo* archive_get_all_supported_info(int *count);
