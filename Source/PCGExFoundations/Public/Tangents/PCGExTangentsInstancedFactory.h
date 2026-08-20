// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExDataHelpers.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"

#include "PCGExTangentsInstancedFactory.generated.h"

namespace PCGExData
{
	class FPointIO;
}

UENUM()
enum class EPCGExTangentSource : uint8
{
	None      = 0 UMETA(DisplayName = "No Tangents", Tooltip="No tangents"),
	Attribute = 1 UMETA(DisplayName = "Attribute", Tooltip="Tangents are read from attributes"),
	InPlace   = 2 UMETA(DisplayName = "In-place", Tooltip="Tangents are calculated in-place using a custom module")
};

class FPCGExTangentsOperation : public FPCGExOperation
{
public:
	bool bClosedLoop = false;

	virtual bool PrepareForData(FPCGExContext* InContext)
	{
		return true;
	}

	virtual void ProcessFirstPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();

		const FVector A = InTransforms[0].GetLocation();
		const FVector B = InTransforms[1].GetLocation();
		const FVector Dir = (B - A); //.GetSafeNormal() * (FVector::Dist(A, B));
		OutArrive = Dir * ArriveScale;
		OutLeave = Dir * LeaveScale;
	}

	virtual void ProcessLastPoint(const UPCGBasePointData* InPointData, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
	{
		const TConstPCGValueRange<FTransform> InTransforms = InPointData->GetConstTransformValueRange();
		const int32 LastIndex = InPointData->GetNumPoints() - 1;

		const FVector A = InTransforms[LastIndex].GetLocation();
		const FVector B = InTransforms[LastIndex - 1].GetLocation();
		const FVector Dir = (A - B); //.GetSafeNormal() * (FVector::Dist(A, B));
		OutArrive = Dir * ArriveScale;
		OutLeave = Dir * LeaveScale;
	}

	virtual void ProcessPoint(const UPCGBasePointData* InPointData, const int32 Index, const int32 NextIndex, const int32 PrevIndex, const FVector& ArriveScale, FVector& OutArrive, const FVector& LeaveScale, FVector& OutLeave) const
	{
	}
};

/**
 * 
 */
UCLASS(Abstract, meta=(PCGExNodeLibraryDoc="paths/common-settings/tangents/tangents-base"))
class PCGEXFOUNDATIONS_API UPCGExTangentsInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	bool bClosedLoop = false;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override
	{
		Super::CopySettingsFrom(Other);
		if (const UPCGExTangentsInstancedFactory* TypedOther = Cast<UPCGExTangentsInstancedFactory>(Other))
		{
			bClosedLoop = TypedOther->bClosedLoop;
		}
	}

	virtual TSharedPtr<FPCGExTangentsOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);
};

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExTangentsScalingDetails
{
	GENERATED_BODY()

	FPCGExTangentsScalingDetails() = default;

	/** Where the arrive tangent scale comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExInputValueType ArriveScaleInput = EPCGExInputValueType::Constant;

	/** Attribute to read arrive tangent scale from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Arrive Scale (Attr)", EditCondition="ArriveScaleInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector ArriveScaleAttribute;

	/** Scale multiplier for arrive tangents. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Arrive Scale", EditCondition="ArriveScaleInput == EPCGExInputValueType::Constant", EditConditionHides))
	double ArriveScaleConstant = 1;

	PCGEX_SETTING_VALUE_DECL(ArriveScale, FVector)

	/** Where the leave tangent scale comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExInputValueType LeaveScaleInput = EPCGExInputValueType::Constant;

	/** Attribute to read leave tangent scale from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Leave Scale (Attr)", EditCondition="LeaveScaleInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector LeaveScaleAttribute;

	/** Scale multiplier for leave tangents. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Leave Scale", EditCondition="LeaveScaleInput == EPCGExInputValueType::Constant", EditConditionHides))
	double LeaveScaleConstant = 1;

	PCGEX_SETTING_VALUE_DECL(LeaveScale, FVector)
};

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExTangentsDetails
{
	GENERATED_BODY()

	FPCGExTangentsDetails() = default;

	/** Where tangent data comes from. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable))
	EPCGExTangentSource Source = EPCGExTangentSource::Attribute;

	/** Attribute containing arrive tangent vectors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Source == EPCGExTangentSource::Attribute", EditConditionHides))
	FName ArriveTangentAttribute = "ArriveTangent";

	/** Attribute containing leave tangent vectors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Source == EPCGExTangentSource::Attribute", EditConditionHides))
	FName LeaveTangentAttribute = "LeaveTangent";


	/** Factory used to calculate tangents in-place. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, EditCondition = "Source == EPCGExTangentSource::InPlace", EditConditionHides, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> Tangents;

	/** Optional module for the start point specifically */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, DisplayName=" ├─ Start Override (Opt.)", EditCondition = "Source == EPCGExTangentSource::InPlace", EditConditionHides, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> StartTangents;

	/** Optional module for the end point specifically */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta=(PCG_Overridable, DisplayName=" └─ End Override (Opt.)", EditCondition = "Source == EPCGExTangentSource::InPlace", EditConditionHides, NoResetToDefault))
	TObjectPtr<UPCGExTangentsInstancedFactory> EndTangents;

	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable))
	FPCGExTangentsScalingDetails Scaling;


	/**  */
	UPROPERTY(meta = (PCG_NotOverridable))
	bool bDeprecationApplied = false;

#if WITH_EDITOR
	void ApplyDeprecation(bool bUseAttribute, FName InArriveAttributeName, FName InLeaveAttributeName);
#endif

	bool Init(FPCGExContext* InContext, const FPCGExTangentsDetails& InDetails);
};

namespace PCGExTangents
{
	const FName SourceOverridesTangents = TEXT("Overrides : Tangents");
	const FName SourceOverridesTangentsStart = TEXT("Overrides : Start Tangents");
	const FName SourceOverridesTangentsEnd = TEXT("Overrides : End Tangents");

	class PCGEXFOUNDATIONS_API FTangentsHandler : public TSharedFromThis<FTangentsHandler>
	{
	protected:
		bool bClosedLoop = false;
		EPCGExTangentSource Mode = EPCGExTangentSource::Attribute;
		int32 LastIndex = -1;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> StartScaleReader;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> EndScaleReader;

		TSharedPtr<FPCGExTangentsOperation> Tangents;
		TSharedPtr<FPCGExTangentsOperation> StartTangents;
		TSharedPtr<FPCGExTangentsOperation> EndTangents;

		TSharedPtr<PCGExData::TBuffer<FVector>> ArriveReader;
		TSharedPtr<PCGExData::TBuffer<FVector>> LeaveReader;

		const UPCGBasePointData* PointData = nullptr;

	public:
		explicit FTangentsHandler(const bool InClosedLoop)
			: bClosedLoop(InClosedLoop)
		{
		}

		virtual ~FTangentsHandler() = default;

		FORCEINLINE bool IsEnabled() const
		{
			return Mode != EPCGExTangentSource::None;
		}

		bool Init(FPCGExContext* InContext, const FPCGExTangentsDetails& InDetails, const TSharedPtr<PCGExData::FFacade>& InDataFacade);

		void GetPointTangents(const int32 Index, FVector& OutArrive, FVector& OutLeave) const;
		void GetSegmentTangents(const int32 Index, FVector& OutStartTangent, FVector& OutEndTangent) const;

	protected:
		void GetArriveTangent(const int32 Index, FVector& OutDir, const FVector& InScale) const;
		void GetLeaveTangent(const int32 Index, FVector& OutDir, const FVector& InScale) const;
	};
}
