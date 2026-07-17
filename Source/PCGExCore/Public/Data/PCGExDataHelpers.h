// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

enum class EPCGExInputValueType : uint8;
struct FPCGExContext;

namespace PCGExData
{
	class IBuffer;
	class FPointIO;
	class FFacade;
	class FPropertyArrayBuffer;
	struct FAttributeIdentity;
}

namespace PCGExData::Helpers
{
	/**
	 * Canonical read slot for a single-value (@Data-like) attribute: first entry when the domain
	 * carries items, default-value slot otherwise. Single source of truth for ReadDataValue and
	 * every promotion/copy path that reads a data-domain value.
	 */
	PCGEXCORE_API PCGMetadataEntryKey GetDataValueKey(const FPCGMetadataAttributeBase* Attribute);

	/** True when the attribute holds a readable value address at the given key (pre-validation for property-based copies, e.g. before destructive delete/recreate sequences). */
	PCGEXCORE_API bool HasPropertyCopyableValue(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key);

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute);

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute, T Fallback);

	template <typename T>
	void SetDataValue(FPCGMetadataAttributeBase* Attribute, const T Value);

	template <typename T>
	void SetDataValue(UPCGData* InData, FName Name, const T Value);

	template <typename T>
	void SetDataValue(UPCGData* InData, FPCGAttributeIdentifier Identifier, const T Value);

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute); \
extern template _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute, _TYPE Fallback); \
extern template void SetDataValue<_TYPE>(FPCGMetadataAttributeBase* Attribute, const _TYPE Value); \
extern template void SetDataValue<_TYPE>(UPCGData* InData, FName Name, const _TYPE Value); \
extern template void SetDataValue<_TYPE>(UPCGData* InData, FPCGAttributeIdentifier Identifier, const _TYPE Value);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	constexpr static EPCGMetadataTypes GetNumericType(const EPCGExNumericOutput InType)
	{
		switch (InType)
		{
		case EPCGExNumericOutput::Double:
			return EPCGMetadataTypes::Double;
		case EPCGExNumericOutput::Float:
			return EPCGMetadataTypes::Float;
		case EPCGExNumericOutput::Int32:
			return EPCGMetadataTypes::Integer32;
		case EPCGExNumericOutput::Int64:
			return EPCGMetadataTypes::Integer64;
		}

		return EPCGMetadataTypes::Unknown;
	}

	template <typename T>
	bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryReadDataValue(FPCGExContext* InContext, const UPCGData* InData, const FName& InName, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryReadDataValue(const TSharedPtr<FPointIO>& InIO, const FName& InName, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryReadDataValue(const TSharedPtr<FPointIO>& InIO, const FPCGAttributePropertyInputSelector& InSelector, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryGetSettingDataValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const T& InConstant, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryGetSettingDataValue(FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FName& InName, const T& InConstant, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryGetSettingDataValue(const TSharedPtr<FPointIO>& InIO, const EPCGExInputValueType Input, const FName& InName, const T& InConstant, T& OutValue, const bool bQuiet = false);

	template <typename T>
	bool TryGetSettingDataValue(const TSharedPtr<FPointIO>& InIO, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const T& InConstant, T& OutValue, const bool bQuiet = false);

	/**
	 * Copy all pending writable buffer values from a source facade to a target FPointIO.
	 * Creates a temporary facade for the target, creates matching writable buffers,
	 * copies values using type-erased GetVoid/SetVoid, and commits synchronously.
	 * @param SourceFacade Source facade with pending writable buffer values
	 * @param TargetIO Target point IO to write values to
	 * @param SourcePointIndices Maps target point index i -> source point index SourcePointIndices[i]
	 * @param IgnoreList List of names to skip
	 */
	PCGEXCORE_API void CopyBuffersValues(
		const TSharedPtr<FFacade>& SourceFacade,
		const TSharedPtr<FFacade>& TargetIO,
		const TArray<int32>& SourcePointIndices,
		const TSet<FName>* IgnoreList = nullptr);

	/**
	 * Single-value property-aware attribute copy (data domain).
	 * Reads source attribute's value at SourceKey via void* and writes to target attribute at TargetKey
	 * using FProperty reflection (CopyCompleteValue under the hood). Works for ANY attribute type the
	 * property factory supports -- basic scalars, extended scalars (Struct/Enum/Object/...) and containers
	 * (TArray/TSet/TMap). Source and target descs must be compatible.
	 *
	 * @param SourceAttr Source attribute (must be non-null)
	 * @param SourceKey Entry key on source (PCGDefaultValueKey for data-domain values)
	 * @param TargetAttr Target attribute (must be non-null and have a compatible type)
	 * @param TargetKey Entry key on target
	 * @return true on success; false if pointers are null, descs differ, or no source value exists
	 */
	PCGEXCORE_API bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, PCGMetadataEntryKey TargetKey);

	/**
	 * Multi-target variant of PropertyCopyAttribute: reads the source value once and writes it to
	 * every provided target key with a single transient FProperty (the single-key version allocates
	 * one per call -- see the PERF note in the implementation). Use whenever the same value lands in
	 * more than one slot (default value + materialized entry, etc.).
	 */
	PCGEXCORE_API bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, TArrayView<const PCGMetadataEntryKey> TargetKeys);

	/**
	 * Element-range property-aware copy from one source IO's attribute into a property-backed
	 * target buffer. The merger uses this in its per-source-IO contribution path when the working
	 * type isn't covered by PCGEX_FOREACH_SUPPORTEDTYPES (Struct/Enum/Object/container/etc).
	 *
	 * Writes are deep-copied via FProperty::CopyCompleteValue, so containers and FString-flavored
	 * attributes carry through correctly without aliasing source allocators.
	 *
	 * @param SourceIO Source point IO providing both metadata and per-point entry keys
	 * @param SourceIdentity Identity describing the source attribute (must match target buffer's desc)
	 * @param TargetBuffer Property-backed writable buffer on the merge target
	 * @param ReadScope Source range (Start..End on the source IO)
	 * @param WriteScope Target range (Start..End on the target buffer; same Count as ReadScope)
	 * @param bReverse If true, source is iterated reversed so that ReadScope.End-1 → WriteScope.Start
	 * @return true if at least one element was copied
	 */
	PCGEXCORE_API bool PropertyCopyAttributeRange(
		const TSharedPtr<FPointIO>& SourceIO, const FAttributeIdentity& SourceIdentity,
		const TSharedRef<FPropertyArrayBuffer>& TargetBuffer,
		const PCGExMT::FScope& ReadScope, const PCGExMT::FScope& WriteScope, bool bReverse);

	/**
	 * Property-aware broadcast: read a single value from SourceAttr at SourceKey, then deep-copy it
	 * into ALL slots of TargetWriter. Standard fallback shape for property-backed forward / carry
	 * paths (DataForward, PointsToBounds, spline-to-path data domain, etc.) where one source value
	 * fans out to N target slots.
	 *
	 * @param SourceAttr Source attribute to read from (must be non-null)
	 * @param SourceKey Entry key on source (PCGDefaultValueKey for data-domain values)
	 * @param TargetWriter Property-backed writable IBuffer; uses MakeScopedValue + SetVoid in a loop
	 * @return true if the value was deep-copied into at least one slot
	 */
	PCGEXCORE_API bool PropertyBroadcastAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		const TSharedPtr<IBuffer>& TargetWriter);

	/**
	 * Like PropertyBroadcastAttribute but writes to a specific set of target indices instead of
	 * all slots. Used by FDataForwardHandler::Forward(SourceIdx, FFacade, Indices).
	 */
	PCGEXCORE_API bool PropertyScatterAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		const TSharedPtr<IBuffer>& TargetWriter, TArrayView<const int32> Indices);

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template bool TryReadDataValue<_TYPE>(FPCGExContext* InContext, const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryReadDataValue<_TYPE>(FPCGExContext* InContext, const UPCGData* InData, const FName& InName, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryReadDataValue<_TYPE>(const TSharedPtr<PCGExData::FPointIO>& InIO, const FName& InName, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryReadDataValue<_TYPE>(const TSharedPtr<PCGExData::FPointIO>& InIO, const FPCGAttributePropertyInputSelector& InSelector, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryGetSettingDataValue<_TYPE>( FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryGetSettingDataValue<_TYPE>( FPCGExContext* InContext, const UPCGData* InData, const EPCGExInputValueType Input, const FName& InName, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryGetSettingDataValue<_TYPE>( const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExInputValueType Input, const FName& InName, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet); \
extern template bool TryGetSettingDataValue<_TYPE>( const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExInputValueType Input, const FPCGAttributePropertyInputSelector& InSelector, const _TYPE& InConstant, _TYPE& OutValue, const bool bQuiet);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL
}
