// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExVtxPropertyFactoryProvider.h"
#include "Factories/PCGExFactoryProvider.h"

#include "PCGExVtxPropertyAmplitude.generated.h"

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

class UPCGExPointFilterFactoryData;

UENUM()
enum class EPCGExVtxAmplitudeMode : uint8
{
	Length     = 0 UMETA(DisplayName = "Length", ToolTip="Uniform fit"),
	Individual = 1 UMETA(DisplayName = "Individual", ToolTip="Component-wise amplitude"),
};

UENUM()
enum class EPCGExVtxAmplitudeUpMode : uint8
{
	Average  = 0 UMETA(DisplayName = "Average Direction", ToolTip="Average direction to neighbors"),
	UpVector = 1 UMETA(DisplayName = "Custom Up Vector", ToolTip="Custom Up Vector"),
};

UENUM()
enum class EPCGExVtxAmplitudeSignOutput : uint8
{
	Raw            = 0 UMETA(DisplayName = "Raw", ToolTip="Raw dot product"),
	Size           = 1 UMETA(DisplayName = "Size", ToolTip="Dot product * edge size"),
	NormalizedSize = 2 UMETA(DisplayName = "Size (Normalized)", ToolTip="Dot product * edge size normalized"),
	Sign           = 3 UMETA(DisplayName = "Sign", ToolTip="Sign (0,1,-1)"),
};

USTRUCT(BlueprintType)
struct FPCGExAmplitudeConfig
{
	GENERATED_BODY()

	FPCGExAmplitudeConfig();

	/** Write the minimum amplitude to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteMinAmplitude = false;

	/** Name of the attribute to write minimum amplitude to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Min", PCG_Overridable, EditCondition="bWriteMinAmplitude"))
	FName MinAmplitudeAttributeName = "MinAmplitude";

	/** Use absolute values for minimum amplitude. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Absolute", EditCondition="bWriteMinAmplitude && MinMode == EPCGExVtxAmplitudeMode::Individual", EditConditionHides, HideEditConditionToggle))
	bool bAbsoluteMin = true;

	/** How the minimum amplitude is computed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Mode", EditCondition="bWriteMinAmplitude", EditConditionHides, HideEditConditionToggle))
	EPCGExVtxAmplitudeMode MinMode = EPCGExVtxAmplitudeMode::Length;

	/** Write the maximum amplitude to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteMaxAmplitude = false;

	/** Name of the attribute to write maximum amplitude to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Max", PCG_Overridable, EditCondition="bWriteMaxAmplitude"))
	FName MaxAmplitudeAttributeName = "MaxAmplitude";

	/** Use absolute values for maximum amplitude. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Absolute", EditCondition="bWriteMaxAmplitude && MaxMode == EPCGExVtxAmplitudeMode::Individual", EditConditionHides, HideEditConditionToggle))
	bool bAbsoluteMax = true;

	/** How the maximum amplitude is computed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Mode", EditCondition="bWriteMaxAmplitude", EditConditionHides, HideEditConditionToggle))
	EPCGExVtxAmplitudeMode MaxMode = EPCGExVtxAmplitudeMode::Length;

	/** Write the amplitude range to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAmplitudeRange = false;

	/** Name of the attribute to write amplitude range to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Range", PCG_Overridable, EditCondition="bWriteAmplitudeRange"))
	FName AmplitudeRangeAttributeName = "AmplitudeRange";

	/** Use absolute values for amplitude range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Absolute", EditCondition="bWriteAmplitudeRange && RangeMode == EPCGExVtxAmplitudeMode::Individual", EditConditionHides, HideEditConditionToggle))
	bool bAbsoluteRange = true;

	/** How the amplitude range is computed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Mode", EditCondition="bWriteAmplitudeRange", EditConditionHides, HideEditConditionToggle))
	EPCGExVtxAmplitudeMode RangeMode = EPCGExVtxAmplitudeMode::Length;

	/** Write the amplitude sign to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAmplitudeSign = false;

	/** Name of the attribute to write amplitude sign to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName="Sign", PCG_Overridable, EditCondition="bWriteAmplitudeSign"))
	FName AmplitudeSignAttributeName = "AmplitudeSign";

	/** How the sign value is computed and output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Mode", EditCondition="bWriteAmplitudeSign", EditConditionHides, HideEditConditionToggle))
	EPCGExVtxAmplitudeSignOutput SignOutputMode = EPCGExVtxAmplitudeSignOutput::Size;

	/** Use absolute value for sign calculation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Absolute", EditCondition="bWriteAmplitudeSign", EditConditionHides, HideEditConditionToggle))
	bool bAbsoluteSign = true;

	/** Up vector source.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Up Mode", EditCondition="bWriteAmplitudeSign", EditConditionHides, HideEditConditionToggle))
	EPCGExVtxAmplitudeUpMode UpMode = EPCGExVtxAmplitudeUpMode::Average;

	/** Up vector source.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Up Input Type", EditCondition="bWriteAmplitudeSign && UpMode == EPCGExVtxAmplitudeUpMode::UpVector", EditConditionHides, HideEditConditionToggle))
	EPCGExInputValueType UpSelection = EPCGExInputValueType::Constant;

	/** Up vector to use for amplitude sign */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Up Vector (Attr)", EditCondition="bWriteAmplitudeSign && UpMode == EPCGExVtxAmplitudeUpMode::UpVector && UpSelection != EPCGExInputValueType::Constant", EditConditionHides, HideEditConditionToggle))
	FPCGAttributePropertyInputSelector UpSource;

	/** Up vector to use for amplitude sign */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Up Vector", EditCondition="bWriteAmplitudeSign && UpMode == EPCGExVtxAmplitudeUpMode::UpVector && UpSelection == EPCGExInputValueType::Constant", EditConditionHides, HideEditConditionToggle))
	FVector UpConstant = FVector::UpVector;

	bool Validate(FPCGExContext* InContext) const;
};

/**
 * 
 */
class FPCGExVtxPropertyAmplitude : public FPCGExVtxPropertyOperation
{
public:
	FPCGExAmplitudeConfig Config;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>* FilterFactories = nullptr;

	virtual bool PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual void ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP) override;

protected:
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> DirCache;

	TSharedPtr<PCGExData::TBuffer<double>> MinAmpLengthBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> MaxAmpLengthBuffer;
	TSharedPtr<PCGExData::TBuffer<double>> AmpRangeLengthBuffer;

	TSharedPtr<PCGExData::TBuffer<FVector>> MinAmpBuffer;
	TSharedPtr<PCGExData::TBuffer<FVector>> MaxAmpBuffer;
	TSharedPtr<PCGExData::TBuffer<FVector>> AmpRangeBuffer;

	TSharedPtr<PCGExData::TBuffer<double>> AmpSignBuffer;

	bool bUseSize = false;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExVtxPropertyAmplitudeFactory : public UPCGExVtxPropertyFactoryData
{
	GENERATED_BODY()

public:
	FPCGExAmplitudeConfig Config;
	virtual TSharedPtr<FPCGExVtxPropertyOperation> CreateOperation(FPCGExContext* InContext) const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|VtxProperty", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-vtx-properties/vtx-amplitude"))
class UPCGExVtxPropertyAmplitudeSettings : public UPCGExVtxPropertyProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(VtxAmplitude, "Vtx : Amplitude", "Amplitude of a vtx, based on neighboring connections.", FName(GetDisplayName()))
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Direction Settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAmplitudeConfig Config;
};
