#ifndef FILE_ACCESS_H
#define FILE_ACCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h> // FILE*

#ifdef __cplusplus
extern "C" {
#endif



// Initialize and mount the SPIFFS filesystem
bool mount_filesystem();

// ummount the SPIFFS filesystem
bool unmount_filesystem();



// Open a file on the SPIFFS filesystem
FILE* open_file(const char* filename, const char* mode);

// Close a file on the SPIFFS filesystem
int close_file(FILE* const file);


// read string from file (using fgets)
char* read_from_file(FILE* const file, char* const buffer, const int bufsize);

// write string to file (using fputs)
int write_to_file(FILE* const file, const char* const buffer);

// read data from file (using fread)
int read_data_from_file(FILE* const file, uint8_t* const buffer, const int bufsize);

// write data to file (using fwrite)
int write_data_to_file(FILE* const file, const uint8_t* const buffer, const int bufsize);



// Open file, read string from file (using fgets), close file
bool read_file_from_filesystem(const char* filename, char* const buffer, const int bufsize);

// Open file, write string to file (using fputs), close file
bool write_file_to_filesystem(const char* filename, const char* const buffer);

// Open file, read data from file (using fread), close file
bool read_data_from_filesystem(const char* filename, uint8_t* const buffer, const int bufsize);

// Open file, write data to file (using fwrite), close file
bool write_data_to_filesystem(const char* filename, const uint8_t* const buffer, const int bufsize);



// Mount the filesystem, open file, read string from file (using fgets), close file, unmount the filesystem
bool read_file_from_storage(const char* filename, char* const buffer, const int bufsize);

// Mount the filesystem, open file, write string to file (using fputs), close file, unmount the filesystem
bool write_file_to_storage(const char* filename, const char* const buffer);

// Mount the filesystem, open file, read data from file (using fread), close file, unmount the filesystem
bool read_data_from_storage(const char* filename, uint8_t* const buffer, const int bufsize);

// Mount the filesystem, open file, write data to file (using fwrite), close file, unmount the filesystem
bool write_data_to_storage(const char* filename, const uint8_t* const buffer, const int bufsize);



// Check if file exists
bool does_file_exist_on_filesystem(const char* filename);

// Mount the filesystem, check if file exists, unmount the filesystem
bool does_file_exist_in_storage(const char* filename);



// Get the size of a file
int get_file_size_from_filesystem(const char* filename);

// Mount the filesystem, get the size of a file, unmount the filesystem
int get_file_size_from_storage(const char* filename);

// Alias for 'get_file_size_from_storage' (Mount the filesystem, get the size of a file, unmount the filesystem)
int get_file_size(const char* filename);



// Use get_file_size to determine the needed buffer size.
// Allocate a buffer an read the file from the filesystem
// The returned pointer needs to be freed after use.
bool read_data_file_from_filesysteme(const char* filename, uint8_t** buffer, int* filesize);

// Mount the filesystem
// Use get_file_size to determine the needed buffer size.
// Allocate a buffer an read the file from the filesystem
// Unmount the filesystem
// The returned pointer needs to be freed after use.
bool read_data_file_from_storage(const char* filename, uint8_t** buffer, int* filesize);



#ifdef __cplusplus
};
#endif

#endif // FILE_ACCESS_H