#ifndef BUFFER_ACCESS_H
#define BUFFER_ACCESS_H

#include "stdint.h"
#include "stdbool.h"
#include "string.h" // memcpy, memset

#ifdef __cplusplus
extern "C" {
#endif



#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef MIXED_ENDIAN 
#define MIXED_ENDIAN 2143
#endif

#ifndef MIDDLE_ENDIAN
#define MIDDLE_ENDIAN 3412
#endif


#define B16_ENDIANESS_12 12
#define B16_ENDIANESS_21 21

#define B32_ENDIANESS_1234 1234
#define B32_ENDIANESS_4321 4321
#define B32_ENDIANESS_2143 2143
#define B32_ENDIANESS_3412 3412

#define B64_ENDIANESS_12345678 12345678 // Little Endian
#define B64_ENDIANESS_87654321 87654321 // Big Endian

#define B64_ENDIANESS_56781234 56781234
#define B64_ENDIANESS_43218765 43218765

#define B64_ENDIANESS_34127856 34127856
#define B64_ENDIANESS_65872143 65872143

#define B64_ENDIANESS_78563412 78563412
#define B64_ENDIANESS_21436587 21436587



uint8_t  parse_uint8 (const uint8_t* const data);
uint16_t parse_uint16(const uint8_t* const data, const int endianness);
uint32_t parse_uint32(const uint8_t* const data, const int endianness);
uint64_t parse_uint64(const uint8_t* const data, const int endianness);
float    parse_float (const uint8_t* const data, const int endianness);
double   parse_double(const uint8_t* const data, const int endianness);

void copy_uint8_into_buffer (const uint8_t  value, uint8_t* data);
void copy_uint16_into_buffer(const uint16_t value, uint8_t* data, const int endianness);
void copy_uint32_into_buffer(const uint32_t value, uint8_t* data, const int endianness);
void copy_uint64_into_buffer(const uint64_t value, uint8_t* data, const int endianness);
void copy_float_into_buffer (const float    value, uint8_t* data, const int endianness);
void copy_double_into_buffer(const double   value, uint8_t* data, const int endianness);




void convert_endianess_16(const uint8_t* const in, uint8_t* const out, const int endianness);
void convert_endianess_32(const uint8_t* const in, uint8_t* const out, const int endianness);
void convert_endianess_64(const uint8_t* const in, uint8_t* const out, const int endianness);



void swap_uint8 (uint8_t*  const v1, uint8_t*  const v2);
void swap_uint16(uint16_t* const v1, uint16_t* const v2);
void swap_uint32(uint32_t* const v1, uint32_t* const v2);
void swap_uint64(uint64_t* const v1, uint64_t* const v2);
void swap_float (float*    const v1, float*    const v2);
void swap_double(double*   const v1, double*   const v2);

void reverse_elements_in_buffer(uint8_t* const buffer, const uint32_t buffer_size);


#ifdef __cplusplus
};
#endif

#endif // BUFFER_ACCESS_H