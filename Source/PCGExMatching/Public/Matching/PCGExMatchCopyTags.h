// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "PCGExMatchCopyTags.generated.h"


USTRUCT(BlueprintType)
struct FPCGExMatchCopyTagsConfig : public FPCGExMatchRuleConfigBase
{
	GENERATED_BODY()

	FPCGExMatchCopyTagsConfig()
		: FPCGExMatchRuleConfigBase()
	{
	}
};

/**
 * 
 */
class FPCGExMatchCopyTags : public FPCGExMatchRuleOperation
{
public:
	FPCGExMatchCopyTagsConfig Config;

	virtual bool PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources) override;

	virtual bool Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExMatchCopyTagsFactory : public UPCGExMatchRuleFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExMatchCopyTagsConfig Config;

	virtual TSharedPtr<FPCGExMatchRuleOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|DataMatch", meta=(PCGExNodeLibraryDoc="utilities/data-matching/match-copy-tags"))
class UPCGExCreateMatchCopyTagsSettings : public UPCGExMatchRuleFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(MatchAttrToAttr, "Match : Copy Tags", "Copy matched targets tags to candidate", FName (GetDisplayName ()))

#endif
	//~End UPCGSettings

	/** Rules properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExMatchCopyTagsConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
