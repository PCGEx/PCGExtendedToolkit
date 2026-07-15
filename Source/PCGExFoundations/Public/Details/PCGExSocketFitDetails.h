// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSocket.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"

#include "PCGExSocketFitDetails.generated.h"

namespace PCGExData
{
	class FFacade;
}

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExSocketFitDetails
{
	GENERATED_BODY()

	FPCGExSocketFitDetails() = default;

	/** Whether socket fit is enabled or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bEnabled = false;

	/** Name of the socket to fit to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Socket Name", EditCondition="bEnabled", EditConditionHides))
	FPCGExInputShorthandNameName SocketNameValue = FPCGExInputShorthandNameName(FName(NAME_None), NAME_None, true);

	/** Type of Socket name input */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	//EPCGExInputValueType SocketTagInput = EPCGExInputValueType::Attribute;

	/** Attribute to read socket name from. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Socket Tag (Attr)", EditCondition="SocketTagInput != EPCGExInputValueType::Constant", EditConditionHides))
	//FName SocketTagttribute = NAME_None;

	/** Socket name */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Socket Tag", EditCondition="SocketTagInput == EPCGExInputValueType::Constant", EditConditionHides))
	//FString SocketTag = TEXT("");

	//PCGEX_SETTING_VALUE_GET(SocketTag, FString, SocketTagInput, SocketTagAttribute, SocketTag)

	bool Init(const TSharedPtr<PCGExData::FFacade>& InFacade);
	void Mutate(const int32 Index, const TArray<FPCGExSocket>& InSockets, FTransform& InOutTransform) const;

protected:
	bool bMutate = false;
	TSharedPtr<PCGExDetails::TSettingValue<FName>> SocketNameBuffer;
};
