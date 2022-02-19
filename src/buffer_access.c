#include "buffer_access.h"
#define NULL ((void *)0) // #include "stdlib.h"


// #define B16_ENDIANESS_12 12
// #define B16_ENDIANESS_21 21

// #define B32_ENDIANESS_1234 1234
// #define B32_ENDIANESS_4321 4321
// #define B32_ENDIANESS_2143 2143
// #define B32_ENDIANESS_3412 3412

// #define B64_ENDIANESS_12345678 12345678
// #define B64_ENDIANESS_87654321 87654321

// #define B64_ENDIANESS_56781234 56781234
// #define B64_ENDIANESS_43218765 43218765

// #define B64_ENDIANESS_34127856 34127856
// #define B64_ENDIANESS_65872143 65872143

// #define B64_ENDIANESS_78563412 78563412
// #define B64_ENDIANESS_21436587 21436587

void convert_endianess_16(const uint8_t* const in, uint8_t* const out, const int endianness) {
    if (in == NULL || out == NULL) { return; }

    switch (endianness) {
        case B16_ENDIANESS_12:
        case LITTLE_ENDIAN: {
            // out[0] = in[0];
            // out[1] = in[1];
            *((uint16_t*) out) = *((const uint16_t*) in);
        }
        break;
        case B16_ENDIANESS_21:
        case BIG_ENDIAN: {
            out[0] = in[1];
            out[1] = in[0];
        }
        break;
    }
}

void convert_endianess_32(const uint8_t* const in, uint8_t* const out, const int endianness) {
    if (in == NULL || out == NULL) { return; }
    
    switch (endianness) {
        case LITTLE_ENDIAN: {
            // out[0] = in[0];
            // out[1] = in[1];
            // out[2] = in[2];
            // out[3] = in[3];
            *((uint32_t*) out) = *((const uint32_t*) in);
        }
        break;
        case BIG_ENDIAN: {
            // Reorder bytes
            out[3] = in[0];
            out[2] = in[1];
            out[1] = in[2];
            out[0] = in[3];
        }
        break;
        case MIXED_ENDIAN: {
            // Reorder bytes
            out[1] = in[0];
            out[0] = in[1];
            out[3] = in[2];
            out[2] = in[3];
        }
        break;
        case MIDDLE_ENDIAN: {
            // Reorder bytes
            out[2] = in[0];
            out[3] = in[1];
            out[0] = in[2];
            out[1] = in[3];
        }
        break;
    }
}

void convert_endianess_64(const uint8_t* const in, uint8_t* const out, const int endianness) {
    if (in == NULL || out == NULL) { return; }
    
    switch (endianness) {
        case LITTLE_ENDIAN:
        case B64_ENDIANESS_12345678: {
            // out[0] = in[0];
            // out[1] = in[1];
            // out[2] = in[2];
            // out[3] = in[3];
            // out[4] = in[4];
            // out[5] = in[5];
            // out[6] = in[6];
            // out[7] = in[7];
            *((uint64_t*) out) = *((const uint64_t*) in);
        }
        break;
        case BIG_ENDIAN:
        case B64_ENDIANESS_87654321: {
            // Reorder bytes
            out[7] = in[0];
            out[6] = in[1];
            out[5] = in[2];
            out[4] = in[3];
            out[3] = in[4];
            out[2] = in[5];
            out[1] = in[6];
            out[0] = in[7];
        }
        break;
        case B64_ENDIANESS_56781234: {
            // Reorder bytes
            out[4] = in[0];
            out[5] = in[1];
            out[6] = in[2];
            out[7] = in[3];
            out[0] = in[4];
            out[1] = in[5];
            out[2] = in[6];
            out[3] = in[7];
        }
        break;
        case B64_ENDIANESS_43218765: {
            // Reorder bytes
            out[3] = in[0];
            out[2] = in[1];
            out[1] = in[2];
            out[0] = in[3];
            out[7] = in[4];
            out[6] = in[5];
            out[5] = in[6];
            out[4] = in[7];
        }
        break;
        case B64_ENDIANESS_34127856: {
            // Reorder bytes
            out[2] = in[0];
            out[3] = in[1];
            out[0] = in[2];
            out[1] = in[3];
            out[6] = in[4];
            out[7] = in[5];
            out[4] = in[6];
            out[5] = in[7];
        }
        break;
        case B64_ENDIANESS_65872143: {
            // Reorder bytes
            out[5] = in[0];
            out[4] = in[1];
            out[7] = in[2];
            out[6] = in[3];
            out[1] = in[4];
            out[0] = in[5];
            out[3] = in[6];
            out[2] = in[7];
        }
        break;
        case B64_ENDIANESS_78563412: {
            // Reorder bytes
            out[6] = in[0];
            out[7] = in[1];
            out[4] = in[2];
            out[5] = in[3];
            out[2] = in[4];
            out[3] = in[5];
            out[0] = in[6];
            out[1] = in[7];
        }
        break;
        case B64_ENDIANESS_21436587: {
            // Reorder bytes
            out[1] = in[0];
            out[0] = in[1];
            out[3] = in[2];
            out[2] = in[3];
            out[5] = in[4];
            out[4] = in[5];
            out[7] = in[6];
            out[6] = in[7];
        }
        break;
    }
}


// Works because ESP32 is Little Endian system
// make sure data contains enough space

uint8_t parse_uint8 (const uint8_t* const data) {
    return *data;
}

uint16_t parse_uint16(const uint8_t* const data, const int endianness) {
    uint16_t buffer = 0;
    convert_endianess_16(data, (uint8_t*)  &buffer, endianness);
    return buffer;
}

uint32_t parse_uint32(const uint8_t* const data, const int endianness) {
    uint32_t buffer = 0;
    convert_endianess_32(data, (uint8_t*)  &buffer, endianness);
    return buffer;
}

uint64_t parse_uint64(const uint8_t* const data, const int endianness) {
    uint64_t buffer = 0;
    convert_endianess_64(data, (uint8_t*)  &buffer, endianness);
    return buffer;
}

float parse_float (const uint8_t* const data, const int endianness) {
    float buffer = 0;
    convert_endianess_32(data, (uint8_t*) &buffer, endianness);
    return buffer;
}

double parse_double(const uint8_t* const data, const int endianness) {
    double buffer = 0;
    convert_endianess_64(data, (uint8_t*) &buffer, endianness);
    return buffer;
}



void copy_uint8_into_buffer (const uint8_t value, uint8_t* data) {
    *data = value;
}

void copy_uint16_into_buffer(const uint16_t value, uint8_t* data, const int endianness) {
    convert_endianess_16((const uint8_t*) &value, data, endianness);
}

void copy_uint32_into_buffer(const uint32_t value, uint8_t* data, const int endianness) {
    convert_endianess_32((const uint8_t*) &value, data, endianness);
}

void copy_uint64_into_buffer(const uint64_t value, uint8_t* data, const int endianness) {
    convert_endianess_64((const uint8_t*) &value, data, endianness);
}

void copy_float_into_buffer (const float value, uint8_t* data, const int endianness) {
    convert_endianess_32((const uint8_t*) &value, data, endianness);
}

void copy_double_into_buffer(const double value, uint8_t* data, const int endianness) {
    convert_endianess_64((const uint8_t*) &value, data, endianness);
}






void swap_uint8(uint8_t* const v1, uint8_t* const v2) {
    const uint8_t temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}

void swap_uint16(uint16_t* const v1, uint16_t* const v2) {
    const uint16_t temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}

void swap_uint32(uint32_t* const v1, uint32_t* const v2) {
    const uint32_t temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}

void swap_uint64(uint64_t* const v1, uint64_t* const v2) {
    const uint64_t temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}

void swap_float(float* const v1, float* const v2) {
    const float temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}

void swap_double(double* const v1, double* const v2) {
    const double temp = *v1;
    *v1 = *v2;
    *v2 = temp;
}


void reverse_elements_in_buffer(uint8_t* const buffer, const uint32_t buffer_size) {

    for (uint32_t i = 0; i < buffer_size / 2; ++i) {
        swap_uint8(&buffer[i], &buffer[buffer_size-1 - i]);
    }
}