// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExSettings.h"

#include "PCGExCustomVersion.h"
#include "PCGExVersion.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGExLog.h"
#include "Core/PCGExContext.h"
#include "PCGExSettingsCacheBody.h"
#include "PCGCommon.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Core/PCGExContext.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Styling/SlateStyle.h"
#include "UObject/UnrealType.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#include "Helpers/PCGSettingsHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Algo/Transform.h"
#include "Metadata/PCGMetadataCommon.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExSettings"

namespace PCGExSettingsCVars
{
	bool bForceOffThread = false;
	FAutoConsoleVariableRef CVarForceOffThread(
		TEXT("pcgex.ForceOffThread"),
		bForceOffThread,
		TEXT("Force every PCGEx node's prepare and execute phases off the game thread, ignoring per-node Force Off Thread and the Runtime Always Off Thread plugin setting."));
}

#if WITH_EDITOR
namespace PCGExSettings
{
	// Must match PCGSettings::PropertyPathSeparator (private to PCGSettings.cpp; GetPropertyPath() is not exported).
	const TCHAR* PropertyPathSeparator = TEXT("/");

	// Property meta: a nested struct member opts into a whole-struct param when the node expands nested structs.
	const TCHAR* NestedStructOverridableMeta = TEXT("PCGExNestedStructOverridable");

	FString JoinPath(const TArrayView<const FName> InNames)
	{
		return FString::JoinBy(InNames, PropertyPathSeparator, [](const FName InName) { return InName.ToString(); });
	}

	// Mirrors the keep predicate of UPCGSettings::GatherOverridableParams.
	bool ShouldKeepProperty(const FProperty* InProperty, const int32 InDepth)
	{
		if (InProperty->HasMetaData(PCGObjectMetadata::Overridable)
			|| InProperty->HasMetaData(PCGObjectMetadata::OverridableCPUAndGPU)
			|| InProperty->HasMetaData(PCGObjectMetadata::OverridableCPUAndGPUWithReadback))
		{
			return true;
		}

		if (InProperty->HasMetaData(PCGObjectMetadata::NotOverridable) || !InProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			return false;
		}

		return InDepth > 0;
	}

	bool PathStartsWith(const TArray<FName>& InPath, const TArray<FName>& InPrefix)
	{
		if (InPath.Num() < InPrefix.Num()) { return false; }
		for (int32 i = 0; i < InPrefix.Num(); i++)
		{
			if (InPath[i] != InPrefix[i]) { return false; }
		}
		return true;
	}

	struct FWholeStructGatherer
	{
		const UClass* SettingsClass = nullptr;
		bool bExpandNested = false;
		TSet<FName> UsedLabels;

		// Whole-struct params in walk order: every parent precedes its descendants.
		TArray<FPCGSettingsOverridableParam> StructParams;

		// A PCG_OverridableChildProperties list, applied to the path relative to the property that carries it.
		struct FChildFilter
		{
			int32 Root = 0;
			TArray<FString> Paths;
		};

		TArray<FName> Names;
		TArray<const FProperty*> Chain;
		TArray<FChildFilter> Filters;
		TArray<const FProperty*> VisitedObjectProperties;

		void Walk(const UStruct* InStruct, const int32 InDepth)
		{
			for (TFieldIterator<FProperty> It(InStruct, EFieldIteratorFlags::IncludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
			{
				const FProperty* Property = *It;
				if (!ShouldKeepProperty(Property, InDepth)) { continue; }

				Names.Add(*Property->GetAuthoredName());
				Chain.Add(Property);

				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					VisitStruct(StructProperty, InDepth);
				}
				else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);
					ObjectProperty && ObjectProperty->HasAllPropertyFlags(CPF_InstancedReference) && !VisitedObjectProperties.Contains(Property))
				{
					// Instanced sub-objects are walked through their declared class, as Super does.
					VisitedObjectProperties.Add(Property);
					Walk(ObjectProperty->PropertyClass, InDepth + 1);
					VisitedObjectProperties.Pop(EAllowShrinking::No);
				}

				Names.Pop(EAllowShrinking::No);
				Chain.Pop(EAllowShrinking::No);
			}
		}

		void VisitStruct(const FStructProperty* InStructProperty, const int32 InDepth)
		{
			// Super's own gate: a struct its old accessors support is a leaf param there and is never recursed.
			if (PCGAttributeAccessorHelpers::IsPropertyAccessorSupported(InStructProperty)) { return; }

			const FPCGMetadataAttributeDesc Desc = FPCGMetadataAttributeDesc::CreateFromProperty(InStructProperty);
			if (!Desc.IsValid() || Desc.ValueType != EPCGMetadataTypes::Struct) { return; }

			// Root = owned by a class (the settings or an instanced sub-object); a struct owned by a struct is nested and must opt in.
			const bool bRoot = Cast<UClass>(InStructProperty->GetOwnerStruct()) != nullptr;
			const bool bExposed = bRoot || (bExpandNested && InStructProperty->HasMetaData(NestedStructOverridableMeta));

			if (bExposed && !IsFilteredOut()) { Emit(); }

			const bool bHasOwnFilter = InStructProperty->HasMetaData(PCGObjectMetadata::OverridableChildProperties);
			if (bHasOwnFilter)
			{
				FChildFilter& Filter = Filters.Emplace_GetRef();
				Filter.Root = Names.Num();
				Filter.Paths = PCGHelpers::GetStringArrayFromCommaSeparatedList(InStructProperty->GetMetaData(PCGObjectMetadata::OverridableChildProperties));
			}

			Walk(InStructProperty->Struct, InDepth + 1);

			if (bHasOwnFilter) { Filters.Pop(EAllowShrinking::No); }
		}

		bool IsFilteredOut() const
		{
			for (const FChildFilter& Filter : Filters)
			{
				if (!Filter.Paths.Contains(JoinPath(TArrayView<const FName>(Names).RightChop(Filter.Root)))) { return true; }
			}
			return false;
		}

		void Emit()
		{
			FPCGSettingsOverridableParam& Param = StructParams.Emplace_GetRef();
			Param.PropertiesNames = Names;
			Param.Properties = Chain;
			Param.PropertyClass = SettingsClass;
			Param.NumContainers = 0;
#if WITH_EDITORONLY_DATA
			Param.UnderlyingType = EPCGMetadataTypes::Struct;
#endif

			for (int32 i = 0; i < Chain.Num(); i++)
			{
				if (!Chain[i]->HasMetaData(PCGObjectMetadata::OverrideAliases)) { continue; }
				FPCGPropertyAliases& Entry = Param.MapOfAliases.FindOrAdd(i);
				Algo::Transform(PCGHelpers::GetStringArrayFromCommaSeparatedList(Chain[i]->GetMetaData(PCGObjectMetadata::OverrideAliases)), Entry.Aliases, [](const FString& In) { return FName(In); });
			}

			FName Label = Names.Last();
			if (UsedLabels.Contains(Label))
			{
				Param.bHasNameClash = true;
				Label = FName(JoinPath(Names));

				// A top-level path is its own name, so the path alone cannot disambiguate.
				if (UsedLabels.Contains(Label)) { Label = FName(JoinPath(Names) + TEXT(" (Struct)")); }
			}

			UsedLabels.Add(Label);
			Param.Label = Label;
		}

		// Each struct param goes right before the first Super param under its path: the engine applies in list order, so members win.
		TArray<FPCGSettingsOverridableParam> Merge(TArray<FPCGSettingsOverridableParam>&& InSuperParams)
		{
			TArray<FPCGSettingsOverridableParam> Result;
			Result.Reserve(InSuperParams.Num() + StructParams.Num());

			TBitArray<> Placed(false, StructParams.Num());
			for (FPCGSettingsOverridableParam& SuperParam : InSuperParams)
			{
				for (int32 i = 0; i < StructParams.Num(); i++)
				{
					if (Placed[i]) { continue; }
					const TArray<FName>& StructPath = StructParams[i].PropertiesNames;
					if (SuperParam.PropertiesNames.Num() > StructPath.Num() && PathStartsWith(SuperParam.PropertiesNames, StructPath))
					{
						Result.Add(MoveTemp(StructParams[i]));
						Placed[i] = true;
					}
				}
				Result.Add(MoveTemp(SuperParam));
			}

			for (int32 i = 0; i < StructParams.Num(); i++)
			{
				if (!Placed[i]) { Result.Add(MoveTemp(StructParams[i])); }
			}

			return Result;
		}
	};
}

TArray<FPCGSettingsOverridableParam> UPCGExSettings::GatherOverridableParams() const
{
	TArray<FPCGSettingsOverridableParam> Params = Super::GatherOverridableParams();
	if (!bStructOverrides)
	{
		return Params;
	}

	PCGExSettings::FWholeStructGatherer Gatherer;
	Gatherer.SettingsClass = GetClass();
	Gatherer.bExpandNested = bExpandNestedStructs;
	Gatherer.UsedLabels.Add(PCGPinConstants::DefaultParamsLabel);
	for (const FPCGSettingsOverridableParam& Param : Params) { Gatherer.UsedLabels.Add(Param.Label); }

	Gatherer.Walk(GetClass(), 0);
	return Gatherer.Merge(MoveTemp(Params));
}

void UPCGExSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
}

void UPCGExSettings::ApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	// No fresh-node guard: PCGExDataVersion is resolved in Serialize, so per-block PCGEX_IF_VERSION_LOWER
	// gates already distinguish legacy from current data.
	PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);

	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	ApplyPropertyDeprecation(InOutNode);

	Super::ApplyDeprecation(InOutNode);
	
	PCGEX_UPDATE_DATA_VERSION_TO_LATEST
	
	ensure(PCGExDataVersion == PCGExVersion::Latest);
}

void UPCGExSettings::ApplyPropertyDeprecation(UPCGNode* InOutNode)
{
	if (bPropertyDeprecationApplied) { return; }
	bPropertyDeprecationApplied = true;

	PCGExApplyDeprecation(InOutNode);
	ApplyInstancedFactoriesDeprecation();
}

void UPCGExSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
}

void UPCGExSettings::ApplyInstancedFactoriesDeprecation()
{
	// Instanced operations serialize in this package, so the resolved PCGExDataVersion is theirs too.
	// Walks structs and containers, then each found operation for nested ones; only owned instances are touched.
	TArray<UPCGExInstancedFactory*> Factories;
	TSet<const UPCGExInstancedFactory*> Visited;

	auto Gather = [&](const UStruct* InStruct, const void* InContainer)
	{
		for (TPropertyValueIterator<FObjectProperty> It(InStruct, InContainer, EPropertyValueIteratorFlags::FullRecursion, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
		{
			if (!It.Key()->HasAnyPropertyFlags(CPF_InstancedReference)) { continue; }

			UPCGExInstancedFactory* Factory = Cast<UPCGExInstancedFactory>(It.Key()->GetObjectPropertyValue(It.Value()));
			if (!Factory || !Factory->IsIn(this)) { continue; }

			bool bAlreadyVisited = false;
			Visited.Add(Factory, &bAlreadyVisited);
			if (!bAlreadyVisited) { Factories.Add(Factory); }
		}
	};

	Gather(GetClass(), this);

	for (int32 i = 0; i < Factories.Num(); i++)
	{
		UPCGExInstancedFactory* Factory = Factories[i];
		Gather(Factory->GetClass(), Factory);
		Factory->PCGExApplyDeprecation(PCGExDataVersion);
	}
}

void UPCGExSettings::ResolveDataVersion()
{
	// Source the deprecation version from the package custom version: the engine fills UserDataVersion
	// from GetUserCustomVersionGuid() during Serialize, and it lives in the archive header so it is never
	// dropped by delta serialization. Legacy assets predating the custom version have UserDataVersion < 0:
	// keep their captured per-object PCGExDataVersion if present, else (never stamped) treat as oldest.
	if (UserDataVersion >= 0) { PCGExDataVersion = UserDataVersion; }
	else if (PCGExDataVersion == INDEX_NONE) { PCGExDataVersion = 0; }
	// A node in a legacy package (UserDataVersion < 0) that was never version-stamped (INDEX_NONE) predates
	// deprecation stamping entirely, so it must run ALL deprecation -> treat as oldest (0), NOT current.
	// Genuinely-new nodes never reach this branch: any package a current build saves records the PCGEx
	// custom version in its archive header, so they load with UserDataVersion >= 0 (the branch above).
	// else: keep the captured legacy PCGExDataVersion as-is.
}

void UPCGExSettings::RetireInputPin(UPCGNode* InOutNode, const FName InLabel) const
{
	UPCGPin* Pin = InOutNode ? InOutNode->GetInputPin(InLabel) : nullptr;
	if (!Pin || !Pin->IsConnected()) { return; }

	// A label the settings still declare is live, not retired.
	if (AllInputPinProperties().ContainsByPredicate([&InLabel](const FPCGPinProperties& Properties) { return Properties.Label == InLabel; })) { return; }

	const int32 NumEdges = Pin->EdgeCount();
	Pin->BreakAllEdges();

	UE_LOG(LogPCGEx, Warning, TEXT("[%s] %s: removed %d connection(s) to the '%s' input pin, which this node no longer has. Re-save the graph to clear this warning."), *GetPathNameSafe(InOutNode->GetGraph()), *InOutNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), NumEdges, *FName::NameToDisplayString(InLabel.ToString(), false));
}

bool UPCGExSettings::GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip) const
{
	return PCGEX_CORE_SETTINGS.GetPinExtraIcon(InPin, OutExtraIcon, OutTooltip, InPin->IsOutputPin());
}

void UPCGExSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		const bool bIsInstanced = Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference);
		if (bIsInstanced)
		{
			DirtyCache();
		}
	}

	// A plain edit never re-gathers; Super's change broadcast then runs UpdatePins against the fresh list.
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UPCGExSettings, bStructOverrides)
		|| ChangedName == GET_MEMBER_NAME_CHECKED(UPCGExSettings, bExpandNestedStructs))
	{
		InitializeCachedOverridableParams(/*bReset=*/true);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGExSettings::PostLoad()
{
#if WITH_EDITOR
	// Node-owned settings are migrated by their graph, in order with its pin updates; nothing else reaches the rest.
	// Before Super, so the CRC it caches sees the migrated values.
	// TODO: pin blocks gate on these settings' version, so an asset instanced by several graphs only migrates the first graph's pins.
	if (PCGExDataVersion < PCGExVersion::Latest && !GetOuter()->IsA<UPCGNode>()) { ApplyPropertyDeprecation(nullptr); }
#endif

	Super::PostLoad();
}

FGuid UPCGExSettings::GetUserCustomVersionGuid()
{
	return FPCGExCustomVersion::GUID;
}

void UPCGExSettings::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	// After Super, UserDataVersion holds this package's PCGEx custom version (-1 if it predates it).
	// Resolve the effective deprecation version before PostLoad and the graph's deprecation pass run.
	// Fresh nodes need no explicit stamp: any package saved by a current build writes the PCGEx custom
	// version, so UserDataVersion >= 0 on reload and ResolveDataVersion resolves them via that (never the
	// INDEX_NONE->oldest path). Do NOT stamp on save here — it mutates the CDO/templates and poisons the
	// delta-serialization baseline, making legacy assets inherit Latest and skip all deprecation.
	if (Ar.IsLoading()) { ResolveDataVersion(); }
#endif
}

bool UPCGExSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (PCGEX_CORE_SETTINGS.bToneDownOptionalPins && !InPin->Properties.IsRequiredPin() && !InPin->IsOutputPin())
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGExData::EIOInit UPCGExSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::NoInit;
}

bool UPCGExSettings::GetForceOffThreadPrepare(const FPCGExContext* InContext) const
{
	return PCGExSettingsCVars::bForceOffThread || bForceOffThreadPrepare || (PCGEX_CORE_SETTINGS.bRuntimeAlwaysOffThread && InContext->IsRuntimeGen());
}

bool UPCGExSettings::GetForceOffThreadExecute(const FPCGExContext* InContext) const
{
	return PCGExSettingsCVars::bForceOffThread || bForceOffThreadExecute || (PCGEX_CORE_SETTINGS.bRuntimeAlwaysOffThread && InContext->IsRuntimeGen());
}

bool UPCGExSettings::WantsResourcesCached() const
{
	PCGEX_GET_OPTION_STATE(CacheLoadedResources, bCacheLoadedResources)
}

#if WITH_EDITOR
void UPCGExSettings::EDITOR_OpenNodeDocumentation() const
{
	// Node pages live at <owning plugin DocsURL>/node-library/<PCGExNodeLibraryDoc>; companion plugins ship their own book.
	FString BaseURL = TEXT("https://pcgex.gitbook.io/pcgex");

	FStringView ModuleName;
	if (FPackageName::TryConvertScriptPackageNameToModuleName(WriteToString<256>(GetClass()->GetOutermost()->GetFName()), ModuleName))
	{
		const TSharedPtr<IPlugin> OwnerPlugin = IPluginManager::Get().GetModuleOwnerPlugin(FName(ModuleName));
		if (OwnerPlugin && !OwnerPlugin->GetDescriptor().DocsURL.IsEmpty()) { BaseURL = OwnerPlugin->GetDescriptor().DocsURL; }

#if PCGEX_PRO_BUNDLE
		// Merged bundle: every module resolves to the umbrella plugin, whose DocsURL is the
		// root book. PCGEx Pack records each merged module's original book in the
		// [ModuleDocsURLs] section of Config/PCGExBundle.ini -- restore it from there.
		// The function-local static means the file is read and parsed ONCE, on the first
		// docs click of the session; every later call is a plain TMap lookup.
		if (OwnerPlugin)
		{
			static const TMap<FName, FString> ModuleDocsURLs = [&OwnerPlugin]
			{
				TMap<FName, FString> URLs;
				TArray<FString> Lines;
				FFileHelper::LoadFileToStringArray(Lines, *(OwnerPlugin->GetBaseDir() / TEXT("Config/PCGExBundle.ini")));
				bool bInSection = false;
				for (FString& Line : Lines)
				{
					Line.TrimStartAndEndInline();
					if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#"))) { continue; }
					if (Line.StartsWith(TEXT("["))) { bInSection = Line.Equals(TEXT("[ModuleDocsURLs]")); continue; }
					FString Key;
					FString Value;
					if (bInSection && Line.Split(TEXT("="), &Key, &Value)) { URLs.Add(FName(Key.TrimStartAndEnd()), Value.TrimStartAndEnd()); }
				}
				return URLs;
			}();

			if (const FString* ModuleDocsURL = ModuleDocsURLs.Find(FName(ModuleName))) { BaseURL = *ModuleDocsURL; }
		}
#endif
	}

	BaseURL.RemoveFromEnd(TEXT("/"));

	const FString URL = BaseURL + TEXT("/node-library/") + GetClass()->GetMetaData(TEXT("PCGExNodeLibraryDoc"));
	FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
}
#endif

bool UPCGExSettings::SupportsDataStealing() const
{
	return false;
}

bool UPCGExSettings::ShouldCache() const
{
	if (!IsCacheable())
	{
		return false;
	}
	PCGEX_GET_OPTION_STATE(CacheData, bDefaultCacheNodeOutput)
}

bool UPCGExSettings::WantsScopedAttributeGet() const
{
	PCGEX_GET_OPTION_STATE(ScopedAttributeGet, bDefaultScopedAttributeGet)
}

bool UPCGExSettings::WantsBulkInitData() const
{
	PCGEX_GET_OPTION_STATE(BulkInitData, bBulkInitData)
}

#undef LOCTEXT_NAMESPACE
