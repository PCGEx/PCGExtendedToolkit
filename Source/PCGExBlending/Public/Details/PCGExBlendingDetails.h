// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExBlendingCommon.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Details/PCGExAttributesDetails.h"
#include "Metadata/PCGMetadataCommon.h"

#include "PCGExBlendingDetails.generated.h"

enum class EPCGPinStatus : uint8;
struct FPCGPinProperties;

namespace PCGExBlending
{
	PCGEXBLENDING_API void DeclareBlendOpsInputs(TArray<FPCGPinProperties>& PinProperties, const EPCGPinStatus InStatus, EPCGExBlendingInterface Interface = EPCGExBlendingInterface::Individual);
}

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExAttributeBlendToTargetDetails : public FPCGExAttributeSourceToTargetDetails
{
	GENERATED_BODY()

	FPCGExAttributeBlendToTargetDetails() = default;

	/** BlendMode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayAfter="Source"))
	EPCGExBlendingType Blending = EPCGExBlendingType::None;
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExPointPropertyBlendingOverrides
{
	GENERATED_BODY()

#pragma region Property overrides

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideDensity = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Density", EditCondition="bOverrideDensity"))
	EPCGExBlendingType DensityBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideBoundsMin = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="BoundsMin", EditCondition="bOverrideBoundsMin"))
	EPCGExBlendingType BoundsMinBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideBoundsMax = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="BoundsMax", EditCondition="bOverrideBoundsMax"))
	EPCGExBlendingType BoundsMaxBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideColor = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Color", EditCondition="bOverrideColor"))
	EPCGExBlendingType ColorBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverridePosition = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Position", EditCondition="bOverridePosition"))
	EPCGExBlendingType PositionBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideRotation = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Rotation", EditCondition="bOverrideRotation"))
	EPCGExBlendingType RotationBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideScale = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Scale", EditCondition="bOverrideScale"))
	EPCGExBlendingType ScaleBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideSteepness = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Steepness", EditCondition="bOverrideSteepness"))
	EPCGExBlendingType SteepnessBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	bool bOverrideSeed = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Seed", EditCondition="bOverrideSeed"))
	EPCGExBlendingType SeedBlending = EPCGExBlendingType::Average;

#pragma endregion

	bool HasAnyOverride() const
	{
		return bOverrideDensity || bOverrideBoundsMin || bOverrideBoundsMax ||
			bOverrideColor || bOverridePosition || bOverrideRotation ||
			bOverrideScale || bOverrideSteepness || bOverrideSeed;
	}
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExPropertiesBlendingDetails
{
	GENERATED_BODY()

	FPCGExPropertiesBlendingDetails() = default;
	explicit FPCGExPropertiesBlendingDetails(const EPCGExBlendingType InDefaultBlending);

	EPCGExBlendingType DefaultBlending = EPCGExBlendingType::Average;

#pragma region Property overrides

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Density", PCG_NotOverridable))
	EPCGExBlendingType DensityBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="BoundsMin", PCG_NotOverridable))
	EPCGExBlendingType BoundsMinBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="BoundsMax", PCG_NotOverridable))
	EPCGExBlendingType BoundsMaxBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Color", PCG_NotOverridable))
	EPCGExBlendingType ColorBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Position", PCG_NotOverridable))
	EPCGExBlendingType PositionBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Rotation", PCG_NotOverridable))
	EPCGExBlendingType RotationBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Scale", PCG_NotOverridable))
	EPCGExBlendingType ScaleBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Steepness", PCG_NotOverridable))
	EPCGExBlendingType SteepnessBlending = EPCGExBlendingType::None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Seed", PCG_NotOverridable))
	EPCGExBlendingType SeedBlending = EPCGExBlendingType::None;

#pragma endregion
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/blending-details-monolithic"))
struct PCGEXBLENDING_API FPCGExBlendingDetails
{
	GENERATED_BODY()

	FPCGExBlendingDetails() = default;

	explicit FPCGExBlendingDetails(const EPCGExBlendingType InDefaultBlending);
	explicit FPCGExBlendingDetails(const EPCGExBlendingType InDefaultBlending, const EPCGExBlendingType InPositionBlending);
	explicit FPCGExBlendingDetails(const FPCGExPropertiesBlendingDetails& InDetails);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAttributeFilter BlendingFilter = EPCGExAttributeFilter::All;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="BlendingFilter != EPCGExAttributeFilter::All", EditConditionHides))
	TArray<FName> FilteredAttributes;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExBlendingType DefaultBlending = EPCGExBlendingType::Average;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExPointPropertyBlendingOverrides PropertiesOverrides;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TMap<FName, EPCGExBlendingType> AttributesOverrides;

	FPCGExPropertiesBlendingDetails GetPropertiesBlendingDetails() const;

	/** Returns true if any blending is configured (default, property overrides, or attribute overrides) */
	bool HasAnyBlending() const
	{
		return DefaultBlending != EPCGExBlendingType::None ||
			PropertiesOverrides.HasAnyOverride() ||
			!AttributesOverrides.IsEmpty();
	}

	bool CanBlend(const FName AttributeName) const;
	void Filter(TArray<PCGExData::FAttributeIdentity>& Identities) const;

	bool GetBlendingParam(const FPCGAttributeIdentifier& InIdentifer, PCGExBlending::FBlendingParam& OutParam) const;
	void GetPointPropertyBlendingParams(TArray<PCGExBlending::FBlendingParam>& OutParams) const;

	// Create a list of attributes & property selector representing the data we want to blend
	// Takes the reference list from source, 
	void GetBlendingParams(const UPCGMetadata* SourceMetadata, UPCGMetadata* TargetMetadata, TArray<PCGExBlending::FBlendingParam>& OutParams, TArray<FPCGAttributeIdentifier>& OutAttributeIdentifiers, const bool bSkipProperties = false, const TSet<FName>* IgnoreAttributeSet = nullptr) const;

	void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TSet<FName>* IgnoredAttributes = nullptr) const;
};

namespace PCGExBlending
{
	PCGEXBLENDING_API void AssembleBlendingDetails(const FPCGExPropertiesBlendingDetails& PropertiesBlending, const TMap<FName, EPCGExBlendingType>& PerAttributeBlending, const TSharedRef<PCGExData::FPointIO>& SourceIO, FPCGExBlendingDetails& OutDetails, TSet<FName>& OutMissingAttributes);

	PCGEXBLENDING_API void AssembleBlendingDetails(const FPCGExPropertiesBlendingDetails& PropertiesBlending, const TMap<FName, EPCGExBlendingType>& PerAttributeBlending, const TArray<TSharedRef<PCGExData::FFacade>>& InSources, FPCGExBlendingDetails& OutDetails, TSet<FName>& OutMissingAttributes);

	PCGEXBLENDING_API void AssembleBlendingDetails(const EPCGExBlendingType& DefaultBlending, const TArray<FName>& Attributes, const TSharedRef<PCGExData::FPointIO>& SourceIO, FPCGExBlendingDetails& OutDetails, TSet<FName>& OutMissingAttributes);

	PCGEXBLENDING_API void AssembleBlendingDetails(const EPCGExBlendingType& DefaultBlending, const TArray<FName>& Attributes, const TArray<TSharedRef<PCGExData::FFacade>>& InSources, FPCGExBlendingDetails& OutDetails, TSet<FName>& OutMissingAttributes);

	PCGEXBLENDING_API void GetFilteredIdentities(const UPCGMetadata* InMetadata, TArray<PCGExData::FAttributeIdentity>& OutIdentities, const FPCGExBlendingDetails* InBlendingDetails = nullptr, const FPCGExCarryOverDetails* InCarryOverDetails = nullptr, const TSet<FName>* IgnoreAttributeSet = nullptr);
}
