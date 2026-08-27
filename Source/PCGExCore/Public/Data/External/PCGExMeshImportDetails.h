// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Types/PCGExAttributeIdentity.h"

#include "PCGExMeshImportDetails.generated.h"

struct FPCGExContext;

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="clusters/common-settings/geo-mesh-import-details"))
struct PCGEXCORE_API FPCGExGeoMeshImportDetails
{
	GENERATED_BODY()

	FPCGExGeoMeshImportDetails() = default;

	/** Import vertex colors from mesh as point attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bImportVertexColor = true;

	/** Import per-vertex normals from the mesh, if available, by orienting each point so its Up axis (+Z) faces the vertex normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bImportVertexNormals = false;

	/** Import UV coordinates from mesh as point attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bImportUVs = false;

	/** A list of mapping channel in the format [Output Attribute Name]::[Channel Index]. You can output the same UV channel to different attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ├─ UV Channels Mapping", EditCondition="bImportUVs"))
	TMap<FName, int32> UVChannels;

	/** If enabled, will create placeholder attributes if a listed UV channel is missing. This is useful if the rest of your graph expects those attribute to exist, even if invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" ├─ Create placeholders", EditCondition="bImportUVs"))
	bool bCreatePlaceholders = false;

	/** Placeholder UV value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName=" └─ Placeholder Value", EditCondition="bImportUVs && bCreatePlaceholders"))
	FVector2D Placeholder = FVector2D(0, 0);

	TArray<FPCGAttributeIdentifier> UVChannelId;
	TArray<int32> UVChannelIndex;

	bool Validate(FPCGExContext* InContext);

	bool WantsImport() const;
};
