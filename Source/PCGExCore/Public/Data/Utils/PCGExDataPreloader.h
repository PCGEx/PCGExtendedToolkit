// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Core/PCGExMTCommon.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExAttributeIdentity.h"
#include "UObject/Object.h"

struct FPCGExContext;

namespace PCGExMT
{
	struct FScope;
	class FAsyncToken;
	class IAsyncHandleGroup;
	class FTaskManager;
}

struct FPCGContextHandle;

namespace PCGExData
{
	class FFacade;
	class IBuffer;

	namespace States
	{
		PCGEX_CTX_STATE(State_PreloadingData)
	}

	enum class EBufferPreloadType : uint8
	{
		RawAttribute = 0,
		BroadcastFromName,
		BroadcastFromSelector,
	};

	struct PCGEXCORE_API FReadableBufferConfig
	{
	protected:
		FRWLock ReaderLock;
		int8 Status = 0;
		TWeakPtr<IBuffer> WeakReader;

	public:
		EBufferPreloadType Mode = EBufferPreloadType::RawAttribute;
		FPCGAttributePropertyInputSelector Selector;
		FAttributeIdentity Identity;
		// Read every value up front (a consumer that reads unscoped would otherwise re-read the whole attribute); min/max capture is selector mode only.
		bool bFullRead = false;
		bool bCaptureMinMax = false;

		FReadableBufferConfig(const FReadableBufferConfig& Other);
		explicit FReadableBufferConfig(const FAttributeIdentity& InIdentity, EBufferPreloadType InMode = EBufferPreloadType::RawAttribute);
		FReadableBufferConfig(const FName InName, const EPCGMetadataTypes InUnderlyingType, EBufferPreloadType InMode = EBufferPreloadType::RawAttribute);
		FReadableBufferConfig(const FPCGAttributePropertyInputSelector& InSelector, const EPCGMetadataTypes InUnderlyingType);

		void Fetch(const TSharedRef<FFacade>& InFacade, const PCGExMT::FScope& Scope);
		void Read(const TSharedRef<FFacade>& InFacade) const;
	};

	class PCGEXCORE_API FFacadePreloader : public TSharedFromThis<FFacadePreloader>
	{
	protected:
		TWeakPtr<FPCGContextHandle> ContextHandle;
		TWeakPtr<FFacade> InternalDataFacadePtr;
		bool bLoaded = false;

	public:
		TArray<FReadableBufferConfig> BufferConfigs;

		FFacadePreloader(const TSharedPtr<FFacade>& InDataFacade);

		TSharedPtr<FFacade> GetDataFacade() const;

		bool IsEmpty() const
		{
			return BufferConfigs.IsEmpty();
		}

		int32 Num() const
		{
			return BufferConfigs.Num();
		}

		void Register(FPCGExContext* InContext, const FAttributeIdentity& InIdentity);

		void TryRegister(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector);

		template <typename T>
		void Register(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, bool bCaptureMinMax = false, bool bFullRead = false);

		template <typename T>
		void Register(FPCGExContext* InContext, const FName InName, EBufferPreloadType InMode = EBufferPreloadType::RawAttribute, bool bFullRead = false);

		void Fetch(const TSharedRef<FFacade>& InFacade, const PCGExMT::FScope& Scope);
		void Read(const TSharedRef<FFacade>& InFacade, const int32 ConfigIndex) const;

		///

		using CompletionCallback = std::function<void()>;
		CompletionCallback OnCompleteCallback;

		bool StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle = nullptr);

		bool IsLoaded() const
		{
			return bLoaded;
		}

	protected:
		void OnLoadingEnd();
	};

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template void FFacadePreloader::Register<_TYPE>(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, bool bCaptureMinMax, bool bFullRead); \
extern template void FFacadePreloader::Register<_TYPE>(FPCGExContext* InContext, const FName InName, EBufferPreloadType InMode, bool bFullRead);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	class PCGEXCORE_API FMultiFacadePreloader : public TSharedFromThis<FMultiFacadePreloader>
	{
		int32 NumCompleted = 0;
		TArray<TSharedPtr<FFacadePreloader>> Preloaders;
		bool bLoaded = false;

	public:
		FMultiFacadePreloader(const TArray<TSharedPtr<FFacade>>& InDataFacades);
		FMultiFacadePreloader(const TArray<TSharedRef<FFacade>>& InDataFacades);

		bool IsEmpty() const
		{
			return Preloaders.IsEmpty();
		}

		int32 Num() const
		{
			return Preloaders.Num();
		}

		using FPreloaderItCallback = std::function<void(FFacadePreloader&)>;
		void ForEach(FPreloaderItCallback&& It);

		PCGExMT::FCompletionCallback OnCompleteCallback;

		void StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle = nullptr);

	protected:
		void OnSubloadComplete();
		void OnLoadingEnd();
	};
}
