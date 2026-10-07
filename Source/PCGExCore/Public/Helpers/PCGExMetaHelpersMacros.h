// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

// Lightweight macro header - no heavy PCG metadata includes
// For template functions that use these macros, include PCGExMetaHelpers.h instead

#pragma once

#pragma region Macros

#define PCGEX_FOREACH_SUPPORTEDTYPES(MACRO, ...) \
MACRO(float, Float, __VA_ARGS__)      \
MACRO(double, Double, __VA_ARGS__)     \
MACRO(int32, Integer32, __VA_ARGS__)      \
MACRO(int64, Integer64, __VA_ARGS__)      \
MACRO(FVector2D, Vector2, __VA_ARGS__)  \
MACRO(FVector, Vector, __VA_ARGS__)    \
MACRO(FVector4, Vector4, __VA_ARGS__)   \
MACRO(FQuat, Quaternion, __VA_ARGS__)      \
MACRO(FTransform, Transform, __VA_ARGS__) \
MACRO(FString, String, __VA_ARGS__)    \
MACRO(bool, Boolean, __VA_ARGS__)       \
MACRO(FRotator, Rotator, __VA_ARGS__)   \
MACRO(FName, Name, __VA_ARGS__)\
MACRO(FSoftObjectPath, SoftObjectPath, __VA_ARGS__)\
MACRO(FSoftClassPath, SoftClassPath, __VA_ARGS__)
//MACRO(uint8, Byte, __VA_ARGS__)\
//MACRO(FText, Text, __VA_ARGS__)

#define PCGEX_INNER_FOREACH_TYPE2(_TYPE_A, _NAME_A, MACRO, ...) \
MACRO(_TYPE_A, _NAME_A, float, Float, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, double, Double, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, int32, Integer32, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, int64, Integer64, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FVector2D, Vector2, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FVector, Vector, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FVector4, Vector4, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FQuat, Quaternion, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FTransform, Transform, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FString, String, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, bool, Boolean, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FRotator, Rotator, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FName, Name, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FSoftObjectPath, SoftObjectPath, __VA_ARGS__) \
MACRO(_TYPE_A, _NAME_A, FSoftClassPath, SoftClassPath, __VA_ARGS__)
//MACRO(_TYPE_A, _NAME_A, uint8, Byte, __VA_ARGS__) \
//MACRO(_TYPE_A, _NAME_A, FText, Text, __VA_ARGS__)

#define PCGEX_FOREACH_SUPPORTEDTYPES_PAIRS(MACRO, ...) \
PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_INNER_FOREACH_TYPE2, MACRO, __VA_ARGS__)

#define PCGEX_EXECUTEWITHRIGHTTYPE_CASE(_TYPE, _NAME, MACRO) case EPCGMetadataTypes::_NAME : MACRO(_TYPE, _NAME) break;
#define PCGEX_EXECUTEWITHRIGHTTYPE(_TYPE, MACRO) switch (_TYPE){ PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_EXECUTEWITHRIGHTTYPE_CASE, MACRO) default: ; }

#define PCGEX_FOREACH_POINT_NATIVE_PROPERTY(MACRO, ...)\
MACRO(Transform, FTransform, __VA_ARGS__) \
MACRO(Density, float, __VA_ARGS__) \
MACRO(BoundsMin, FVector, __VA_ARGS__) \
MACRO(BoundsMax, FVector, __VA_ARGS__) \
MACRO(Color, FVector4, __VA_ARGS__) \
MACRO(Steepness, float, __VA_ARGS__) \
MACRO(Seed, int32, __VA_ARGS__) \
MACRO(MetadataEntry, int64, __VA_ARGS__)

#pragma endregion
