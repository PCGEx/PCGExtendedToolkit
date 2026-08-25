// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExAttributeHasher.h"


#include "PCGExAttributeHash.generated.h"

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="metadata/analyze/attribute-hash"))
class UPCGExAttributeHashSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(AttributeHash, "Attribute Hash", "Generates a hash from the input data, based on a attribute or property.", FName(FString::Printf(TEXT("Hash : %s"), *HashConfig.SourceAttribute.GetName().ToString())));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(MiscWrite);
	}
#endif

protected:
	virtual bool HasDynamicPins() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Configuration for which attributes to hash and how. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ShowOnlyInnerProperties))
	FPCGExAttributeHashConfig HashConfig;

	/** Name to output the hash to (written on @Data domain) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName OutputName = FName("Hash");

	/** Whether to add the hash as a tag */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputToTags = false;

	/** Whether to add the hash as an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputToAttribute = true;
};

struct FPCGExAttributeHashContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExAttributeHashElement;
	TArray<bool> ValidHash;
	TArray<int32> Hashes;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExAttributeHashElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributeHash)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExAttributeHash
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAttributeHashContext, UPCGExAttributeHashSettings>
	{
		TSharedPtr<PCGEx::FAttributeHasher> Hasher;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
