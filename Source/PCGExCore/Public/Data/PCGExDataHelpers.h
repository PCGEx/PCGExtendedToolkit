// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataCommon.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

enum class EPCGExInputValueType : uint8;
struct FPCGExContext;

namespace PCGExData
{
	class FPointIO;
	class FFacade;
}

namespace PCGExData::Helpers
{
	template <typename T>
	T ReadDataValue(const FPCGMetadataAttribute<T>* Attribute);

	template <typename T>
	T ReadDataValue(const FPCGMetadataAttributeBase* Attribute, T Fallback);

	template <typename T>
	void SetDataValue(FPCGMetadataAttribute<T>* Attribute, const T Value);

	template <typename T>
	void SetDataValue(UPCGData* InData, FName Name, const T Value);

	template <typename T>
	void SetDataValue(UPCGData* InData, FPCGAttributeIdentifier Identifier, const T Value);

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttribute<_TYPE>* Attribute); \
extern template _TYPE ReadDataValue<_TYPE>(const FPCGMetadataAttributeBase* Attribute, _TYPE Fallback); \
extern template void SetDataValue<_TYPE>(FPCGMetadataAttribute<_TYPE>* Attribute, const _TYPE Value); \
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
	 * Registers the @Data attribute a data-value read consumed, so the node's Cleanup Consumable Attributes
	 * deletes it from the outputs. The registered name is ALWAYS Data-domain qualified: TryReadDataValue forces
	 * that domain regardless of the selector's own, and a bare name would delete the Elements twin instead.
	 * No-op without a context, without data, or when the node toggle is off.
	 */
	PCGEXCORE_API void RegisterDataDomainConsumable(FPCGExContext* InContext, const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector);
	PCGEXCORE_API void RegisterDataDomainConsumable(FPCGExContext* InContext, const UPCGData* InData, const FName& InName);
	PCGEXCORE_API void RegisterDataDomainConsumable(const TSharedPtr<FPointIO>& InIO, const FPCGAttributePropertyInputSelector& InSelector);
	PCGEXCORE_API void RegisterDataDomainConsumable(const TSharedPtr<FPointIO>& InIO, const FName& InName);

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

	/** Canonical read slot for a @Data attribute: first entry when the domain has items, else the default slot. */
	PCGEXCORE_API PCGMetadataEntryKey GetDataValueKey(const FPCGMetadataAttributeBase* Attribute);

	/** True when a value can be read from Attribute at Key (on 5.7 this reduces to Attribute existing). */
	PCGEXCORE_API bool HasPropertyCopyableValue(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key);

	/** Single-value attribute copy (data domain): reads SourceAttr at SourceKey and writes it to TargetKey. Source and target must share the same type. */
	PCGEXCORE_API bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, PCGMetadataEntryKey TargetKey);

	/** Multi-target variant: reads the source value once and writes it to every provided target key. */
	PCGEXCORE_API bool PropertyCopyAttribute(
		const FPCGMetadataAttributeBase* SourceAttr, PCGMetadataEntryKey SourceKey,
		FPCGMetadataAttributeBase* TargetAttr, TArrayView<const PCGMetadataEntryKey> TargetKeys);

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
