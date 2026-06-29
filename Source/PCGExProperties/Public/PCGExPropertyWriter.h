// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExProperty.h"
#include "Data/PCGExData.h"

#include "PCGExPropertyWriter.generated.h"

USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyOutputConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta=(EditCondition="bEnabled"))
	FName PropertyName;

	/** Defaults to PropertyName when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta=(EditCondition="bEnabled"))
	FName OutputAttributeName;

	/** NAME_None when the resolved name fails PCG attribute-name validation. */
	FName GetEffectiveOutputName() const;

	bool IsValid() const
	{
		return bEnabled && !PropertyName.IsNone() && !GetEffectiveOutputName().IsNone();
	}
};

USTRUCT(BlueprintType)
struct PCGEXPROPERTIES_API FPCGExPropertyOutputSettings
{
	GENERATED_BODY()

	/** Explicit configs win over IncludedSchemas-derived entries on name conflict, regardless
	 *  of bEnabled -- a disabled explicit entry suppresses the schema's same-named output. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName="Properties Mapping", TitleProperty="{PropertyName}"))
	TArray<FPCGExPropertyOutputConfig> Configs;

	/** Each asset's Collection is resolved recursively (locals + ImportedSchemas, cycle-safe);
	 *  every resolved entry contributes an enabled config keyed by its Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta=(DisplayName="Included Schemas"))
	TArray<TObjectPtr<UPCGExPropertySchemaAsset>> IncludedSchemas;

	/** Configs first (in order, including disabled), then IncludedSchemas entries (first-wins,
	 *  name-deduped against Configs). OutConfigs is Reset before population. */
	void GetEffectiveConfigs(TArray<FPCGExPropertyOutputConfig>& OutConfigs) const;

	/** True on the first valid explicit Config, or failing that, on the first named IncludedSchemas entry. */
	bool HasOutputs() const;

	/** Appends output-supporting Registry entries that aren't already present as enabled Configs. */
	int32 AutoPopulateFromRegistry(TConstArrayView<FPCGExPropertyRegistryEntry> Registry)
	{
		TSet<FName> ExistingNames;
		for (const FPCGExPropertyOutputConfig& Config : Configs)
		{
			if (Config.bEnabled && !Config.PropertyName.IsNone())
			{
				ExistingNames.Add(Config.PropertyName);
			}
		}

		int32 AddedCount = 0;
		for (const FPCGExPropertyRegistryEntry& Entry : Registry)
		{
			if (Entry.bSupportsOutput && !ExistingNames.Contains(Entry.PropertyName))
			{
				FPCGExPropertyOutputConfig& NewConfig = Configs.AddDefaulted_GetRef();
				NewConfig.bEnabled = true;
				NewConfig.PropertyName = Entry.PropertyName;
				AddedCount++;
			}
		}

		return AddedCount;
	}
};

/** Abstraction over "where do properties come from" for the writer classes below. The prototype
 *  (FindPrototypeProperty) is cloned during writer Initialize; per-source values come from
 *  GetPropertyAt during the per-row write. */
class PCGEXPROPERTIES_API IPCGExPropertyProvider
{
public:
	virtual ~IPCGExPropertyProvider() = default;

	virtual TConstArrayView<FInstancedStruct> GetProperties(int32 Index) const = 0;
	virtual TConstArrayView<FPCGExPropertyRegistryEntry> GetPropertyRegistry() const = 0;
	virtual const FInstancedStruct* FindPrototypeProperty(FName PropertyName) const = 0;

	/** Override with an O(1) name-keyed lookup when the provider has one -- the writer hot path
	 *  calls this per writer per row, so the default linear scan via GetProperties is
	 *  O(P*SchemaSize) per row vs O(P) with an override. */
	virtual const FInstancedStruct* GetPropertyAt(int32 SourceIndex, FName PropertyName) const
	{
		return PCGExProperties::GetPropertyByName(GetProperties(SourceIndex), PropertyName);
	}
};

class FPCGMetadataAttributeBase;
class UPCGMetadata;
struct FPCGExContext;

/**
 * Metadata-attribute writer (single-threaded per instance). Pair with FPCGExPropertyWriter when
 * the target is an FFacade-backed point buffer instead.
 *
 *   FPCGExPropertySetWriter W;
 *   W.Initialize(Ctx, Provider, Settings, Metadata);
 *   for (each row r) W.WriteEntry(EntryKey[r], SourceIndexFor(r));
 *
 * For sources that don't fit the SourceIndex shape, drive the per-writer loop yourself via
 * Num() / GetPropertyName / WriteAt (see FPCGExCollectionPropertySetWriter).
 */
class PCGEXPROPERTIES_API FPCGExPropertySetWriter
{
public:
	FPCGExPropertySetWriter() = default;

	/** Provider must outlive WriteAt / WriteEntry calls. Writers whose prototype is missing or
	 *  whose CreateMetadataAttribute returns null are silently dropped; true if any survive. */
	bool Initialize(
		FPCGExContext* InContext,
		const IPCGExPropertyProvider* InProvider,
		const FPCGExPropertyOutputSettings& OutputSettings,
		UPCGMetadata* Metadata);

	/** Skips GetEffectiveConfigs -- pass pre-assembled configs (e.g. from a discovered schema union). */
	bool Initialize(
		FPCGExContext* InContext,
		const IPCGExPropertyProvider* InProvider,
		TConstArrayView<FPCGExPropertyOutputConfig> EffectiveConfigs,
		UPCGMetadata* Metadata);

	int32 Num() const { return Writers.Num(); }
	bool HasOutputs() const { return !Writers.IsEmpty(); }
	FName GetPropertyName(int32 WriterIdx) const { return Writers[WriterIdx].PropertyName; }

	/** Silent no-op on script-struct mismatch -- the type-erased FPCGExProperty interface can't
	 *  transfer values between different concrete types. */
	void WriteAt(int32 WriterIdx, int64 Key, const FInstancedStruct& Source);

	/** Returns the count of successful writes (compare to Num() for partial-match detection).
	 *  Returns 0 when no Provider was set during Initialize. */
	int32 WriteEntry(int64 Key, int32 SourceIndex);

protected:
	struct FWriter
	{
		FName PropertyName;
		FInstancedStruct WriterInstance;
		FPCGMetadataAttributeBase* Attribute = nullptr;
	};

	const IPCGExPropertyProvider* Provider = nullptr;
	TArray<FWriter> Writers;
};

/**
 * FFacade-backed point-attribute writer. Pair with FPCGExPropertySetWriter when the target is
 * a UPCGMetadata instead.
 *
 *   FPCGExPropertyWriter W;
 *   W.Initialize(Provider, OutputFacade, Settings);   // single-threaded boot
 *   W.WriteProperties(PointIdx, SourceIdx);            // per-point write
 *
 * NOT thread-safe -- CopyValueFrom mutates the writer instance. For parallel writes go through
 * FPCGExProperty::WriteOutputFrom directly.
 */
class PCGEXPROPERTIES_API FPCGExPropertyWriter
{
public:
	FPCGExPropertyWriter() = default;

	bool Initialize(
		const IPCGExPropertyProvider* InProvider,
		const TSharedRef<PCGExData::FFacade>& OutputFacade,
		const FPCGExPropertyOutputSettings& OutputSettings
		);

	void WriteProperties(int32 PointIndex, int32 SourceIndex);

	bool HasOutputs() const;

protected:
	FPCGExPropertyOutputSettings Settings;
	const IPCGExPropertyProvider* Provider = nullptr;

	/** Cloned per-output prototypes that own their FFacade output buffer (allocated during
	 *  Initialize via InitializeOutput). Per-row writes copy source values into these clones
	 *  then flush via WriteOutput. */
	TMap<FName, FInstancedStruct> WriterInstances;
};
