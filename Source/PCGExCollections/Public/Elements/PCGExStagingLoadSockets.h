// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Details/PCGExSocketOutputDetails.h"

#include "PCGExStagingLoadSockets.generated.h"

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(Keywords = "stage prepare spawn proxy", PCGExNodeLibraryDoc="staging/staging-load-sockets"))
class UPCGExSocketStagingSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SocketStaging, "Staging : Load Sockets", "Create points from staged data sockets.");
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Sampler; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points get staged.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSocketOutputDetails OutputSocketDetails;
};

struct FPCGExSocketStagingContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSocketStagingElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;
	
	FPCGExSocketOutputDetails OutputSocketDetails;
	TSharedPtr<PCGExData::FPointIOCollection> SocketsCollection;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSocketStagingElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SocketStaging)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSocketStaging
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSocketStagingContext, UPCGExSocketStagingSettings>
	{
		TSharedPtr<PCGExCollections::FSocketHelper> SocketHelper;
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

	public:
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
	};
}
