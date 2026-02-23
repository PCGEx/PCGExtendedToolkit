// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Core/PCGExClusterFilter.h"

#include "PCGExPointStates.generated.h"

USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExStateConfigBase
{
	GENERATED_BODY()

	FPCGExStateConfigBase()
	{
	}

	FName StateName = NAME_None;
	int32 StateId = 0;

	/** Flags */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bOnTestPass = true;

	/** Operations executed on the flag if all filters pass */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOnTestPass"))
	FPCGExBitmaskWithOperation PassStateFlags;

	/** Flags */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bOnTestFail = true;

	/** Operations executed on the flag if any filters fail */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOnTestFail"))
	FPCGExBitmaskWithOperation FailStateFlags;

	void Init()
	{
	}

#if WITH_EDITOR
	void ApplyDeprecation()
	{
		PassStateFlags.ApplyDeprecation();
		FailStateFlags.ApplyDeprecation();
	}
#endif
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | State : Point"))
struct FPCGExDataTypeInfoPointState : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXFILTERS_API)
};

/**
 * Factory for point-level state definitions. A state is a named, filter-driven condition
 * that applies bitmask flag operations (pass/fail) to each point. Multiple states are
 * evaluated through a FStateManager, which accumulates their flag mutations into a shared
 * int64 flags array.
 *
 * Inherits from ClusterFilterFactory (the widest type) so states can accept both
 * point and cluster filters in their internal filter stack.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXFILTERS_API UPCGExPointStateFactoryData : public UPCGExClusterFilterFactoryData
{
	// Inherits from ClusterFilterFactory because states are filters so we want to inherit from the widest type
	// This is a bit inelegant but greatly simplifies maintenance
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoPointState)

	UPROPERTY()
	FPCGExStateConfigBase BaseConfig;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> FilterFactories;

	virtual bool GetRequiresFilters() const { return true; }
	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::PointState; }
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual void BeginDestroy() override;
};

namespace PCGExPointStates
{
	namespace Labels
	{
		const FName OutputOnPassBitmaskLabel = TEXT("BitmaskPass");
		const FName OutputOnFailBitmaskLabel = TEXT("BitmaskFail");

		const FName OutputStateLabel = TEXT("State");
		const FName SourceStatesLabel = TEXT("States");
	}

	/**
	 * A single named state. Wraps an internal FManager with its own filter stack.
	 * Test() delegates to the internal manager. ProcessFlags() applies bitmask ops
	 * to the shared flags based on pass/fail.
	 */
	class PCGEXFILTERS_API FState final : public PCGExPointFilter::IFilter
	{
	public:
		FPCGExStateConfigBase BaseConfig;
		const UPCGExPointStateFactoryData* StateFactory = nullptr;

		explicit FState(const TObjectPtr<const UPCGExPointStateFactoryData>& InFactory)
			: IFilter(InFactory), StateFactory(InFactory)
		{
		}

		virtual ~FState() override;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		bool InitInternalManager(FPCGExContext* InContext, const TArray<TObjectPtr<const UPCGExPointFilterFactoryData>>& InFactories);
		virtual bool Test(const int32 Index) const override;

		void ProcessFlags(const bool bSuccess, int64& InFlags) const;

	protected:
		TSharedPtr<PCGExPointFilter::FManager> Manager;
	};

	/**
	 * Specialized manager that evaluates all states for each point and accumulates
	 * their bitmask flag mutations into the shared FlagsCache array.
	 * Test() always returns true -- the purpose is the side-effect on flags, not filtering.
	 */
	class PCGEXFILTERS_API FStateManager final : public PCGExPointFilter::FManager
	{
		TArray<TSharedPtr<FState>> States;
		TSharedPtr<TArray<int64>> FlagsCache;

	public:
		explicit FStateManager(const TSharedPtr<TArray<int64>>& InFlags, const TSharedRef<PCGExData::FFacade>& InPointDataFacade);

		virtual bool Test(const int32 Index) override;

	protected:
		virtual void PostInitFilter(FPCGExContext* InContext, const TSharedPtr<PCGExPointFilter::IFilter>& InFilter) override;
	};
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class PCGEXFILTERS_API UPCGExStateFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	PCGEX_NODE_INFOS(PointStateDefinition, "Abstract Point State Definition", "Base class for state factory management.")
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(ClusterState); }
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Unique name for this state definition. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName Name = FName("Flag");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable), AdvancedDisplay)
	int32 Priority = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bOutputBitmasks = true;

	virtual bool CanOutputBitmasks() const { return true; }
	virtual FName GetMainOutputPin() const override;
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

protected:
	virtual TSet<PCGExFactories::EType> GetInternalFilterTypes() const;
	virtual void OutputBitmasks(FPCGExContext* InContext, const FPCGExStateConfigBase& InConfig) const;
};
