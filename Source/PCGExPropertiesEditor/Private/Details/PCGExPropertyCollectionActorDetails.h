// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ActorDetailsDelegates.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;

/**
 * Level-actor details integration for UPCGExPropertyCollectionComponent.
 *
 * Selecting an actor that hosts a property collection surfaces the collection's VALUES directly
 * in the actor's details panel, under Transform, without selecting the component: the same rows
 * the component's own panel renders in values-only mode, hoisted through the engine's
 * OnExtendActorDetails hook. The schema itself stays editable only on the component.
 */
namespace PCGExPropertyCollectionActorDetails
{
	/** Instance-metadata key the hoist sets on the collection handle; the collection customization
	 *  reads it to render values-only with an empty header (rows land flat under the category). */
	inline FName ValuesOnlyMetaKey()
	{
		static const FName Key(TEXT("PCGExValuesOnly"));
		return Key;
	}

	/** Bound to OnExtendActorDetails. Single-actor selections only: the collection customization
	 *  edits one host, so multi-actor selections get no section rather than a half-working one. */
	void ExtendActorDetails(IDetailLayoutBuilder& DetailLayout, const FGetSelectedActors& GetSelectedActors);
}

/**
 * Class layout for the component itself. Only one property collection per actor is supported
 * (every consumer reads the one FindOnActor returns); a second component on the same actor
 * replaces its Properties rows with a warning naming the component that actually applies.
 */
class FPCGExPropertyCollectionComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
