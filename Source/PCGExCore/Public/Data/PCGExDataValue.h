// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "UObject/Object.h"

class UPCGData;
struct FPCGTaggedData;
struct FPCGAttributePropertyInputSelector;
enum class EPCGMetadataTypes : uint8;

namespace PCGExData
{
	class PCGEXCORE_API IDataValue : public TSharedFromThis<IDataValue>
	{
	protected:
		EPCGMetadataTypes Type = EPCGMetadataTypes::Unknown;

	public:
		virtual ~IDataValue() = default;
		FORCEINLINE EPCGMetadataTypes GetTypeId() const
		{
			return Type;
		}

		IDataValue() = default;

		virtual FString Flatten(const FString& LeftSide) = 0;

		virtual bool IsNumeric() const = 0;
		virtual bool IsText() const = 0;

		virtual double AsDouble() = 0;
		virtual FString AsString() = 0;

		bool SameValue(const TSharedPtr<IDataValue>& Other);

		template <typename T>
		T GetValue();

		virtual void GetVoid(void* OutValue) const = 0;

	protected:
		TOptional<double> CachedDouble;
		TOptional<FString> CachedString;
	};

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template _TYPE IDataValue::GetValue<_TYPE>();
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)

#undef PCGEX_TPL

#pragma endregion

	template <typename T>
	class PCGEXCORE_API TDataValue : public IDataValue
	{
	public:
		T Value;

		explicit TDataValue(const T& InValue);


		virtual FString Flatten(const FString& LeftSide) override;

		virtual bool IsNumeric() const override;
		virtual bool IsText() const override;

		virtual double AsDouble() override;
		virtual FString AsString() override;

		virtual void GetVoid(void* OutValue) const override;
	};

#pragma region externalization

#define PCGEX_TPL(_TYPE, _NAME, ...)\
extern template class TDataValue<_TYPE>;

	//PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL) // omitted: extern-template of dllexport class = C4910 (MSVC)

#undef PCGEX_TPL

#pragma endregion

	PCGEXCORE_API TSharedPtr<IDataValue> TryGetValueFromTag(const FString& InTag, FString& OutLeftSide);

	PCGEXCORE_API TSharedPtr<IDataValue> TryGetValueFromData(const UPCGData* InData, const FPCGAttributePropertyInputSelector& InSelector);

	PCGEXCORE_API TSharedPtr<IDataValue> TryGetValueFromData(const UPCGData* InData, const FName& InName);

	// Reads a data-level value from tagged data: matches the selector name against "key:value" tags first, then falls back to a data-domain attribute.
	PCGEXCORE_API TSharedPtr<IDataValue> TryGetValueFromData(const FPCGTaggedData& InTaggedData, const FPCGAttributePropertyInputSelector& InSelector);
}
