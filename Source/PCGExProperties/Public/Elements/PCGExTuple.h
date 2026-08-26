// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"

#include "PCGExProperty.h"
#include "PCGSettings.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"

#include "PCGExTuple.generated.h"

class FPCGMetadataAttributeBase;

// ============================================================================
// TUPLE NODE - Reference implementation for the property system
// ============================================================================
//
// The Tuple node is the simplest consumer of the property system.
// It demonstrates how to:
// - Use FPCGExPropertySchemaCollection to define columns (Composition)
// - Use TArray<FPCGExPropertyOverrides> for row values (Values)
// - Sync overrides to schema in PostEditChangeProperty
// - Create metadata attributes from properties (CreateMetadataAttribute)
// - Write property values to metadata entries (WriteMetadataValue)
//
// If you're building a node that needs user-definable typed properties,
// this is the best starting point to study.
//
// All value types (Boolean, Float, String, etc.) use FPCGExProperty_* from PCGExProperty.h

#pragma region Tuple Header and Body

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="metadata/keys/tuple"))
class UPCGExTupleSettings : public UPCGExSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Tuple, "Tuple", "A Simple Tuple attribute.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Param;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Tuple composition - defines the columns (property types and names) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ToolTip="Tuple composition defines the columns (property types and names)."))
	FPCGExPropertySchemaCollection Composition;

	/** Tuple values - each row uses PropertyOverrides to enable/disable columns */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ToolTip="Tuple values. Toggle 'Enabled' per column to include/exclude values. Rows auto-sync with composition changes.", FullyExpand=true))
	TArray<FPCGExPropertyOverrides> Values;

	/** A list of tags separated by a comma, for easy overrides. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString CommaSeparatedTags;

	/** Emit the "Map" attribute set describing external resources referenced by written values (e.g. Collection Entry picks). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay, meta=(DisplayName="Output Map"))
	bool bOutputMap = false;
};

struct FPCGExTupleContext final : FPCGExContext
{
	/** Output dependencies of every authored value (e.g. Collection Entry picks). */
	virtual void RegisterAssetDependencies() override;
};

class FPCGExTupleElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Tuple)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
