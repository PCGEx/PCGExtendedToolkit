// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchingCommon.h"
#include "Factories/PCGExFactoryData.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Factories/PCGExOperation.h"

#include "PCGExMatchRuleFactoryProvider.generated.h"

#define PCGEX_MATCH_RULE_BOILERPLATE(_RULE) \
TSharedPtr<FPCGExMatchRuleOperation> UPCGExMatch##_RULE##Factory::CreateOperation(FPCGExContext* InContext) const{ \
	PCGEX_FACTORY_NEW_OPERATION(Match##_RULE)\
	NewOperation->Config = Config; \
	NewOperation->Config.Init(); \
	return NewOperation; } \
UPCGExFactoryData* UPCGExCreateMatch##_RULE##Settings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{ \
	UPCGExMatch##_RULE##Factory* NewFactory = InContext->ManagedObjects->New<UPCGExMatch##_RULE##Factory>(); \
	NewFactory->BaseConfig = Config; \
	NewFactory->Config = Config; \
	return Super::CreateFactory(InContext, NewFactory);}


struct FPCGExTaggedData;

namespace PCGExData
{
	class FPointIO;
	struct FConstPoint;
}

namespace PCGExMatching
{
	struct FScope;
}

USTRUCT(BlueprintType)
struct PCGEXMATCHING_API FPCGExMatchRuleConfigBase
{
	GENERATED_BODY()

	FPCGExMatchRuleConfigBase() = default;
	virtual ~FPCGExMatchRuleConfigBase() = default;

	/** Match Strictness */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1))
	EPCGExMatchStrictness Strictness = EPCGExMatchStrictness::Any;

	/** Invert the match result (match becomes no-match and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bInvert = false;

	virtual void Init()
	{
	}
};

/**
 *
 */
class PCGEXMATCHING_API FPCGExMatchRuleOperation : public FPCGExOperation
{
public:
	virtual bool PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources);

	virtual bool Test(const PCGExData::FConstPoint& InMatchableSourceElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const PCGEX_NOT_IMPLEMENTED_RET(Test, false);

	/** Whether this rule type supports recursive/transitive matching */
	virtual bool SupportsRecursion() const
	{
		return false;
	}

	/** Whether recursion is both supported AND enabled in config */
	virtual bool WantsRecursion() const
	{
		return false;
	}

	/** Maximum recursion depth (-1 = unlimited). Only meaningful if WantsRecursion() returns true. */
	virtual int32 GetMaxRecursionDepth() const
	{
		return -1;
	}

protected:
	TSharedPtr<TArray<FPCGExTaggedData>> MatchableSources;
};

USTRUCT(meta=(PCG_DataTypeDisplayName="PCGEx | Match Rule"))
struct FPCGExDataTypeInfoMatchRule : public FPCGExFactoryDataTypeInfo
{
	GENERATED_BODY()
	PCG_DECLARE_TYPE_INFO(PCGEXMATCHING_API)
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXMATCHING_API UPCGExMatchRuleFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGExDataTypeInfoMatchRule)

	FPCGExMatchRuleConfigBase BaseConfig;

	virtual bool WantsPoints() const
	{
		return false;
	}

	virtual TSharedPtr<FPCGExMatchRuleOperation> CreateOperation(FPCGExContext* InContext) const;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|DataMatch", meta=(PCGExNodeLibraryDoc="utilities/data-matching/match-rule"))
class PCGEXMATCHING_API UPCGExMatchRuleFactoryProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

protected:
	PCGEX_FACTORY_TYPE_ID(FPCGExDataTypeInfoMatchRule)

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(MatchRule, "Match Rule Definition", "Creates a single match rule node, to be used with nodes that support data matching.")

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::ControlFlow;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MatchRule);
	}
#endif
	//~End UPCGSettings

	virtual FName GetMainOutputPin() const override
	{
		return PCGExMatching::Labels::OutputMatchRuleLabel;
	}

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
