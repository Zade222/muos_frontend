#include "sprite_shrink/sprite_shrink.h"

#include "archive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* ssmc_extensions[] = { ".ssmc", NULL };

typedef struct ChunkIndexHandleU64 ChunkIndexHandleU64;
typedef struct ChunkIndexHandleU128 ChunkIndexHandleU128;

static bool ssmc_is_supported(const char *filename) {
    return archive_helper_is_ext_supported(filename, ssmc_extensions);
}

ArchiveEntry* ssmc_list_contents(const char *archive_path, int *count){
    *count = 0;
    ArchiveEntry *item_list = NULL;

    FILE *archive_file = NULL;
    uint8_t *manifest_buffer = NULL;
    FFIParsedManifestArrayU64 *parsed_manifest64 = NULL;
    FFIParsedManifestArrayU128 *parsed_manifest128 = NULL;

    void *generic_parsed_manifest = NULL;
    uintptr_t manifest_len = 0;

    archive_file = fopen(archive_path, "rb");
    if (!archive_file) {
        //Todo: Log error, failed to open file.
        goto cleanup;
    }

    FileHeader header;
    if (fread(&header, sizeof(FileHeader), 1, archive_file) != 1) {
        //Todo: Log error, failed to read file.
        goto cleanup;
    }

    if (memcmp(
        header.magic_num,
        MAGIC_NUMBER,
        sizeof(header.magic_num)
    ) != 0) {
        //Todo: Log error, magic number check failed. Likely unsupported
        // archive.
        goto cleanup;
    }

    manifest_buffer = malloc(header.man_length);
    if (!manifest_buffer) {
        //Todo: Log error, memory allocation failure
        goto cleanup;
    }

    if (fseek(archive_file, header.man_offset, SEEK_SET) != 0) {
        //Todo: Log error, file seek failure
        goto cleanup;
    }
    if (fread(
        manifest_buffer,
        1,
        header.man_length,
        archive_file
    ) != header.man_length) {
        //Todo: Log error, failed to read manifest from archive.
        // Likely malformed.
        goto cleanup;
    }

    FFIResult result;

    if (header.hash_type == 1) { //1 = xxhash3_64
        result = parse_file_metadata_u64(
            manifest_buffer,
            header.man_length,
            &parsed_manifest64
        );
        if (result != StatusOk || !parsed_manifest64) {
            goto cleanup;
        }
        generic_parsed_manifest = parsed_manifest64;
        manifest_len = parsed_manifest64->manifests_len;
    } else if (header.hash_type == 2) { //2 = xxhash3_128
        result = parse_file_metadata_u128(
            manifest_buffer,
            header.man_length,
            &parsed_manifest128
        );
        if (result != StatusOk || !parsed_manifest128) {
            goto cleanup;
        }
        generic_parsed_manifest = parsed_manifest128;
        manifest_len = parsed_manifest128->manifests_len;
    } else {
        //Todo: Log error, unsupported hash type
        goto cleanup;
    }

    //Not possible to make empty ssmc archive. Throw error if found.
    if (manifest_len == 0) {
        //Todo: Log error, likely malformed archive
        goto cleanup;
    }

    item_list = malloc(manifest_len * sizeof(ArchiveEntry));
    if (!item_list) {
        goto cleanup;
    }
    memset(item_list, 0, manifest_len * sizeof(ArchiveEntry));

    for (uintptr_t i = 0; i < manifest_len; i++) {
        const char* source_filename;

        if (header.hash_type == 1){
            source_filename = (
                (FFIParsedManifestArrayU64*) generic_parsed_manifest
            )->manifests[i].filename;
        } else {
            source_filename = (
                (FFIParsedManifestArrayU128*) generic_parsed_manifest
            )->manifests[i].filename;
        }

        item_list[i].path = strdup(source_filename);
        if (!item_list[i].path) {
            //Todo: Log error, filename allocation failed
            goto cleanup;
        }

        //In ssmc archives, there are only files.
        item_list[i].type = ARCHIVE_ENTRY_FILE;

        item_list[i].index = i;
    }

    //Todo: Log msg, file list scuessfully retrieved from ssmc archive.
    *count = (int)manifest_len;

    cleanup:
        if (*count == 0 && item_list) {
            for (uintptr_t i = 0; i < manifest_len; i++) {
                if (item_list[i].path) {
                    free(item_list[i].path);
                }
            }
            free(item_list);
            item_list = NULL;
        }

        if (archive_file) {
            fclose(archive_file);
        }
        if (manifest_buffer) {
            free(manifest_buffer);
        }
        if (parsed_manifest64) {
            free_parsed_manifest_u64(parsed_manifest64);
        }
        if (parsed_manifest128) {
            free_parsed_manifest_u128(parsed_manifest128);
        }

        return item_list;
}

/**
 * @brief Extracts a file from an SSMC archive using 64-bit hashes.
 *
 * @details This helper function encapsulates the logic for extracting a file
 * from an archive that uses xxhash3_64. It handles parsing the manifest,
 * looking up chunks, and decompressing data.
 *
 * @param archive_file Pointer to the open archive file.
 * @param header Pointer to the archive's file header.
 * @param manifest_buf Buffer containing the manifest data.
 * @param chunk_index_buf Buffer containing the chunk index data.
 * @param dictionary_buf Buffer containing the decompression dictionary.
 * @param file_inside_archive The name of the file to extract.
 * @param file_index Index of the file to extract.
 * @param target_dir The directory to extract the file to.
 * @return A dynamically allocated string containing the full path to the
 *         extracted file on success, or NULL on failure.
 */
static char* extract_u64(
    FILE *archive_file,
    const FileHeader *header,
    const uint8_t *manifest_buf,
    const uint8_t *chunk_index_buf,
    const uint8_t *dictionary_buf,
    const char *file_inside_archive,
    int file_index,
    const char *target_dir
) {
    FFIParsedManifestArrayU64 *p_manifest64 = NULL;
    ChunkIndexHandleU64 *chunk_index_handle64 = NULL;
    char *output_path = NULL;
    FILE *out_file = NULL;
    bool success = false;

    if (parse_file_metadata_u64(
        manifest_buf,
        header->man_length,
        &p_manifest64
    ) != StatusOk){
        //Todo: Log error, likely malformed archive
        goto cleanup;
    }
    if (prepare_chunk_index_u64(
        chunk_index_buf,
        header->chunk_index_length,
        &chunk_index_handle64
    ) != StatusOk) {
        //Todo: Log error, likely malformed archive
        goto cleanup;
    }

    const FFIFileManifestParentU64 *target_manifest = NULL;

    if (file_index >= 0 && file_index < p_manifest64->manifests_len) {
        // Direct index lookup
        target_manifest = &p_manifest64->manifests[file_index];
    } else if (file_inside_archive) {
        // Exhaustive fallback
        for (uintptr_t i = 0; i < p_manifest64->manifests_len; i++) {
            if (strcmp(
                p_manifest64->manifests[i].filename,
                file_inside_archive
            ) == 0) {
                target_manifest = &p_manifest64->manifests[i];
                break;
            }
        }
    }

    if (!target_manifest) goto cleanup;

    output_path = malloc(
        strlen(target_dir) + 1 + strlen(target_manifest->filename) + 1
    );
    if (!output_path) goto cleanup;
    sprintf(output_path, "%s/%s", target_dir, target_manifest->filename);

    out_file = fopen(output_path, "wb");
    if (!out_file) goto cleanup;

    for (uintptr_t i = 0; i < target_manifest->chunk_metadata_len; i++) {
        const FFISSAChunkMeta_u64 *chunk_meta = &target_manifest->chunk_metadata[i];
        FFIChunkLocation location;
        if (lookup_chunk_location_u64(
            chunk_index_handle64,
            chunk_meta->hash,
            &location
        ) != StatusOk) {
            //Todo: Log error, ssmc chunk hash not found in archive
            goto cleanup;
        }

        uint8_t *comp_buf = malloc(location.length);
        if (!comp_buf) {
            //Todo: Log error, memory allocation failure
            goto cleanup;
        }
        uint8_t *decomp_buf = malloc(chunk_meta->length);
        if (!decomp_buf) {
            //Todo: Log error, memory allocation failure
            free(comp_buf);
            goto cleanup;
        }

        if (fseek(
            archive_file,
            header->data_offset + location.offset,
            SEEK_SET
        ) != 0){
            //Todo: Log error, archive seek failure
            free(comp_buf);
            free(decomp_buf);
            goto cleanup;
        }
        if (fread(
            comp_buf,
            1,
            location.length,
            archive_file
        ) != location.length){
            //Todo: Log error, archive read failure
            free(comp_buf);
            free(decomp_buf);
            goto cleanup;
        }

        FFIResult res = decompress_chunk_c(
            comp_buf,
            location.length,
            dictionary_buf,
            header->dict_length,
            decomp_buf,
            chunk_meta->length
        );

        if (res == StatusOk){
            if (fwrite(
                decomp_buf, 1,
                chunk_meta->length,
                out_file
            ) != chunk_meta->length) {
                //Todo: Log error, archive read failure
                free(comp_buf);
                free(decomp_buf);
                goto cleanup;
            }
        }

        free(comp_buf);
        free(decomp_buf);
        if (res != StatusOk) goto cleanup;
    }
    //Todo: Log msg, file scuessfully decompressed.
    success = true;

    cleanup:
        if (out_file) fclose(out_file);
        if (p_manifest64) free_parsed_manifest_u64(p_manifest64);
        if (chunk_index_handle64) free_chunk_index_u64(chunk_index_handle64);

        if(success) {
            return output_path;
        } else {
            if (output_path) {
                remove(output_path);
                free(output_path);
            }
            return NULL;
        }
}

/**
 * @brief Extracts a file from an SSMC archive using 128-bit hashes.
 *
 * This helper function encapsulates the logic for extracting a file from an
 * archive that uses xxhash3_128. It handles parsing the manifest, looking up
 * chunks, and decompressing data.
 *
 * @param archive_file Pointer to the open archive file.
 * @param header Pointer to the archive's file header.
 * @param manifest_buf Buffer containing the manifest data.
 * @param chunk_index_buf Buffer containing the chunk index data.
 * @param dictionary_buf Buffer containing the decompression dictionary.
 * @param file_inside_archive The name of the file to extract.
 * @param file_index: Index of the file to extract.
 * @param out_file Pointer to the open output file.
 * * @return A dynamically allocated string containing the full path to the
 *           extracted file on success, or NULL on failure.
 */
static char* extract_u128(
    FILE *archive_file,
    const FileHeader *header,
    const uint8_t *manifest_buf,
    const uint8_t *chunk_index_buf,
    const uint8_t *dictionary_buf,
    const char *file_inside_archive,
    int file_index,
    const char *target_dir
) {
    FFIParsedManifestArrayU128 *p_manifest128 = NULL;
    ChunkIndexHandleU128 *chunk_index_handle128 = NULL;
    char *output_path = NULL;
    FILE *out_file = NULL;
    bool success = false;

    if (parse_file_metadata_u128(
        manifest_buf, header->man_length,
        &p_manifest128
    ) != StatusOk) {
        //Todo: Log error, likely malformed archive
        goto cleanup;
    }

    if (prepare_chunk_index_u128(
        chunk_index_buf,
        header->chunk_index_length,
        &chunk_index_handle128
    ) != StatusOk) {
        //Todo: Log error, likely malformed archive
        goto cleanup;
    }

    const FFIFileManifestParentU128 *target_manifest = NULL;

    if (file_index >= 0 && file_index < p_manifest128->manifests_len) {
        // Direct index lookup
            target_manifest = &p_manifest128->manifests[file_index];
    } else if (file_inside_archive) {
        // Exhaustive fallback
        for (uintptr_t i = 0; i < p_manifest128->manifests_len; i++) {
            if (strcmp(
                p_manifest128->manifests[i].filename, file_inside_archive
            ) == 0) {
                target_manifest = &p_manifest128->manifests[i];
                break;
            }
        }
    }

    if (!target_manifest) goto cleanup;

    output_path = malloc(
        strlen(target_dir) + 1 + strlen(target_manifest->filename) + 1
    );
    if (!output_path) goto cleanup;
    sprintf(output_path, "%s/%s", target_dir, target_manifest->filename);

    out_file = fopen(output_path, "wb");
    if (!out_file) goto cleanup;

    for (uintptr_t i = 0; i < target_manifest->chunk_metadata_len; i++) {
        const FFISSAChunkMeta_U128Bytes *chunk_meta = &target_manifest->chunk_metadata[i];
        FFIChunkLocation location;
        if (lookup_chunk_location_u128(
            chunk_index_handle128,
            chunk_meta->hash.bytes,
            &location
        ) != StatusOk) {
            //Todo: Log error, ssmc chunk hash not found in archive
            goto cleanup;
        }

        uint8_t *comp_buf = malloc(location.length);
        if (!comp_buf) {
            //Todo: Log error, memory allocation failure
            goto cleanup;
        }
        uint8_t *decomp_buf = malloc(chunk_meta->length);
        if (!decomp_buf) {
            //Todo: Log error, memory allocation failure
            free(comp_buf);
            goto cleanup;
        }

        if (fseek(
            archive_file,
            header->data_offset + location.offset,
            SEEK_SET
        ) != 0){
            //Todo: Log error, archive seek failure
            free(comp_buf);
            free(decomp_buf);
            goto cleanup;
        }
        if (fread(
            comp_buf,
            1,
            location.length,
            archive_file
        ) != location.length){
            //Todo: Log error, archive read failure
            free(comp_buf);
            free(decomp_buf);
            goto cleanup;
        }

        FFIResult res = decompress_chunk_c(
            comp_buf,
            location.length,
            dictionary_buf,
            header->dict_length,
            decomp_buf,
            chunk_meta->length
        );

        if (res == StatusOk){
            if (fwrite(
                decomp_buf, 1,
                chunk_meta->length,
                out_file
            ) != chunk_meta->length) {
                //Todo: Log error, archive read failure
                free(comp_buf);
                free(decomp_buf);
                goto cleanup;
            }
        }

        free(comp_buf);
        free(decomp_buf);
        if (res != StatusOk) goto cleanup;
    }
    //Todo: Log msg, file scuessfully decompressed.
    success = true;

    cleanup:
        if (out_file) fclose(out_file);
        if (p_manifest128) free_parsed_manifest_u128(p_manifest128);
        if (chunk_index_handle128) free_chunk_index_u128(chunk_index_handle128);

        if(success) {
            return output_path;
        } else {
            if (output_path) {
                remove(output_path);
                free(output_path);
            }
            return NULL;
        }

}


/**
 * @brief Extracts a single file from an SSMC archive to a target directory.
 *
 * This is the main extraction function. It reads the archive header, allocates
 * memory for the main data sections (manifest, chunk index, dictionary), and
 * then delegates to the appropriate helper function (extract_u64 or
 * extract_u128) based on the hash type specified in the header.
 *
 * @param archive_path The full path to the SSMC archive file.
 * @param file_inside_archive The name of the file to extract (can be NULL if
 *        using index).
 * @param file_index The index of the file to extract (use -1 if using name).
 * @param target_dir The directory to extract the file to.
 * @return A dynamically allocated string containing the full path to the
 *         extracted file on success, or NULL on failure.
 */
static char* ssmc_extract_file(
    const char *archive_path,
    const char *file_inside_archive,
    int file_index,
    const char *target_dir
){
    FILE *archive_file = NULL;
    uint8_t *manifest_buf = NULL;
    uint8_t *chunk_index_buf = NULL;
    uint8_t*dictionary_buf = NULL;
    char *result_path = NULL;

    archive_file = fopen(archive_path, "rb");
    if (!archive_file) goto cleanup;

    FileHeader header;
    if (fread(&header, sizeof(FileHeader), 1, archive_file) != 1) goto cleanup;
    if (memcmp(
        header.magic_num,
        MAGIC_NUMBER,
        sizeof(header.magic_num)
    ) != 0) goto cleanup;

    manifest_buf = malloc(header.man_length);
    if (!manifest_buf) goto cleanup;
    if (fseek(
        archive_file,
        header.man_offset,
        SEEK_SET
    ) != 0) goto cleanup;
    if (fread(
        manifest_buf, 1,
        header.man_length,
        archive_file
    ) != header.man_length) goto cleanup;

    chunk_index_buf = malloc(header.chunk_index_length);
    if (!chunk_index_buf) goto cleanup;
    if (fseek(
        archive_file,
        header.chunk_index_offset,
        SEEK_SET
    ) != 0) goto cleanup;
    if (fread(
        chunk_index_buf,
        1,
        header.chunk_index_length,
        archive_file
    ) != header.chunk_index_length) goto cleanup;

    dictionary_buf = malloc(header.dict_length);
    if (!dictionary_buf) goto cleanup;
    if (fseek(
        archive_file,
        header.dict_offset,
        SEEK_SET
    ) != 0) goto cleanup;
    if (fread(
        dictionary_buf,
        1,
        header.dict_length,
        archive_file
    ) != header.dict_length) goto cleanup;

    if (header.hash_type == 1) {
        result_path = extract_u64(
            archive_file,
            &header,
            manifest_buf,
            chunk_index_buf,
            dictionary_buf,
            file_inside_archive,
            file_index,
            target_dir
        );
    } else if (header.hash_type == 2) {
        result_path = extract_u128(
            archive_file,
            &header,
            manifest_buf,
            chunk_index_buf,
            dictionary_buf,
            file_inside_archive,
            file_index,
            target_dir
        );
    } else {
        //Todo: Log error, unsupported hash type
        goto cleanup;
    }

    cleanup:
        if (archive_file) fclose(archive_file);

        if (manifest_buf) free(manifest_buf);
        if (chunk_index_buf) free(chunk_index_buf);
        if (dictionary_buf) free(dictionary_buf);

        return result_path;
}

static const char* ssmc_get_handler_name(void) {
    return "SSMC Archive Handler";
}

static const char** ssmc_get_supported_extensions(void) {
    return ssmc_extensions;
}

static ArchiveVTable arc_ssmc_vtable = {
    .is_supported = ssmc_is_supported,
    .list_contents = ssmc_list_contents,
    .extract_file = ssmc_extract_file,
    .get_handler_name = ssmc_get_handler_name,
    .get_supported_extensions = ssmc_get_supported_extensions,
};

ArchiveVTable* get_ssmc_archive_handler(void) {
    return &arc_ssmc_vtable;
}
