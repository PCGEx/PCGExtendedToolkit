// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExAttributesDetails.generated.h"

class FPCGMetadataAttributeBase;
struct FPCGExContext;

#pragma region Legacy

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExInputConfig
{
	GENERATED_BODY()

	// Legacy horror

	FPCGExInputConfig() = default;
	explicit FPCGExInputConfig(const FPCGAttributePropertyInputSelector& InSelector);
	explicit FPCGExInputConfig(const FName InName);

	// Compiler-generated copy/move: Attribute/UnderlyingType are transient caches rebound by Validate(), so never read them before calling it.
	FPCGExInputConfig(const FPCGExInputConfig&) = default;
	FPCGExInputConfig& operator=(const FPCGExInputConfig&) = default;

	virtual ~FPCGExInputConfig() = default;
	UPROPERTY(VisibleAnywhere, Category=Settings, meta=(HideInDetailPanel, EditCondition="false", EditConditionHides))
	FString TitlePropertyName;

	/** Attribute or $Property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Attribute", DisplayPriority=0))
	FPCGAttributePropertyInputSelector Selector;

	FPCGMetadataAttributeBase* Attribute = nullptr;
	int16 UnderlyingType = 255; //static_cast<int16>(EPCGMetadataTypes::Unknown);

	EPCGAttributePropertySelection GetSelection() const
	{
		return Selector.GetSelection();
	}

	FName GetName() const
	{
		return Selector.GetName();
	}
#if WITH_EDITOR
	virtual FString GetDisplayName() const;
	void UpdateUserFacingInfos();
#endif
	/**
	 * Bind & cache the current selector for a given point data
	 * @param InData 
	 * @return 
	 */
	virtual bool Validate(const UPCGData* InData);

	FString ToString() const
	{
		return GetName().ToString();
	}
};

#pragma endregion

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExAttributeSourceToTargetDetails
{
	GENERATED_BODY()

	FPCGExAttributeSourceToTargetDetails() = default;

	explicit FPCGExAttributeSourceToTargetDetails(const FName DefaultName, const bool InOutputToDifferentName = false, const FName DefaultTargetName = NAME_None)
		: Source(DefaultName)
		  , bOutputToDifferentName(InOutputToDifferentName)
		  , Target(DefaultTargetName)
	{
	}

	/** Attribute to read on input */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName Source = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOutputToDifferentName = false;

	/** Attribute to write on output, if different from input */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOutputToDifferentName"))
	FName Target = NAME_None;

	bool WantsRemappedOutput() const
	{
		return (bOutputToDifferentName && Source != GetOutputName());
	}

	bool ValidateNames(FPCGExContext* InContext) const;
	bool ValidateNamesOrProperties(FPCGExContext* InContext) const;

	FName GetOutputName() const;

	FPCGAttributePropertyInputSelector GetSourceSelector() const;
	FPCGAttributePropertyInputSelector GetTargetSelector() const;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExAttributeSourceToTargetList
{
	GENERATED_BODY()

	FPCGExAttributeSourceToTargetList() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, TitleProperty="{Source}"))
	TArray<FPCGExAttributeSourceToTargetDetails> Attributes;

	bool IsEmpty() const
	{
		return Attributes.IsEmpty();
	}

	int32 Num() const
	{
		return Attributes.Num();
	}

	bool ValidateNames(FPCGExContext* InContext) const;
};

#if WITH_EDITOR
namespace PCGExAttributeMigration
{
	/** Editor-only deprecation aid: append one source->target mapping per legacy selector. Source is set to
	 *  the selector's canonical text so it round-trips through FPCGExAttributeSourceToTargetDetails::GetSourceSelector()
	 *  (which feeds Source back into FPCGAttributePropertyInputSelector::Update) and resolves to the same
	 *  attribute / $property / @domain / .sub the selector did. No remap is set, so the promoted output name
	 *  keeps the pre-migration (source-resolved) behavior. Shared by every FPCGExAttributeToTagDetails user. */
	PCGEXCORE_API void AppendMappingsFromSelectors(
		TConstArrayView<FPCGAttributePropertyInputSelector> InSelectors,
		TArray<FPCGExAttributeSourceToTargetDetails>& OutMappings);
}
#endif
