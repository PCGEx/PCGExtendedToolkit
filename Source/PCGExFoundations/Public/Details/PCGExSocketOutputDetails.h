// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/Utils/PCGExDataFilterDetails.h"

#include "PCGExSocketOutputDetails.generated.h"

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="staging/common-settings/socket-output-details"))
struct PCGEXFOUNDATIONS_API FPCGExSocketOutputDetails
{
	GENERATED_BODY()

	FPCGExSocketOutputDetails() = default;

	/** Include or exclude sockets based on their tag */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails SocketTagFilters;

	/** Include or exclude sockets based on their name */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExNameFiltersDetails SocketNameFilters;

	/** Write socket name to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSocketName = false;

	/** Attribute name to write socket name to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteSocketName"))
	FName SocketNameAttributeName = "SocketName";

	/** Write socket tag to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSocketTag = false;

	/** Attribute name to write socket tag to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteSocketTag"))
	FName SocketTagAttributeName = "SocketTag";

	/** Write category to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCategory = false;

	/** Attribute name to write category to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteCategory"))
	FName CategoryAttributeName = "Category";

	/** Write asset path to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAssetPath = false;

	/** Attribute name to write asset path to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bWriteAssetPath"))
	FName AssetPathAttributeName = "AssetPath";

	/** Which scale components from the sampled transform should be applied to the point.  */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 TransformScale = 7; //static_cast<uint8>(EPCGExApplySampledComponentFlags::All);

	/** Meta filter settings for socket points, as they naturally inherit from the original points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;


	bool Init(FPCGExContext* InContext);

	TArray<int32> TrScaComponents;
};
