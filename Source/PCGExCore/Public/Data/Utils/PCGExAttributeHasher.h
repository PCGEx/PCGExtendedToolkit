// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <functional>

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExMTCommon.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Sorting/PCGExPointSorter.h"
#include "PCGExAttributeHasher.generated.h"

namespace PCGExMT
{
	class FTaskManager;
}

UENUM()
enum class EPCGExDataHashScope : uint8
{
	All          = 0 UMETA(DisplayName = "All", Tooltip="Combines all the values"),
	Uniques      = 1 UMETA(DisplayName = "Uniques", Tooltip="Combines all the unique values, ignoring duplicates."),
	FirstAndLast = 2 UMETA(DisplayName = "First and Last", Tooltip="Combines the value from the first and last point"),
	First        = 3 UMETA(DisplayName = "First only", Tooltip="Uses the value from the first point only"),
	Last         = 4 UMETA(DisplayName = "Last only", Tooltip="Uses the value from the last point only"),
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExAttributeHashConfig
{
	GENERATED_BODY()

	FPCGExAttributeHashConfig() = default;
	virtual ~FPCGExAttributeHashConfig() = default;

	/** Which attribute should be used to generate the hash */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector SourceAttribute;

	/** Which values will be combined to a hash */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDataHashScope Scope = EPCGExDataHashScope::Uniques;

	/** Sort values before combining into hash. Ensures consistent hashes regardless of point order. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bSortInputValues = true;

	/** Whether to sort hash components or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSortInputValues"))
	EPCGExSortDirection Sorting = EPCGExSortDirection::Ascending;
};

namespace PCGEx
{
	class PCGEXCORE_API FAttributeHasher : public TSharedFromThis<FAttributeHasher>
	{
		FPCGExAttributeHashConfig Config;

		TSharedPtr<PCGExData::FFacade> DataFacade;
		TSharedPtr<PCGExData::IBufferProxy> ValuesBuffer;
		TArray<PCGExValueHash> Hashes;

		int32 NumValues = -1;
		uint32 OutHash = 0;

		TSharedPtr<PCGExMT::TScopedArray<PCGExValueHash>> ScopedHashes;

		PCGExMT::FSimpleCallback CompleteCallback;

	public:
		explicit FAttributeHasher(const FPCGExAttributeHashConfig& InConfig);
		~FAttributeHasher() = default;

		bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InFacade);

		bool RequiresCompilation();
		void Compile(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, PCGExMT::FSimpleCallback&& InCallback);

		uint32 GetHash() const
		{
			return OutHash;
		}

	protected:
		void CompileScope(const PCGExMT::FScope& Scope);
		void OnCompilationComplete();
	};
}
