#include "archive.h"
#include "archive_ssmc.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_HANDLERS 50
#define MAX_ARCHIVE_DISPLAY_ITEMS 255
static ArchiveVTable* archive_handlers[MAX_HANDLERS];
static int handler_count = 0;

void register_archive_handler(ArchiveVTable* handler) {
    if (!handler) {
        // TODO: Log error, attempt to register a NULL handler
        return;
    }

    if (handler_count >= MAX_HANDLERS) {
        // TODO: Log error, handler limit reached.
        return;
    }

    for (int i = 0; i < handler_count; i++) {
        if (archive_handlers[i] == handler) {
            return; // This handler is already in the list.
        }
    }

    archive_handlers[handler_count++] = handler;
    // TODO: Log msg, successfully registered {name of handler} handler
}

//NOTE: The get_handler_for_file function will choose the first
// supported handler. May need to be reworked should user defined preferences
// for specific formats support be added.

ArchiveVTable* get_handler_for_file(const char *filename) {
    if(!filename) {
        return NULL;
    }

    for (int i = 0; i < handler_count; i++) {
        if (archive_handlers[i]->is_supported(filename)) {
            return archive_handlers[i];
        }
    }

    return NULL;
}

void register_all_archive_handlers(void) {
    register_archive_handler(get_ssmc_archive_handler());
    //register_archive_handler(get_zip_archive_handler()); //Future example
}

bool archive_helper_is_ext_supported(const char *filename, const char **extensions) {
    const char *file_ext = strrchr(filename, '.');
    if (!file_ext || file_ext == filename) {
        return false; //No extension found
    }

    for (int i = 0; extensions[i] != NULL; i++) {
        if (strcasecmp(file_ext, extensions[i]) == 0) {
            return true;
        }
    }

    return false;
}

char** archive_list_contents(const char *archive_path, int *count) {
    ArchiveVTable* handler = get_handler_for_file(archive_path);
    ArchiveEntry *entries = NULL;
    char** working_list = NULL;
    char** final_list = NULL;
    int valid_item_count = 0;
    int handler_raw_count = 0;

    if(!handler || !handler->list_contents){
        //Todo: Log warning, extension not supported or list_contents not
        // implemented.
        return NULL;
    }

    entries = handler->list_contents(archive_path, &handler_raw_count);
    if(!entries){
        //Todo: Log error, handler failed to list contents.
        return NULL;
    }

    working_list = malloc(MAX_ARCHIVE_DISPLAY_ITEMS * sizeof(char*));
    if(!working_list){
        //Todo: Log error: Memory allocation for final_list failed.
        goto cleanup;
    }
    memset(working_list, 0, MAX_ARCHIVE_DISPLAY_ITEMS * sizeof(char*));

    for (int i = 0; i < handler_raw_count; i++){
        if (valid_item_count >= MAX_ARCHIVE_DISPLAY_ITEMS){
            //Todo: Log warning, archive contains more than the supported 255
            // items. List contains first 255 valid items.
            break;
        }

        bool is_file = (entries[i].type == ARCHIVE_ENTRY_FILE);
        bool is_root = (strchr(entries[i].path, '/') == NULL);

        if (is_file && is_root){
            working_list[valid_item_count] = strdup(entries[i].path);
            if(!working_list[valid_item_count]){
                //Todo: Log error, strdup failed for item path/filename
                goto cleanup;
            }
            valid_item_count++;
        }
    }
    *count = valid_item_count;

    if (valid_item_count > 0) {
        char** shrunk_list = realloc(
            working_list,
            valid_item_count * sizeof(char*)
        );
        if(!shrunk_list){
            //Todo: Log error, shrunk list reallocation failed.
            goto cleanup;
        }
        final_list = shrunk_list;
        working_list = NULL;
    }

    cleanup:
        if(entries){
            for (int i = 0; i < handler_raw_count; i++){
                if(entries[i].path){
                    free(entries[i].path);
                }
            }
            free(entries);
        }
        if(working_list){
            for(int i = 0; i < valid_item_count; i++){
                if(working_list[i]){
                    free(working_list[i]);
                }
            }
            free(working_list);
        }

        return final_list;
}

char* archive_extract_file(
    const char *archive_path,
    const char *file_inside_archive,
    int file_index,
    const char *temp_dir
) {
    ArchiveVTable* handler = get_handler_for_file(archive_path);
    if (handler && handler->extract_file) {
        return handler->extract_file(archive_path, file_inside_archive, file_index, temp_dir);
    }
    return NULL;
}

void archive_system_shutdown(void) {
    for (int i = 0; i < handler_count; i++) {
        archive_handlers[i] = NULL;
    }
    handler_count = 0;
}

SupportedExtensionInfo* archive_get_all_supported_info(int *count) {
    if (!count) {
        //Todo: Log error, count pointer is NULL.
        return NULL;
    }

    *count = 0;
    if (handler_count == 0) {
        //Todo: Log error, handler count is 0, registration of handlers may not
        //  have occured.
        return NULL;
    }

    int capacity = 8; //Place holder value
    SupportedExtensionInfo* info_list = malloc(capacity * sizeof(SupportedExtensionInfo));
    if (!info_list) {
        return NULL;
    }

    for (int i = 0; i < handler_count; i++) {
        ArchiveVTable* handler = archive_handlers[i];
        if (!handler ||
            !handler->get_handler_name ||
            !handler->get_supported_extensions) {
                continue;
        }

        const char* handler_name = handler->get_handler_name();
        const char** exts = handler->get_supported_extensions();

        for (int j = 0; exts[j] != NULL; j++) {
            if (*count >= capacity) {
                capacity *= 2;
                SupportedExtensionInfo* new_list = realloc(
                    info_list,
                    capacity * sizeof(SupportedExtensionInfo)
                );
                if (!new_list) {
                    // Abort and clean up on realloc failure
                    for (int k = 0; k < *count; k++) {
                        free(info_list[k].extension);
                        free(info_list[k].handler_name);
                    }
                    free(info_list);
                    *count = 0;
                    return NULL;
                }
                info_list = new_list;
            }

            info_list[*count].extension = strdup(exts[j]);
            info_list[*count].handler_name = strdup(handler_name);

            // Make sure strdup succeeded before incrementing count
            if (!info_list[*count].extension || !info_list[*count].handler_name) {
                // Abort and clean up on strdup failure
                free(info_list[*count].extension);
                free(info_list[*count].handler_name);
                for (int k = 0; k < *count; k++) {
                    free(info_list[k].extension);
                    free(info_list[k].handler_name);
                }
                free(info_list);
                *count = 0;
                return NULL;
            }
            (*count)++;
        }
    }

    if (*count == 0) {
        //Todo: Log warning, no extensions found.
        free(info_list);
        return NULL;
    }

    SupportedExtensionInfo* final_list = realloc(
        info_list,
        *count * sizeof(SupportedExtensionInfo)
    );

    if (!final_list) {
        for (int k = 0; k < *count; k++) {
            free(info_list[k].extension);
            free(info_list[k].handler_name);
        }
        free(info_list);
        *count = 0;
        return NULL;
    }

    return final_list;
}
