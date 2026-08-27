// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExPathProcessor.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExAttachmentRules.h"
#include "Elements/PCGCreateSpline.h"
#include "Paths/PCGExPathsCommon.h"
#include "Tangents/PCGExTangentsInstancedFactory.h"

#include "PCGExCreateSpline.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="paths/output/create-spline"))
class UPCGExCreateSplineSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(CreateSpline, "Create Spline", "Create splines from input points.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscAdd);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return FName(TEXT("Splines"));
	}

	//~End UPCGExPointsProcessorSettings

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGCreateSplineMode Mode = EPCGCreateSplineMode::CreateDataOnly;

	/** Default spline point type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSplinePointType DefaultPointType = EPCGExSplinePointType::Linear;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bApplyCustomPointType = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bApplyCustomPointType"))
	FName PointTypeAttribute = "PointType";

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bApplyCustomTangents_DEPRECATED = false;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ArriveTangentAttribute_DEPRECATED = "ArriveTangent";

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName LeaveTangentAttribute_DEPRECATED = "LeaveTangent";

#pragma endregion

	/** Per-point tangent settings. Can't be set if the spline is linear. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="GetApplyTangents()"))
	FPCGExTangentsDetails Tangents;

	/** Actor the splines are created on, and the frame their control points are placed in. Required in every mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable), AdvancedDisplay)
	TSoftObjectPtr<AActor> TargetActor;

	/** Specify a list of functions to be called on the target actor after spline creation. Functions need to be parameter-less and with "CallInEditor" flag enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay)
	TArray<FName> PostProcessFunctionNames;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay)
	FPCGExAttachmentRules AttachmentRules;

	UFUNCTION()
	bool GetApplyTangents() const
	{
		return (!bApplyCustomPointType && DefaultPointType == EPCGExSplinePointType::CurveCustomTangent);
	}

	virtual bool ShouldCache() const override;
};

struct FPCGExCreateSplineContext final : FPCGExPathProcessorContext
{
	friend class FPCGExCreateSplineElement;
	FPCGExTangentsDetails Tangents;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExCreateSplineElement final : public FPCGExPathProcessorElement
{
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CreateSpline)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;
};

namespace PCGExCreateSpline
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExCreateSplineContext, UPCGExCreateSplineSettings>
	{
		int32 LastIndex = 0;
		bool bClosedLoop = false;
		bool bApplyTangents = false;
		float MaxIndex = 0.0;

		int8 HasAValidEntry = false;

		TSharedPtr<PCGExTangents::FTangentsHandler> TangentsHandler;
		TSharedPtr<PCGExData::TBuffer<int32>> CustomPointType;

		TArray<PCGMetadataEntryKey> SplineEntryKeys;
		TArray<FSplinePoint> SplinePoints;
		FVector PositionOffset = FVector::ZeroVector;

	public:
		TObjectPtr<UPCGSplineData> SplineData = nullptr;
		TObjectPtr<AActor> SplineActor = nullptr;

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

		virtual void Output() override;
		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExPointsMT::TBatch<FProcessor>
	{
		AActor* TargetActor = nullptr;
		EPCGCreateSplineMode Mode = EPCGCreateSplineMode::CreateDataOnly;

	public:
		explicit FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection)
			: TBatch(InContext, InPointsCollection)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(CreateSpline)

			Mode = Settings->Mode;
			TargetActor = Settings->TargetActor.Get() ? Settings->TargetActor.Get() : Context->GetTargetActor(nullptr);
			if (!TargetActor)
			{
				PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Invalid target actor. Ensure TargetActor member is initialized when creating SpatialData."));
				return;
			}

			InContext->AddNotifyActor(TargetActor);
		}

		virtual bool PrepareSingle(const TSharedRef<PCGExPointsMT::IProcessor>& InProcessor) override;
	};
}
