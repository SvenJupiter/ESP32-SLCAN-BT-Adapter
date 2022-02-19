#include "file_access.h"


#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
static const char* TAG = "Filesystem";


#include <stdlib.h>
// #include <stdio.h> // included in 'file_access.h' Header
#include <string.h>
#include <errno.h>
#include "esp_err.h"
#include "esp_spiffs.h"

#define WL_SECTOR_SIZE CONFIG_WL_SECTOR_SIZE

// Mount path for the partition
#define MOUNT_PATH "/spiffs"
#define MOUNT_PATH_LEN (sizeof(MOUNT_PATH)-1)


// base path for the partition
#define BASE_PATH MOUNT_PATH "/"
#define BASE_PATH_LEN (sizeof(BASE_PATH)-1)

#define FULL_FILEPATH_MAX_SIZE 128
// static char s_full_filepath[FULL_FILEPATH_MAX_SIZE] = BASE_PATH;

#define FILENAME_MAX_SIZE (FULL_FILEPATH_MAX_SIZE - BASE_PATH_LEN)
// static char* s_filename = s_full_filepath + BASE_PATH_LEN;


static bool s_filesystem_mounted = false;

// Use this settings to initialize and mount SPIFFS filesystem.
esp_vfs_spiffs_conf_t s_conf = {
    .base_path = MOUNT_PATH,
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
};


bool mount_filesystem() {
    if (s_filesystem_mounted) { return true; }
    ESP_LOGD(TAG, "Mounting filesystem");

    // Use settings to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ESP_LOGV(TAG, "Initializing SPIFFS");
    esp_err_t ret = esp_vfs_spiffs_register(&s_conf);

    // Check if successfull
    if (ret != ESP_OK) { // if not successfull
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }
    else { // if successfull
        size_t total = 0, used = 0;
        ret = esp_spiffs_info(s_conf.partition_label, &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        } else {
            if (used >= ((total*8)/10)) { ESP_LOGE(TAG, "Partition size: total: %d, used: %d", total, used); }
            else { ESP_LOGV(TAG, "Partition size: total: %d, used: %d", total, used); }
        }
        s_filesystem_mounted = true;
        return true;
    }

}

bool unmount_filesystem() {
    if (!s_filesystem_mounted) { return true; }

    // All done, unmount partition and disable SPIFFS
    ESP_LOGD(TAG, "Unmounting filesystem");
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_vfs_spiffs_unregister(s_conf.partition_label));
    ESP_LOGV(TAG, "SPIFFS unmounted");
    s_filesystem_mounted = false;
    return true;
}



FILE* open_file(const char* filename, const char* mode) {
    if (filename == NULL || mode == NULL) { return NULL; }

    // Just checking
    const int filename_len = strlen(filename);
    if (filename_len >= FILENAME_MAX_SIZE) { return NULL; }

    // prepend root path
    char full_filepath[FULL_FILEPATH_MAX_SIZE] = BASE_PATH;
    strcpy(full_filepath+BASE_PATH_LEN, filename);

    // Open file for reading
    ESP_LOGD(TAG, "Open file '%s'", full_filepath);
    FILE* file = fopen(full_filepath, mode);

    // Check
    if (file == NULL) { ESP_LOGE(TAG, "Failed to open file '%s': %s", full_filepath, strerror(errno)); }
    return file;
}

int close_file(FILE* const file) {
    return fclose(file);
}



char* read_from_file(FILE* const file, char* const buffer, const int bufsize) {
    return fgets(buffer, bufsize, file);
}

int write_to_file(FILE* const file, const char* const buffer) {
    return fputs(buffer, file);
}

int read_data_from_file(FILE* const file, uint8_t* const buffer, const int bufsize) {
    return fread(buffer, sizeof(uint8_t), bufsize, file);
}

int write_data_to_file(FILE* const file, const uint8_t* const buffer, const int bufsize) {
    return fwrite(buffer, sizeof(uint8_t), bufsize, file);
}



bool read_file_from_filesystem(const char* filename, char* const buffer, const int bufsize) {
    if (filename == NULL || buffer == NULL || bufsize <= 0) { return false; }
    
    FILE* file = open_file(filename, "r");
    if (file != NULL) {
        char* buffer_write_pos = buffer;
        int free_buf_len = bufsize - 1;

        ESP_LOGI(TAG, "Reading from file '%s'", filename);
        while (!feof(file) && free_buf_len > 0) {
            const char* line = read_from_file(file, buffer_write_pos, free_buf_len + 1);
            if (line == NULL) {
                if (feof(file)) { // EOF reached
                    close_file(file); 
                    return true; 
                } 
                else { // Read error
                    ESP_LOGE(TAG, "'%s': Read Error: %s", filename, strerror(errno)); 
                    close_file(file);
                    *(buffer + (bufsize-1)) = '\0'; // Make sure buffer is null terminated
                    return false; 
                } 
            }
            else {
                const int line_len = strlen(line);
                free_buf_len -= line_len;
                buffer_write_pos += line_len;
            }
        }
        close_file(file); 
        return true; 
    }
    return false;
}

bool write_file_to_filesystem(const char* filename, const char* const buffer) {
    if (filename == NULL || buffer == NULL) { return false; }
    FILE* file = open_file(filename, "w");
    if (file != NULL) { 
        ESP_LOGI(TAG, "Writing to file '%s'", filename);
        const int res = write_to_file(file, buffer);
        if (res <= 0) { ESP_LOGE(TAG, "'%s': Write Error: %s", filename, strerror(errno)); return false; }
        close_file(file);
        return true;
    }
    return false;
}

bool read_data_from_filesystem(const char* filename, uint8_t* const buffer, const int bufsize) {
    if (filename == NULL || buffer == NULL || bufsize <= 0) { return false; }
    
    FILE* file = open_file(filename, "rb");
    if (file != NULL) {
        ESP_LOGI(TAG, "Reading from file '%s'", filename);
        const int bytes_read = read_data_from_file(file, buffer, bufsize);
        if (ferror(file)) { // Read Error
            ESP_LOGE(TAG, "'%s': Read Error: %s", filename, strerror(errno)); 
            close_file(file);
            return true; 
        }
        else if (bytes_read < bufsize) {
            ESP_LOGW(TAG, "'%s': Not enough bytes read. File is to small.", filename); 
            close_file(file); 
            return true; 
        }
        else if (!feof(file)) { // EOF not reached
            int c = fgetc(file); // Check if next char is EOF
            if (c != EOF) {
                ESP_LOGW(TAG, "'%s': Not all bytes read. Buffer is to small.", filename); 
            }
            close_file(file);
            return true; 
        }
        else {
            close_file(file); 
            return true; 
        }
    }
    return false;
}

bool write_data_to_filesystem(const char* filename, const uint8_t* const buffer, const int bufsize) {
    if (filename == NULL || buffer == NULL || bufsize <= 0) { return false; }
    
    FILE* file = open_file(filename, "wb");
    if (file != NULL) {
        ESP_LOGI(TAG, "Writing to file '%s'", filename);
        /*const int bytes_written = */write_data_to_file(file, buffer, bufsize);
        if (ferror(file)) { // Read Error
            ESP_LOGE(TAG, "'%s': Read Error: %s", filename, strerror(errno)); 
            close_file(file);
            return true; 
        }
        else {
            close_file(file); 
            return true; 
        }
    }
    return false;
}





bool read_file_from_storage(const char* filename, char* const buffer, const int bufsize) {
    if (mount_filesystem()) {
        const bool success = read_file_from_filesystem(filename, buffer, bufsize);
        unmount_filesystem();
        return success;
    }
    return false;
}

bool write_file_to_storage(const char* filename, const char* const buffer) {
    if (mount_filesystem()) {
        const bool success = write_file_to_filesystem(filename, buffer);
        unmount_filesystem();
        return success;
    }
    return false;
}

bool read_data_from_storage(const char* filename, uint8_t* const buffer, const int bufsize) {
    if (mount_filesystem()) {
        const bool success = read_data_from_filesystem(filename, buffer, bufsize);
        unmount_filesystem();
        return success;
    }
    return false;
}

bool write_data_to_storage(const char* filename, const uint8_t* const buffer, const int bufsize) {
    if (mount_filesystem()) {
        const bool success = write_data_to_filesystem(filename, buffer, bufsize);
        unmount_filesystem();
        return success;
    }
    return false;
}




bool does_file_exist_on_filesystem(const char* filename) {
    // Open the file
    FILE *file = NULL;
    file = fopen(filename,"rb");

    if (file != NULL) {
        // Close the file
        fclose(file);
        return true;
    }
    else {
        return false;
    }
}

bool does_file_exist_in_storage(const char* filename) {
    if (mount_filesystem()) {
        const bool success = does_file_exist_on_filesystem(filename);
        unmount_filesystem();
        return success;
    }
    return false;
}
int get_file_size_from_filesystem(const char* filename) {

    // Open the file
    FILE *file = NULL;
    file = fopen(filename,"rb");

    if (file != NULL) {

        // Set position indicator to the end of the file
        fseek(file, 0, SEEK_END);

        // Get the current value of the position indicator.
        // For binary streams, this is the number of bytes 
        // from the beginning of the file.
        int size = ftell(file);

        // Close the file
        fclose(file);
        return size;
    }
    else {
        return -1;
    }
}

int get_file_size_from_storage(const char* filename) {
    if (mount_filesystem()) {
        const int size = get_file_size_from_filesystem(filename);
        unmount_filesystem();
        return size;
    }
    return -1;
}
int get_file_size(const char* filename) {
    return get_file_size_from_storage(filename);
}

// Use get_file_size to determine the needed buffer size.
// Allocate a buffer an read the file from the filesystem
// The returned pointer needs to be freed after use.
bool read_data_file_from_filesysteme(const char* filename, uint8_t** buffer, int* filesize) {

    *filesize = get_file_size_from_filesystem(filename);
    if (*filesize > 0) {
        *buffer = malloc(*filesize);
        return read_data_from_filesystem(filename, *buffer, *filesize);
    }
    else {
        return false;
    }
}

// Mount the filesystem
// Use get_file_size to determine the needed buffer size.
// Allocate a buffer an read the file from the filesystem
// Unmount the filesystem
// The returned pointer needs to be freed after use.
bool read_data_file_from_storage(const char* filename, uint8_t** buffer, int* filesize) {
    if (mount_filesystem()) {
        const bool success = read_data_file_from_filesysteme(filename, buffer, filesize);
        unmount_filesystem();
        return success;
    }
    return false;
}


