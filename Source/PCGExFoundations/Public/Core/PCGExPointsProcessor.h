// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGPin.h"

#include "PCGExCoreSettingsCache.h" // Boilerplate
#include "PCGExPointsMT.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Core/PCGExFilterTypeSets.h" // Factory acceptance sets used by PCGEX_NODE_POINT_FILTER

#include "PCGExPointsProcessor.generated.h"

#define PCGEX_ELEMENT_BATCH_POINT_DECL virtual TSharedPtr<PCGExPointsMT::IBatch> CreatePointBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const override;
#define PCGEX_ELEMENT_BATCH_POINT_IMPL(_CLASS) TSharedPtr<PCGExPointsMT::IBatch> FPCGEx##_CLASS##Context::CreatePointBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const{ \
return MakeShared<PCGExPointsMT::TBatch<PCGEx##_CLASS::FProcessor>>(const_cast<FPCGEx##_CLASS##Context*>(this), InData); }
#define PCGEX_ELEMENT_BATCH_POINT_IMPL_ADV(_CLASS) TSharedPtr<PCGExPointsMT::IBatch> FPCGEx##_CLASS##Context::CreatePointBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const{ \
return MakeShared<PCGEx##_CLASS::FBatch>(const_cast<FPCGEx##_CLASS##Context*>(this), InData); }

class UPCGExPointFilterFactoryData;

namespace PCGExPointsMT
{
	class IProcessor;
	class IBatch;
}

namespace PCGExData
{
	class FPointIO;
	class FPointIOCollection;

	class IBuffer;

	template <typename T>
	class TBuffer;
}

class UPCGExInstancedFactory;

namespace PCGExFactories
{
	enum class EType : uint8;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

struct FPCGExPointsProcessorContext;
class FPCGExPointsProcessorElement;

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class PCGEXFOUNDATIONS_API UPCGExPointsProcessorSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend struct FPCGExPointsProcessorContext;
	friend class FPCGExPointsProcessorElement;

public:
	//~Begin UPCGSettings	
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::PointOps;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual bool OnlyPassThroughOneEdgeWhenDisabled() const override
	{
		return false;
	}

	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual bool IsInputless() const
	{
		return false;
	}

	virtual FName GetMainInputPin() const
	{
		return PCGPinConstants::DefaultInputLabel;
	}

	virtual FName GetMainOutputPin() const
	{
		return PCGPinConstants::DefaultOutputLabel;
	}

	virtual bool GetMainAcceptMultipleData() const
	{
		return true;
	}

	virtual bool GetIsMainTransactional() const
	{
		return false;
	}

	virtual PCGExData::EIOInit GetMainOutputInitMode() const;

	virtual FName GetPointFilterPin() const
	{
		return NAME_None;
	}

	virtual FString GetPointFilterTooltip() const
	{
		return TEXT("Filters");
	}

	virtual TSet<FPCGDataTypeBaseId> GetPointFilterTypes() const;

	virtual bool RequiresPointFilters() const
	{
		return false;
	}

	bool SupportsPointFilters() const
	{
		return !GetPointFilterPin().IsNone();
	}

	//~End UPCGExPointsProcessorSettings
};

struct PCGEXFOUNDATIONS_API FPCGExPointsProcessorContext : FPCGExContext
{
	friend class FPCGExPointsProcessorElement;

	using FBatchProcessingValidateEntry = std::function<bool(const TSharedPtr<PCGExData::FPointIO>&)>;
	using FBatchProcessingInitPointBatch = std::function<void(const TSharedPtr<PCGExPointsMT::IBatch>&)>;

	virtual ~FPCGExPointsProcessorContext() override;

	TSharedPtr<PCGExData::FPointIOCollection> MainPoints;
	TSharedPtr<PCGExData::FPointIO> CurrentIO;

	virtual bool AdvancePointsIO(const bool bCleanupKeys = true);

	int32 InitialMainPointsNum = 0;


#pragma region Filtering

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> FilterFactories;

#pragma endregion

#pragma region Batching

	bool bBatchProcessingEnabled = false;
	bool ProcessPointsBatch(const PCGExCommon::ContextState NextStateId);

	TSharedPtr<PCGExPointsMT::IBatch> MainBatch;
	TMap<PCGExData::FPointIO*, TSharedRef<PCGExPointsMT::IProcessor>> SubProcessorMap;

	bool StartBatchProcessingPoints(FBatchProcessingValidateEntry&& ValidateEntry, FBatchProcessingInitPointBatch&& InitBatch);

	virtual void BatchProcessing_InitialProcessingDone();
	virtual void BatchProcessing_WorkComplete();
	virtual void BatchProcessing_WritingDone();

#pragma endregion

protected:
	int32 CurrentPointIOIndex = -1;

	virtual TSharedPtr<PCGExPointsMT::IBatch> CreatePointBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const PCGEX_NOT_IMPLEMENTED_RET(CreatePointBatchInstance, nullptr);
};

class PCGEXFOUNDATIONS_API FPCGExPointsProcessorElement : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PointsProcessor)
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
