// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGExVersion.h"

#if PCGEX_ENGINE_VERSION < 507
#include "MaterialTypes.h"
#else
#include "Materials/MaterialParameters.h"
#endif
#include "Factories/PCGExFactoryProvider.h"

class UPCGTexture2DSingleBaseData;
class UMaterialInterface;
class UPCGExTexParamFactoryData;

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

namespace PCGExTexture
{
	struct PCGEXELEMENTSSAMPLING_API FReference
	{
		FSoftObjectPath TexturePath = FSoftObjectPath();
		int32 TextureIndex = -1;

		FReference()
		{
		}

		explicit FReference(const FSoftObjectPath& InTexturePath, const int32 InTextureIndex = -1)
			: TexturePath(InTexturePath), TextureIndex(InTextureIndex)
		{
		}

		FString GetTag() const;

		bool operator==(const FReference& Other) const { return TexturePath == Other.TexturePath && TextureIndex == Other.TextureIndex; }
		FORCEINLINE friend uint32 GetTypeHash(const FReference& Key) { return HashCombineFast(GetTypeHash(Key.TexturePath), Key.TextureIndex); }
	};

	class FLookup : public TSharedFromThis<FLookup>
	{
		TMap<FString, const UPCGTexture2DSingleBaseData*> TextureDataMap;

	public:
		FLookup()
		{
		}

		TArray<TObjectPtr<const UPCGExTexParamFactoryData>> Factories;
		TArray<TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>>> Buffers;

		bool BuildFrom(FPCGExContext* InContext, const FName InPin);
		bool BuildFrom(const TArray<TObjectPtr<const UPCGExTexParamFactoryData>>& InFactories);
		void PrepareForWrite(FPCGExContext* InContext, TSharedRef<PCGExData::FFacade> InDataFacade);

		void ExtractParams(const int32 PointIndex, const UMaterialInterface* InMaterial) const;
		void ExtractReferences(const UMaterialInterface* InMaterial, TSet<FReference>& References) const;
		void ExtractParamsAndReferences(const int32 PointIndex, const UMaterialInterface* InMaterial, TSet<FReference>& References) const;

		void BuildMapFrom(FPCGExContext* InContext, const FName InPin);
		const UPCGTexture2DSingleBaseData* TryGetTextureData(const FString& InPath) const;
	};
}
