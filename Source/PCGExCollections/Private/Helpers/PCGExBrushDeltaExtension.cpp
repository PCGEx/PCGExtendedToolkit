// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExBrushDeltaExtension.h"

#if WITH_EDITOR

#include "BSPOps.h"
#include "Model.h"
#include "PCGExLog.h"
#include "Components/BrushComponent.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "UObject/Package.h"

namespace PCGExBrushDeltaExtension
{
	// Payload (version 1):
	//   [uint8 bRootOutside][int32 ActorPolyFlags][uint32 PolyCount]
	//   per poly: [uint32 PolyFlags][FVector3f Base][Normal][TextureU][TextureV][uint32 VertCount][FVector3f x VertCount]
	//             [FName ItemName]
	// Materials are left out: the editor nulls them on volumes. FPoly's constructor defaults every other field.
	constexpr uint32 PayloadVersion = 1;

	// Smallest poly record: flags, four vectors, a vertex count and an empty name.
	constexpr int64 MinPolyBytes = sizeof(uint32) + 4 * sizeof(FVector3f) + sizeof(uint32) + sizeof(int32);

	TArray<PCGExActorDelta::FDeltaExtensionHandle>& GetHandles()
	{
		static TArray<PCGExActorDelta::FDeltaExtensionHandle> Instance;
		return Instance;
	}

	int64 GetRemaining(FArchive& Ar)
	{
		return Ar.TotalSize() - Ar.Tell();
	}

	bool Capture(const UObject& Source, FArchive& Ar)
	{
		// Static brushes are BSP level geometry, not a shape of their own.
		const ABrush* Brush = Cast<ABrush>(&Source);
		if (!Brush || !(Brush->IsVolumeBrush() || Brush->IsBrushShape()))
		{
			return false;
		}

		const UModel* Model = Brush->Brush;
		if (!Model || !Model->Polys || Model->Polys->Element.IsEmpty())
		{
			return false;
		}

		uint8 bRootOutside = Model->RootOutside ? 1 : 0;
		int32 ActorPolyFlags = Brush->PolyFlags;
		uint32 PolyCount = Model->Polys->Element.Num();
		Ar << bRootOutside;
		Ar << ActorPolyFlags;
		Ar << PolyCount;

		for (const FPoly& Poly : Model->Polys->Element)
		{
			uint32 PolyFlags = Poly.PolyFlags;
			FVector3f Base = Poly.Base;
			FVector3f Normal = Poly.Normal;
			FVector3f TextureU = Poly.TextureU;
			FVector3f TextureV = Poly.TextureV;
			Ar << PolyFlags;
			Ar << Base;
			Ar << Normal;
			Ar << TextureU;
			Ar << TextureV;

			uint32 VertCount = Poly.Vertices.Num();
			Ar << VertCount;
			for (FVector3f Vertex : Poly.Vertices)
			{
				Ar << Vertex;
			}

			FName ItemName = Poly.ItemName;
			Ar << ItemName;
		}

		return true;
	}

	void Apply(UObject& Target, FArchive& Ar, const uint32 InPayloadVersion, PCGExActorDelta::FScopedActorWrite& InScope)
	{
		ABrush* Brush = Cast<ABrush>(&Target);
		UBrushComponent* BrushComponent = Brush ? Brush->GetBrushComponent() : nullptr;
		if (!BrushComponent)
		{
			return;
		}

		uint8 bRootOutside = 0;
		int32 ActorPolyFlags = 0;
		uint32 PolyCount = 0;
		Ar << bRootOutside;
		Ar << ActorPolyFlags;
		Ar << PolyCount;

		// Counts are checked against the bytes left before allocating, so a corrupt count cannot ask for gigabytes.
		if (PolyCount == 0 || static_cast<int64>(PolyCount) > GetRemaining(Ar) / MinPolyBytes)
		{
			Ar.SetError();
		}

		TArray<FPoly> Polys;
		if (!Ar.IsError())
		{
			Polys.SetNum(PolyCount);
		}

		for (FPoly& Poly : Polys)
		{
			Ar << Poly.PolyFlags;
			Ar << Poly.Base;
			Ar << Poly.Normal;
			Ar << Poly.TextureU;
			Ar << Poly.TextureV;

			uint32 VertCount = 0;
			Ar << VertCount;
			if (Ar.IsError() || static_cast<int64>(VertCount) > GetRemaining(Ar) / static_cast<int64>(sizeof(FVector3f)))
			{
				Ar.SetError();
				break;
			}

			Poly.Vertices.SetNumUninitialized(VertCount);
			for (FVector3f& Vertex : Poly.Vertices)
			{
				Ar << Vertex;
			}

			Ar << Poly.ItemName;
			Poly.Actor = Brush;
		}

		if (Ar.IsError())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[PCGExActorDelta] Malformed brush payload on %s; its shape is not restored."), *Brush->GetName());
			return;
		}

		// FBSPOps::csgPrepMovingBrush checks RootOutside.
		if (!bRootOutside)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[PCGExActorDelta] Brush payload on %s is not RootOutside; its shape is not restored."), *Brush->GetName());
			return;
		}

		// Every component, not only the brush one: siblings that read the brush when they register see the new shape.
		TInlineComponentArray<UActorComponent*> Components;
		Brush->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			InScope.Touch(Component);
		}

		if (UModel* Previous = Brush->Brush; Previous && Previous->GetOuter() == Brush)
		{
			Previous->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}

		// As UActorFactory::CreateBrushForVolumeActor builds a volume's brush.
		const EObjectFlags Flags = Brush->GetFlags() & (RF_Transactional | RF_Transient | RF_NonPIEDuplicateTransient);
		UModel* Model = NewObject<UModel>(Brush, NAME_None, Flags);
		Model->Initialize(nullptr, true);
		Model->Polys = NewObject<UPolys>(Model, NAME_None, Flags);
		Model->Polys->Element = MoveTemp(Polys);

		Brush->PolyFlags = ActorPolyFlags;
		Brush->Brush = Model;
		BrushComponent->Brush = Model;

		// BSP, simple collision and navigation, rebuilt from the polys.
		FBSPOps::csgPrepMovingBrush(Brush);
	}

	void Register()
	{
		PCGExActorDelta::FDeltaExtension BrushExtension;
		BrushExtension.Id = FName(TEXT("PCGEx.Brush"));
		BrushExtension.Version = PayloadVersion;
		BrushExtension.Capture = &Capture;
		BrushExtension.Apply = &Apply;
		BrushExtension.ClaimedProperties = {GET_MEMBER_NAME_CHECKED(ABrush, Brush)};
		GetHandles().Add(PCGExActorDelta::RegisterDeltaExtension(ABrush::StaticClass(), MoveTemp(BrushExtension)));

		// The actor's payload rebuilds both.
		PCGExActorDelta::FDeltaExtension ComponentClaims;
		ComponentClaims.ClaimedProperties = {GET_MEMBER_NAME_CHECKED(UBrushComponent, Brush), GET_MEMBER_NAME_CHECKED(UBrushComponent, BrushBodySetup)};
		GetHandles().Add(PCGExActorDelta::RegisterDeltaExtension(UBrushComponent::StaticClass(), MoveTemp(ComponentClaims)));
	}

	void Unregister()
	{
		GetHandles().Empty();
	}
}

#endif
