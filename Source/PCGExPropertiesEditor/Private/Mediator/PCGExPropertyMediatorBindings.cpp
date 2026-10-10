// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Mediator/PCGExPropertyMediatorBindings.h"

#include "PCGExMediatorRegistry.h"
#include "PCGExPropertySchemaAsset.h"
#include "Elements/PCGExTuple.h"
#include "Mediator/PCGExPropertyMediatorHooks.h"
#include "UObject/UnrealType.h"

void PCGExPropertyMediatorBindings::Register()
{
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExPropertySchemaAsset::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExPropertySchemaAsset, Collection)};
		Binding.Summary = TEXT("Property Schema asset.");
		FPCGExMediatorRegistry::RegisterBinding(Binding);
	}
	{
		FPCGExMediatorBinding Binding;
		Binding.HostClass = UPCGExTupleSettings::StaticClass();
		Binding.Members = {GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Composition), GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values)};
		// Rows decode against the composition applied just before them, so fresh rows resolve names and types.
		Binding.WrapImport = [](UObject* Host, const FName Member, TFunctionRef<void()> Import)
		{
			const UPCGExTupleSettings* Tuple = Cast<UPCGExTupleSettings>(Host);
			if (Tuple && Member == GET_MEMBER_NAME_CHECKED(UPCGExTupleSettings, Values))
			{
				PCGExPropertyMediator::FOverridesSchemaScope Scope(Tuple->Composition.BuildSchema());
				Import();
				return;
			}
			Import();
		};
		Binding.Summary = TEXT("Tuple node: composition schema and value rows.");
		FPCGExMediatorRegistry::RegisterBinding(Binding);
	}
}

void PCGExPropertyMediatorBindings::Unregister()
{
	FPCGExMediatorRegistry::UnregisterBinding(UPCGExPropertySchemaAsset::StaticClass());
	FPCGExMediatorRegistry::UnregisterBinding(UPCGExTupleSettings::StaticClass());
}
