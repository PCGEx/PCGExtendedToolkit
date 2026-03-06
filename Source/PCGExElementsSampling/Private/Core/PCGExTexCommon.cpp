// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExTexCommon.h"


#include "Data/PCGTextureData.h"
#include "Materials/MaterialInterface.h"
#include "TextureResource.h"
#include "Core/PCGExTexParamFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCreateTexParam"
#define PCGEX_NAMESPACE PCGExCreateTexParam

namespace PCGExTexture
{
	FString FReference::GetTag() const
	{
		if (TextureIndex < 0) { return FString::Printf(TEXT("%ls"), *TexturePath.ToString()); }
		return FString::Printf(TEXT("%ls:%d"), *TexturePath.ToString(), TextureIndex);
	}

	bool FLookup::BuildFrom(FPCGExContext* InContext, const FName InPin)
	{
		if (!PCGExFactories::GetInputFactories(InContext, InPin, Factories, {PCGExFactories::EType::TexParam}))
		{
			return false;
		}

		for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Factories) { PCGEX_VALIDATE_NAME_C(InContext, Factory->Config.TextureIDAttributeName) }
		return true;
	}

	bool FLookup::BuildFrom(const TArray<TObjectPtr<const UPCGExTexParamFactoryData>>& InFactories)
	{
		if (InFactories.IsEmpty()) { return false; }
		Factories.Reserve(InFactories.Num());
		for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : InFactories) { Factories.Add(Factory); }
		return true;
	}

	void FLookup::PrepareForWrite(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade> InDataFacade)
	{
		Buffers.Reserve(Factories.Num());

		for (const TObjectPtr<const UPCGExTexParamFactoryData>& Factory : Factories)
		{
			if (!Factory.Get()) { continue; }
			TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> Buffer = InDataFacade->GetWritable<FSoftObjectPath>(Factory->Config.TextureIDAttributeName, FSoftObjectPath(), true, PCGExData::EBufferInit::Inherit);
			Buffers.Add(Buffer);
		}
	}

	void FLookup::ExtractParams(const int32 PointIndex, const UMaterialInterface* InMaterial) const
	{
		if (!InMaterial)
		{
			for (int i = 0; i < Factories.Num(); i++)
			{
				if (!Buffers[i]) { continue; }
				Buffers[i]->SetValue(PointIndex, FSoftObjectPath{});
			}

			return;
		}

		for (int i = 0; i < Factories.Num(); i++)
		{
			if (!Buffers[i]) { continue; }

			const TObjectPtr<const UPCGExTexParamFactoryData>& Factory = Factories[i];

			if (UTexture* FoundTexture = nullptr; InMaterial->GetTextureParameterValue(Factory->Infos, FoundTexture))
			{
				Buffers[i]->SetValue(PointIndex, FSoftObjectPath(FoundTexture->GetPathName()));
			}
		}
	}

	void FLookup::ExtractReferences(const UMaterialInterface* InMaterial, TSet<FReference>& References) const
	{
		if (!InMaterial) { return; }

		for (int i = 0; i < Factories.Num(); i++)
		{
			const TObjectPtr<const UPCGExTexParamFactoryData>& Factory = Factories[i];
			if (UTexture* FoundTexture = nullptr; InMaterial->GetTextureParameterValue(Factory->Infos, FoundTexture))
			{
				References.Add(FReference(FoundTexture->GetPathName(), Factory->Config.TextureIndex));
			}
		}
	}

	void FLookup::ExtractParamsAndReferences(const int32 PointIndex, const UMaterialInterface* InMaterial, TSet<FReference>& References) const
	{
		if (!InMaterial)
		{
			for (int i = 0; i < Factories.Num(); i++)
			{
				if (!Buffers[i]) { continue; }
				Buffers[i]->SetValue(PointIndex, FSoftObjectPath{});
			}

			return;
		}

		for (int i = 0; i < Factories.Num(); i++)
		{
			if (!Buffers[i]) { continue; }

			const TObjectPtr<const UPCGExTexParamFactoryData>& Factory = Factories[i];

			if (UTexture* FoundTexture = nullptr; InMaterial->GetTextureParameterValue(Factory->Infos, FoundTexture))
			{
				Buffers[i]->SetValue(PointIndex, FSoftObjectPath(FoundTexture->GetPathName()));
				References.Add(FReference(FoundTexture->GetPathName(), Factory->Config.TextureIndex));
			}
		}
	}

	void FLookup::BuildMapFrom(FPCGExContext* InContext, const FName InPin)
	{
		// Process all input texture data
		// and store as a map. Take all data tags & texture path
		// This is a very blind approach, but also allows a degree of flexibility as to what can be used to re-associate attribute value with a texture data

		TArray<FPCGTaggedData> TaggedTexData = InContext->InputData.GetInputsByPin(InPin);
		for (const FPCGTaggedData& TaggedData : TaggedTexData)
		{
			const UPCGTexture2DSingleBaseData* BaseTextureData = Cast<UPCGTexture2DSingleBaseData>(TaggedData.Data);
			if (!BaseTextureData) { continue; }

			if (const UPCGTextureData* TextureData = Cast<UPCGTextureData>(BaseTextureData))
			{
				if (TextureData->Texture.IsValid(false, true))
				{
					// Use existing texture path as lookup, since we can.
					TextureDataMap.Add(TextureData->Texture->GetPathName(), BaseTextureData);
				}
			}

			// Resort to parsing tags
			for (const FString& Tag : TaggedData.Tags)
			{
				if (Tag.StartsWith(TexTag_Str))
				{
					FString Path = Tag.Mid(TexTag_Str.Len());
					TextureDataMap.Add(Path, BaseTextureData);
				}
				else
				{
					TextureDataMap.Add(Tag, BaseTextureData);
				}
			}
		}
	}

	const UPCGTexture2DSingleBaseData* FLookup::TryGetTextureData(const FString& InPath) const
	{
		const UPCGTexture2DSingleBaseData* const* Ptr = TextureDataMap.Find(InPath);
		return Ptr ? *Ptr : nullptr;
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
