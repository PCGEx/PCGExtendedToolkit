// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorPropertyOverrides.h"

#include "PCGContext.h"
#include "PCGData.h"
#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "PCGExVersion.h"
#include "PCGModule.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExBulkAttributeHelpers.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "UObject/UnrealType.h"

#if PCGEX_ENGINE_VERSION > 507
#include "Helpers/PCGPropertyHelpers.h"
#include "Metadata/PCGMetadataContainerTypes.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif

namespace PCGExActorOverrides
{
	using FComponents = TArray<UActorComponent*, TInlineAllocator<8>>;

	// The property lookup PCGAttributeAccessorHelpers::GetPropertyChain performs on this engine version.
	const FProperty* FindChainProperty(const UStruct* InStruct, const FName InName)
	{
#if PCGEX_ENGINE_VERSION > 507
		return PCGPropertyHelpers::FindPropertyByName(InStruct, InName);
#else
		const UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(InStruct);
		if (!UserStruct)
		{
			return FindFProperty<FProperty>(InStruct, InName);
		}

		// Body of PCGPropertyHelpers::FindPropertyInUserDefinedStruct, which the engine does not export before 5.8.
		TArray<FName, TInlineAllocator<2>> NamesToLookFor = {InName};
		if (!InName.IsValidXName())
		{
			NamesToLookFor.Add(MakeObjectNameFromDisplayLabel(InName.ToString(), NAME_None));
		}

		for (TFieldIterator<const FProperty> PropIt(UserStruct, EFieldIterationFlags::IncludeSuper); PropIt; ++PropIt)
		{
			const FString AuthoredName = UserStruct->GetAuthoredNameForField(*PropIt);
			const FName PropertyName = *AuthoredName;
			if (NamesToLookFor.Contains(PropertyName)
				|| (!PropertyName.IsValidXName() && NamesToLookFor.Contains(MakeObjectNameFromDisplayLabel(AuthoredName, NAME_None))))
			{
				return *PropIt;
			}
		}

		return nullptr;
#endif
	}

	// Whether the engine feeds InProperty a soft path, which is what it turns a string source into.
	bool TakesPath(const FProperty* InProperty)
	{
		if (InProperty->IsA<FObjectPropertyBase>())
		{
			return true;
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty);
		return StructProperty
			&& (StructProperty->Struct == TBaseStructure<FSoftObjectPath>::Get() || StructProperty->Struct == TBaseStructure<FSoftClassPath>::Get());
	}

	// Walks InPropertyTarget on InClass the way PCGAttributeAccessorHelpers::GetPropertyChain does, minus its
	// LogPCG errors. False means the engine would reject the target on that class.
	bool ResolveTarget(const UClass* InClass, const FString& InPropertyTarget, FResolvedTarget& OutTarget, PCGExHelpers::FStructLayoutStamp& OutLayout)
	{
		// FName construction asserts past NAME_SIZE, and the engine builds its selector from this string unguarded.
		if (InPropertyTarget.IsEmpty() || InPropertyTarget.Len() >= NAME_SIZE)
		{
			return false;
		}

		const FPCGAttributePropertySelector Selector = FPCGAttributePropertySelector::CreateSelectorFromString(InPropertyTarget);
		const TArray<FString>& ExtraNames = Selector.GetExtraNames();

		TArray<FName, TInlineAllocator<4>> PropertyNames;
#if PCGEX_ENGINE_VERSION > 507
		PropertyNames.Add(Selector.GetAttributeName());
#else
		// Before 5.8 the engine looks an undotted target up by its raw string, whatever the selector made of it.
		PropertyNames.Add(ExtraNames.IsEmpty() ? FName(*InPropertyTarget) : Selector.GetAttributeName());
#endif
		for (const FString& ExtraName : ExtraNames)
		{
			PropertyNames.Add(FName(*ExtraName));
		}

		const UStruct* CurrentStruct = InClass;
		const FProperty* Property = nullptr;
		bool bThroughObject = false;

		const int32 LastIndex = PropertyNames.Num() - 1;
		for (int32 i = 0; i <= LastIndex; i++)
		{
			if (!CurrentStruct)
			{
				return false;
			}

			// Stamped before the lookup: a struct that lacks the property today may have it once recompiled.
			OutLayout.Add(CurrentStruct);

			Property = FindChainProperty(CurrentStruct, PropertyNames[i]);
			if (!Property)
			{
				return false;
			}

			OutTarget.Chain.Add(Property);

#if PCGEX_ENGINE_VERSION > 507
			// 5.8 chains continue through an array's inner property.
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				Property = ArrayProperty->Inner;
				OutTarget.Chain.Add(Property);
			}
#endif

			if (i == LastIndex)
			{
				break;
			}

			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				CurrentStruct = StructProperty->Struct;
			}
			else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				CurrentStruct = ObjectProperty->PropertyClass;
				bThroughObject = true;
			}
			else
			{
				return false;
			}
		}

		// Same test the engine uses to flag an override as needing its value loaded.
		TArray<const FStructProperty*> EncounteredStructProps;
		OutTarget.bHoldsObjectReference = Property->ContainsObjectReference(EncounteredStructProps, EPropertyObjectReferenceType::Strong);
		OutTarget.bTakesPath = TakesPath(Property);
		OutTarget.bActorState = !bThroughObject && OutTarget.Chain[0]->GetOwnerClass() == AActor::StaticClass();

		return true;
	}

	// Follows InChain from InContainer the way the engine accessors do, collecting the deepest component on each path
	// the write takes. False when an object on the way to the leaf, or to the first array, is null: the engine asserts
	// there before 5.8, and its array wrapper still does. A null object inside an array element is the engine's to skip.
	bool WalkChain(void* InContainer, UActorComponent* InDeepest, const TArray<const FProperty*>& InChain, const int32 InStart, FComponents& OutComponents)
	{
		void* Container = InContainer;
		UActorComponent* Deepest = InDeepest;

		const int32 LeafIndex = InChain.Num() - 1;
		for (int32 i = InStart; i < LeafIndex; i++)
		{
			const FProperty* Property = InChain[i];

			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				// The write resizes the array, so it lands here whatever the elements hold.
				if (Deepest)
				{
					OutComponents.AddUnique(Deepest);
				}

				// Past the inner property, the chain continues inside each element.
				if (i + 1 < LeafIndex)
				{
					FScriptArrayHelper Elements(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Container));
					for (int32 e = 0; e < Elements.Num(); e++)
					{
						WalkChain(Elements.GetRawPtr(e), Deepest, InChain, i + 1, OutComponents);
					}
				}

				return true;
			}

			if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				UObject* Object = ObjectProperty->GetObjectPropertyValue_InContainer(Container);
				if (!Object)
				{
					return false;
				}

				Container = Object;
				if (UActorComponent* Component = Cast<UActorComponent>(Object))
				{
					Deepest = Component;
				}
			}
			else
			{
				Container = Property->ContainerPtrToValuePtr<void>(Container);
			}
		}

		if (Deepest)
		{
			OutComponents.AddUnique(Deepest);
		}

		return true;
	}

	// Whether InAccessor's values can be read as soft object paths, and what they are read from.
	bool ReadsAsPath(const IPCGAttributeAccessor& InAccessor, bool& bOutIsString, bool& bOutIsArray)
	{
		// Same test the accessor applies to a read that allows broadcast and construction.
#if PCGEX_ENGINE_VERSION > 507
		const FPCGMetadataAttributeDesc& SourceDesc = InAccessor.GetUnderlyingDesc();
		bOutIsString = SourceDesc.ValueType == EPCGMetadataTypes::String;
		bOutIsArray = SourceDesc.IsArray();

		return PCG::Private::IsBroadcastableOrConstructible(
			SourceDesc, bOutIsArray ? PCG::Private::GetDefaultAttributeDesc<TArray<FSoftObjectPath>>() : PCG::Private::GetDefaultAttributeDesc<FSoftObjectPath>());
#else
		const uint16 SourceType = static_cast<uint16>(InAccessor.GetUnderlyingType());
		bOutIsString = SourceType == static_cast<uint16>(EPCGMetadataTypes::String);
		bOutIsArray = false;

		return PCG::Private::IsBroadcastableOrConstructible(SourceType, static_cast<uint16>(PCG::Private::MetadataTypes<FSoftObjectPath>::Id));
#endif
	}

	// Calls InVisit(PointIndex, Value) for each value InAccessor holds over [InStart, InStart + InCount): one per
	// point, or as many as the point's array has.
	template <typename T, typename VisitType>
	void ForEachValue(const IPCGAttributeAccessor& InAccessor, const IPCGAttributeAccessorKeys& InKeys, const bool bInIsArray, const int32 InStart, const int32 InCount, VisitType&& InVisit)
	{
		const EPCGAttributeAccessorFlags Flags = EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible;

#if PCGEX_ENGINE_VERSION > 507
		if (bInIsArray)
		{
			TArray<PCG::TPCGArrayAccessorWrapper<T>> Arrays;
			Arrays.SetNum(InCount);
			if (InAccessor.GetRange<PCG::TPCGArrayAccessorWrapper<T>>(Arrays, InStart, InKeys, Flags))
			{
				for (int32 i = 0; i < InCount; i++)
				{
					for (const T& Value : Arrays[i].GetView())
					{
						InVisit(InStart + i, Value);
					}
				}
			}
			return;
		}
#endif

		TArray<T> Values;
		Values.SetNum(InCount);
		if (InAccessor.GetRange<T>(Values, InStart, InKeys, Flags))
		{
			for (int32 i = 0; i < InCount; i++)
			{
				InVisit(InStart + i, Values[i]);
			}
		}
	}

#pragma region FOverrideTargets

	FOverrideTargets::FOverrideTargets(FPCGContext* InContext, const TArray<FPCGObjectPropertyOverrideDescription>& InDescriptions, const bool bInQuietMissingTargets)
		: Context(InContext)
		  , Descriptions(InDescriptions)
		  , bQuietMissingTargets(bInQuietMissingTargets)
	{
	}

	TSharedRef<FClassTargets> FOverrideTargets::Resolve(const UClass* InClass)
	{
		check(IsInGameThread());

		const TWeakObjectPtr<const UClass> ClassKey(InClass);
		if (const TSharedRef<FClassTargets>* Found = ClassTargets.Find(ClassKey))
		{
			if ((*Found)->Layout.IsCurrent())
			{
				return *Found;
			}
		}

		const TSharedRef<FClassTargets> Resolved = MakeShared<FClassTargets>();
		Resolved->Layout.Add(InClass);

		for (int32 i = 0; i < Descriptions.Num(); i++)
		{
			FResolvedTarget Target;
			Target.Description = i;

			if (ResolveTarget(InClass, Descriptions[i].PropertyTarget, Target, Resolved->Layout))
			{
				Resolved->Targets.Add(MoveTemp(Target));
			}
			else if (!bQuietMissingTargets)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Property override target '{0}' could not be resolved on actor class '{1}' and is skipped for these actors. If this is expected (e.g. a collection mixing actor classes), you can quiet this warning in the settings."),
					           FText::FromString(Descriptions[i].PropertyTarget), FText::FromName(InClass->GetFName())));
			}
		}

		ClassTargets.Add(ClassKey, Resolved);
		return Resolved;
	}

#pragma endregion

#pragma region FActorPropertyOverrides

	FActorPropertyOverrides::FActorPropertyOverrides(const TSharedRef<FOverrideTargets>& InTargets, const UPCGData* InSourceData)
		: Targets(InTargets)
		  , SourceData(InSourceData)
	{
		const TArray<FPCGObjectPropertyOverrideDescription>& Descriptions = Targets->GetDescriptions();
		SourceReaders.SetNum(Descriptions.Num());

		for (int32 i = 0; i < Descriptions.Num(); i++)
		{
			// Quiet: a missing source is reported by the engine when the override gets bound to a class.
			const FPCGAttributePropertyInputSelector Selector = Descriptions[i].InputSource.CopyAndFixLast(SourceData);
			TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(SourceData, Selector, /*bQuiet=*/true);

			FSourceReader& Reader = SourceReaders[i];
			if (!Accessor || !ReadsAsPath(*Accessor, Reader.bIsString, Reader.bIsArray))
			{
				continue;
			}

			TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(SourceData, Selector);
			if (!Keys)
			{
				continue;
			}

			Reader.Accessor = MoveTemp(Accessor);
			Reader.Keys = MoveTemp(Keys);
			bHasPreloadableSources = true;
		}
	}

	FActorPropertyOverrides::~FActorPropertyOverrides() = default;

	void FActorPropertyOverrides::PrepareClass(const UClass* InClass, TArray<int32>& OutPreloadSources)
	{
		OutPreloadSources.Reset();

		const FBoundClass& Bound = FindOrBind(InClass);
		for (const FBoundOverride& BoundOverride : Bound.Overrides)
		{
			const FResolvedTarget& Target = Bound.Targets->Targets[BoundOverride.Target];
			if (Target.bHoldsObjectReference && SourceReaders[Target.Description].Accessor)
			{
				OutPreloadSources.AddUnique(Target.Description);
			}
		}
	}

	void FActorPropertyOverrides::GatherPreloadPaths(const int32 InSource, const int32 InStart, const int32 InCount, const TFunctionRef<bool(int32)> InFilter, TSet<FSoftObjectPath>& OutPaths) const
	{
		const FSourceReader& Reader = SourceReaders[InSource];

		const auto Keep = [&InFilter, &OutPaths](const int32 PointIndex, const FSoftObjectPath& Path)
		{
			if (Path.IsAsset() && InFilter(PointIndex))
			{
				OutPaths.Add(Path);
			}
		};

		// Not the engine's GatherAllOverridesToLoad: it reads with strict types, so it never sees a path held as a string.
		if (Reader.bIsString)
		{
			// Converted here rather than by the accessor: FSoftObjectPath asserts on a package name of NAME_SIZE or more.
			ForEachValue<FString>(
				*Reader.Accessor, *Reader.Keys, Reader.bIsArray, InStart, InCount,
				[&Keep](const int32 PointIndex, const FString& Value)
				{
					Keep(PointIndex, PCGExData::Helpers::MakePathChecked(Value));
				});
		}
		else
		{
			ForEachValue<FSoftObjectPath>(*Reader.Accessor, *Reader.Keys, Reader.bIsArray, InStart, InCount, Keep);
		}
	}

	bool FActorPropertyOverrides::HasOversizedString(const int32 InSource, const int32 InPointIndex) const
	{
		const FSourceReader& Reader = SourceReaders[InSource];
		if (!Reader.Accessor || !Reader.bIsString)
		{
			return false;
		}

		bool bOversized = false;
		ForEachValue<FString>(
			*Reader.Accessor, *Reader.Keys, Reader.bIsArray, InPointIndex, 1,
			[&bOversized](const int32, const FString& Value)
			{
				bOversized |= Value.Len() >= NAME_SIZE;
			});

		return bOversized;
	}

	bool FActorPropertyOverrides::Apply(PCGExActorDelta::FScopedActorWrite& InScope, const int32 InPointIndex)
	{
		AActor* Actor = InScope.GetActor();
		const UClass* ActorClass = Actor->GetClass();

		FBoundClass& Bound = FindOrBind(ActorClass);
		if (Bound.Overrides.IsEmpty())
		{
			return false;
		}

		const TArray<FPCGObjectPropertyOverrideDescription>& Descriptions = Targets->GetDescriptions();
		FPCGContext* Context = Targets->GetContext();

		FPCGAttributeAccessorKeysSingleObjectPtr<AActor> OutputKey(Actor);

		// The PCG point is the source of truth for the actor's transform: what an override writes on the root's is undone.
		USceneComponent* Root = Actor->GetRootComponent();
		bool bRootSaved = false;
		FVector RootLocation = FVector::ZeroVector;
		FRotator RootRotation = FRotator::ZeroRotator;
		FVector RootScale = FVector::OneVector;

		FComponents Components;
		bool bWrote = false;

		for (FBoundOverride& BoundOverride : Bound.Overrides)
		{
			FResolvedTarget& Target = Bound.Targets->Targets[BoundOverride.Target];

			Components.Reset();
			if (!WalkChain(Actor, nullptr, Target.Chain, 0, Components))
			{
				if (!Target.bUnreachableReported && !Targets->IsQuietAboutMissingTargets())
				{
					Target.bUnreachableReported = true;
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
						           FTEXT("Property override target '{0}' goes through an object that is not set on some actors of class '{1}', and is skipped for those. If this is expected, you can quiet this warning in the settings."),
						           FText::FromString(Descriptions[Target.Description].PropertyTarget), FText::FromName(ActorClass->GetFName())));
				}
				continue;
			}

			// The engine converts a string into the path it writes, and FSoftObjectPath asserts on an oversized one.
			if (Target.bTakesPath && HasOversizedString(Target.Description, InPointIndex))
			{
				if (!Target.bOversizedReported)
				{
					Target.bOversizedReported = true;
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
						           FTEXT("Property override target '{0}' was given a string too long to be an asset path for some actors of class '{1}', and is skipped for those."),
						           FText::FromString(Descriptions[Target.Description].PropertyTarget), FText::FromName(ActorClass->GetFName())));
				}
				continue;
			}

			for (UActorComponent* Component : Components)
			{
				if (Component == Root && !bRootSaved)
				{
					bRootSaved = true;
					RootLocation = Root->GetRelativeLocation();
					RootRotation = Root->GetRelativeRotation();
					RootScale = Root->GetRelativeScale3D();
				}

				// The engine accessors write raw property memory: a registered component only picks that up by re-registering.
				InScope.Touch(Component);
			}

			if (BoundOverride.Override.Apply(InPointIndex, OutputKey))
			{
				bWrote = true;
				if (Target.bActorState)
				{
					InScope.RequestActorRefresh();
				}
			}
			else if (!Target.bFailureReported)
			{
				Target.bFailureReported = true;
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
					           FTEXT("Property override target '{0}' failed to apply on some actors of class '{1}'."),
					           FText::FromString(Descriptions[Target.Description].PropertyTarget), FText::FromName(ActorClass->GetFName())));
			}
		}

		if (bRootSaved)
		{
			// Before the scope re-registers the root, which is what recomputes its world transform.
			Root->SetRelativeLocation_Direct(RootLocation);
			Root->SetRelativeRotation_Direct(RootRotation);
			Root->SetRelativeScale3D_Direct(RootScale);
		}

		if (bWrote)
		{
			InScope.RequestFixups();
		}

		return bWrote;
	}

	FActorPropertyOverrides::FBoundClass& FActorPropertyOverrides::FindOrBind(const UClass* InClass)
	{
		check(IsInGameThread());

		TUniquePtr<FBoundClass>& Bound = BoundClasses.FindOrAdd(TWeakObjectPtr<const UClass>(InClass));
		if (Bound && Bound->Targets->Layout.IsCurrent())
		{
			return *Bound;
		}

		// First use, or a recompile destroyed the properties the engine overrides below were built on.
		Bound = MakeUnique<FBoundClass>(Targets->Resolve(InClass));

		const TArray<FPCGObjectPropertyOverrideDescription>& Descriptions = Targets->GetDescriptions();
		const TArray<FResolvedTarget>& ClassTargets = Bound->Targets->Targets;

		Bound->Overrides.Reserve(ClassTargets.Num());
		for (int32 i = 0; i < ClassTargets.Num(); i++)
		{
			const FPCGObjectPropertyOverrideDescription& Description = Descriptions[ClassTargets[i].Description];
			const FPCGAttributePropertyInputSelector Selector = Description.InputSource.CopyAndFixLast(SourceData);

			FBoundOverride BoundOverride;
			BoundOverride.Target = i;

#if PCGEX_ENGINE_VERSION > 507
			FPCGObjectSingleOverride::FInitializeParams Params;
			Params.InputSelector = &Selector;
			Params.OutputProperty = Description.PropertyTarget;
			Params.TemplateClass = InClass;
			Params.SourceData = SourceData;
			Params.OptionalContext = Targets->GetContext();
			BoundOverride.Override.Initialize(Params);
#else
			BoundOverride.Override.Initialize(Selector, Description.PropertyTarget, InClass, SourceData, Targets->GetContext());
#endif

			// An override the engine turned down has said why on its own.
			if (BoundOverride.Override.IsValid())
			{
				Bound->Overrides.Add(MoveTemp(BoundOverride));
			}
		}

		return *Bound;
	}

#pragma endregion
}
