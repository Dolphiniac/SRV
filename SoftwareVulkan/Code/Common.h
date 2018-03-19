#pragma once

#include <stdint.h>

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint8_t uint8;
typedef int32_t int32;

uint32 Min( uint32 a, uint32 b ) {
	return a < b ? a : b;
}

size_t Min( size_t a, size_t b ) {
	return a < b ? a : b;
}

uint32 Min( uint32 a, size_t b ) {
	return a < ( uint32 )b ? a : ( uint32 )b;
}

uint32 Min( size_t a, uint32 b ) {
	return ( uint32 )a < b ? ( uint32 )a : b;
}

uint32 Max( uint32 a, uint32 b ) {
	return a > b ? a : b;
}

size_t Max( size_t a, size_t b ) {
	return a > b ? a : b;
}

int32 Min( int32 a, uint32 b ) {
	return a < ( int32 )b ? a : b;
}

int32 Min( uint32 a, int32 b ) {
	return ( int32 )a < b ? a : b;
}

#define ARRAY_LENGTH( x ) sizeof( x ) / sizeof( *x )

#define BIT( x ) 1 << x