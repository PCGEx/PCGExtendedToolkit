// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExCollectionsMediatorBindings.h"

#include "PCGExMediatorRegistry.h"
#include "Collections/PCGExOmniCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExAssetCollectionTypes.h"
#include "Elements/PCGExDistributeTuple.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "UObject/UnrealType.h"

void PCGExCollectionsMediatorBindings::Register()
{
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExAssetCollection::StaticClass();
		Binding.Members = {
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties),
			GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CategoryOverrides),
			PCGExAssetCollection::EntriesPropertyName
		};
		// Category rows and entry overrides decode against the schema applied just before them.
		Binding.WrapImport = [](UObject* Host, const FName Member, TFunctionRef<void()> Import)
		{
			const UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Host);
			if (Collection && Member != GET_MEMBER_NAME_CHECKED(UPCGExAssetCollection, CollectionProperties))
			{
				PCGExPropertyMediator::FOverridesSchemaScope Scope(Collection->CollectionProperties.BuildSchema());
				Import();
				return;
			}
			Import();
		};
		// A heterogeneous host needs its per-type globals and machinery for every type the rows now carry.
		Binding.PostImport = [](UObject* Host)
		{
			if (UPCGExOmniCollection* Omni = Cast<UPCGExOmniCollection>(Host)) { Omni->EDITOR_EnsureTypeSetup(); }
		};
		Binding.Summary = TEXT("Asset collection: property schema, category overrides and entries (typed, Variant or Omni host).");
		FPCGExMediatorRegistry::RegisterBinding(Binding);
	}
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExDistributeTupleSettings::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values)};
		Binding.WrapImport = [](UObject* Host, const FName Member, TFunctionRef<void()> Import)
		{
			const UPCGExDistributeTupleSettings* Tuple = Cast<UPCGExDistributeTupleSettings>(Host);
			if (Tuple && Member == GET_MEMBER_NAME_CHECKED(UPCGExDistributeTupleSettings, Values))
			{
				PCGExPropertyMediator::FOverridesSchemaScope Scope(Tuple->Composition.BuildSchema());
				Import();
				return;
			}
			Import();
		};
		Binding.Summary = TEXT("Distribute Tuple node: composition schema and weighted value rows.");
		FPCGExMediatorRegistry::RegisterBinding(Binding);
	}
}

void PCGExCollectionsMediatorBindings::Unregister()
{
	FPCGExMediatorRegistry::UnregisterBinding(UPCGExAssetCollection::StaticClass());
	FPCGExMediatorRegistry::UnregisterBinding(UPCGExDistributeTupleSettings::StaticClass());
}
