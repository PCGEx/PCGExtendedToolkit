// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"

#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExTexCommon.h"
#include "Core/PCGExTexParamFactoryProvider.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGTextureData.h"


#include "PCGExSampleTexture.generated.h"


namespace PCGExTexture
{
	class FLookup;
}

class UPCGExPointFilterFactoryData;

/**
 * Use PCGExSampling to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="sampling/textures/sample-texture"))
class UPCGExSampleTextureSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExSampleTextureSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SampleTexture, "Sample : Texture", "Sample texture data using UV coordinates.");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Sampling); }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters, false)
	//~End UPCGExPointsProcessorSettings

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector UVSource;

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasSuccesses = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasSuccesses"))
	FString HasSuccessesTag = TEXT("HasSuccesses");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoSuccesses = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoSuccesses"))
	FString HasNoSuccessesTag = TEXT("HasNoSuccesses");

	//

	/** If enabled, mark filtered out points as "failed". Otherwise, just skip the processing altogether. Only uncheck this if you want to ensure existing attribute values are preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bProcessFilteredOutAsFails = true;

	/** If enabled, points that failed to sample anything will be pruned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bPruneFailedSamples = false;

	/** Suppress warnings about duplicate sample attribute names across texture params. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietDuplicateSampleNamesWarning = false;
};

struct FPCGExSampleTextureContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleTextureElement;
	TArray<TObjectPtr<const UPCGExTexParamFactoryData>> TexParamsFactories;
	TSharedPtr<PCGExTexture::FLookup> TextureMap;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleTextureElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleTexture)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleTexture
{
	class FSampler : public TSharedFromThis<FSampler>
	{
	protected:
		FPCGExTextureParamConfig Config;
		TSharedPtr<PCGExTexture::FLookup> TextureMap;
		TSharedPtr<PCGExData::TAttributeBroadcaster<FString>> IDGetter;

		bool bValid = false;

	public:
		virtual ~FSampler() = default;

		explicit FSampler(const FPCGExTextureParamConfig& InConfig, const TSharedPtr<PCGExTexture::FLookup>& InTextureMap, const TSharedRef<PCGExData::FFacade>& InDataFacade);

		bool IsValid() const { return bValid; }
		virtual bool Sample(const PCGExData::FConstPoint& Point, const FVector2D& UV) const = 0;
	};

	template <typename T>
	class TSampler : public FSampler
	{
		TSharedPtr<PCGExData::TBuffer<T>> Buffer;

	public:
		explicit TSampler(const FPCGExTextureParamConfig& InConfig, const TSharedPtr<PCGExTexture::FLookup>& InTextureMap, const TSharedRef<PCGExData::FFacade>& InDataFacade)
			: FSampler(InConfig, InTextureMap, InDataFacade)
		{
			if (!IsValid()) { return; }
			Buffer = InDataFacade->GetWritable<T>(InConfig.SampleAttributeName, T{}, true, PCGExData::EBufferInit::Inherit);
		}

		virtual bool Sample(const PCGExData::FConstPoint& Point, const FVector2D& UV) const override
		{
			FVector4 SampledValue = FVector4::Zero();
			float SampledDensity = 1;

			if (const UPCGTexture2DSingleBaseData* Tex = TextureMap->TryGetTextureData(IDGetter->FetchSingle(Point, TEXT(""))); !Tex || !Tex->SamplePointLocal(UV, SampledValue, SampledDensity))
			{
				return false;
			}

			SampledValue *= Config.Scale;

			T V = Buffer->GetValue(Point.Index);

			if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
			{
				for (const int32 C : Config.OutChannels) { V = SampledValue[C]; }
				Buffer->SetValue(Point.Index, V);
				return true;
			}
			else if constexpr (std::is_same_v<T, FVector2D> || std::is_same_v<T, FVector> || std::is_same_v<T, FVector4>)
			{
				for (int i = 0; i < Config.OutChannels.Num(); i++) { V[i] = SampledValue[Config.OutChannels[i]]; }
				Buffer->SetValue(Point.Index, V);
				return true;
			}
			else
			{
				return false;
			}
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleTextureContext, UPCGExSampleTextureSettings>
	{
		TArray<int8> SamplingMask;

		TSharedPtr<PCGExTexture::FLookup> TexParamLookup;
		TSharedPtr<PCGExData::TBuffer<FVector2D>> UVGetter;

		int8 bAnySuccess = 0;
		UWorld* World = nullptr;

		TArray<TSharedRef<FSampler>> Samplers;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
