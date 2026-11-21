// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExDataPreloader.h"

#include "PCGExCoreSettingsCache.h"

#include "PCGExSettingsCacheBody.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Types/PCGExTypeTraits.h"

namespace PCGExData
{
	FReadableBufferConfig::FReadableBufferConfig(const FReadableBufferConfig& Other)
		: Mode(Other.Mode)
		  , Selector(Other.Selector)
		  , Identity(Other.Identity)
		  , bFullRead(Other.bFullRead)
		  , bCaptureMinMax(Other.bCaptureMinMax)
	{
	}

	FReadableBufferConfig::FReadableBufferConfig(const FAttributeIdentity& InIdentity, EBufferPreloadType InMode)
		: Mode(InMode)
		  , Identity(InIdentity)
	{
	}

	FReadableBufferConfig::FReadableBufferConfig(const FName InName, const EPCGMetadataTypes InUnderlyingType, EBufferPreloadType InMode)
		: Mode(InMode)
	{
		// Synthesized identity: no live attribute, so populate the inherited Desc fields directly.
		Identity.Name = InName;
		Identity.ValueType = InUnderlyingType;
	}

	FReadableBufferConfig::FReadableBufferConfig(const FPCGAttributePropertyInputSelector& InSelector, const EPCGMetadataTypes InUnderlyingType)
		: Mode(EBufferPreloadType::BroadcastFromSelector), Selector(InSelector)
	{
		// Synthesized identity: no live attribute available at this construction site.
		Identity.Name = InSelector.GetName();
		Identity.ValueType = InUnderlyingType;
	}

	void FReadableBufferConfig::Fetch(const TSharedRef<FFacade>& InFacade, const PCGExMT::FScope& Scope)
	{
		// Lazy-initialized buffer reader with three-state Status:
		// -1 = failed (permanently skip), 0 = uninitialized, 1 = ready.
		// The weak pointer allows the facade to own the buffer lifetime while
		// we cache a reference for repeated scoped fetches across parallel chunks.
		if (Status == -1)
		{
			return;
		}

		TSharedPtr<IBuffer> Reader = nullptr;

		{
			FReadScopeLock ReadScopeLock(ReaderLock);
			Reader = WeakReader.Pin();
		}

		if (!Reader)
		{
			PCGExMetaHelpers::ExecuteWithRightType(
				Identity,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					FWriteScopeLock WriteScopeLock(ReaderLock);

					switch (Mode)
					{
					case EBufferPreloadType::RawAttribute:
						Reader = InFacade->GetReadable<T>(Identity.GetIdentifier(), EIOSide::In, !bFullRead);
						break;
					case EBufferPreloadType::BroadcastFromName:
						Reader = InFacade->GetBroadcaster<T>(Identity.Name, !bFullRead);
						break;
					case EBufferPreloadType::BroadcastFromSelector:
						Reader = InFacade->GetBroadcaster<T>(Selector, !bFullRead, bCaptureMinMax);
						break;
					}

					WeakReader = Reader;
				},
				[&]()
				{
					// Property-backed: only RawAttribute mode is meaningful (broadcast/typed-conversion
					// on extended/container types isn't defined). Fall back to the generic FFacade::GetReadable
					// which returns FPropertyArrayBuffer / FPropertySingleValueBuffer for unknown types.
					FWriteScopeLock WriteScopeLock(ReaderLock);
					if (Mode == EBufferPreloadType::RawAttribute)
					{
						Reader = InFacade->GetReadable(Identity, EIOSide::In, true);
					}
					WeakReader = Reader;
				});

			if (!Reader)
			{
				Status = -1;
				return;
			}
			Status = 1;
		}

		Reader->Fetch(Scope);
	}

	void FReadableBufferConfig::Read(const TSharedRef<FFacade>& InFacade) const
	{
		PCGExMetaHelpers::ExecuteWithRightType(
			Identity,
			[&](auto DummyValue)
			{
				using T = decltype(DummyValue);
				TSharedPtr<TBuffer<T>> Reader = nullptr;
				switch (Mode)
				{
				case EBufferPreloadType::RawAttribute:
					Reader = InFacade->GetReadable<T>(Identity.GetIdentifier());
					break;
				case EBufferPreloadType::BroadcastFromName:
					Reader = InFacade->GetBroadcaster<T>(Identity.Name);
					break;
				case EBufferPreloadType::BroadcastFromSelector:
					Reader = InFacade->GetBroadcaster<T>(Selector, false, bCaptureMinMax);
					break;
				}
			},
			[&]()
			{
				// Property-backed: same constraints as Fetch -- only RawAttribute makes sense.
				if (Mode == EBufferPreloadType::RawAttribute)
				{
					InFacade->GetReadable(Identity, EIOSide::In, false);
				}
			});
	}

	FFacadePreloader::FFacadePreloader(const TSharedPtr<FFacade>& InDataFacade)
		: InternalDataFacadePtr(InDataFacade)
	{
	}

	TSharedPtr<FFacade> FFacadePreloader::GetDataFacade() const
	{
		return InternalDataFacadePtr.Pin();
	}

	void FFacadePreloader::Register(FPCGExContext* InContext, const FAttributeIdentity& InIdentity)
	{
		for (const FReadableBufferConfig& ExistingConfig : BufferConfigs)
		{
			if (ExistingConfig.Identity == InIdentity)
			{
				return;
			}
		}

		BufferConfigs.Emplace(InIdentity.Name, InIdentity.GetType());
	}

	void FFacadePreloader::TryRegister(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector)
	{
		TSharedPtr<FFacade> SourceFacade = GetDataFacade();
		if (!SourceFacade)
		{
			return;
		}

		FAttributeIdentity Identity;
		if (FAttributeIdentity::Get(SourceFacade->GetIn(), InSelector, Identity))
		{
			Register(InContext, Identity);
		}
	}

	template <typename T>
	void FFacadePreloader::Register(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, bool bCaptureMinMax, bool bFullRead)
	{
		EPCGMetadataTypes Type = PCGExTypes::TTraits<T>::Type;
		for (FReadableBufferConfig& ExistingConfig : BufferConfigs)
		{
			if (ExistingConfig.Selector == InSelector && ExistingConfig.Identity.ValueType == Type)
			{
				ExistingConfig.bFullRead = ExistingConfig.bFullRead || bFullRead;
				ExistingConfig.bCaptureMinMax = ExistingConfig.bCaptureMinMax || bCaptureMinMax;
				return;
			}
		}

		FReadableBufferConfig& Config = BufferConfigs.Emplace_GetRef(InSelector, Type);
		Config.bFullRead = bFullRead;
		Config.bCaptureMinMax = bCaptureMinMax;
	}

	template <typename T>
	void FFacadePreloader::Register(FPCGExContext* InContext, const FName InName, EBufferPreloadType InMode, bool bFullRead)
	{
		EPCGMetadataTypes Type = PCGExTypes::TTraits<T>::Type;
		for (FReadableBufferConfig& ExistingConfig : BufferConfigs)
		{
			if (ExistingConfig.Identity.Name == InName && ExistingConfig.Identity.ValueType == Type)
			{
				ExistingConfig.bFullRead = ExistingConfig.bFullRead || bFullRead;
				return;
			}
		}

		BufferConfigs.Emplace_GetRef(InName, Type, InMode).bFullRead = bFullRead;
	}

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API void FFacadePreloader::Register<_TYPE>(FPCGExContext* InContext, const FPCGAttributePropertyInputSelector& InSelector, bool bCaptureMinMax, bool bFullRead); \
template PCGEXCORE_API void FFacadePreloader::Register<_TYPE>(FPCGExContext* InContext, const FName InName, EBufferPreloadType InMode, bool bFullRead);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	void FFacadePreloader::Fetch(const TSharedRef<FFacade>& InFacade, const PCGExMT::FScope& Scope)
	{
		for (FReadableBufferConfig& ExistingConfig : BufferConfigs)
		{
			ExistingConfig.Fetch(InFacade, Scope);
		}
	}

	void FFacadePreloader::Read(const TSharedRef<FFacade>& InFacade, const int32 ConfigIndex) const
	{
		BufferConfigs[ConfigIndex].Read(InFacade);
	}

	bool FFacadePreloader::StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle)
	{
		ContextHandle = TaskManager->GetContext()->GetWeakSelfHandle();

		TSharedPtr<FFacade> SourceFacade = GetDataFacade();
		if (!SourceFacade)
		{
			return false;
		}

		if (IsEmpty())
		{
			OnLoadingEnd();
			return false;
		}

		// Pre-warm InKeys while still single-threaded: GetInKeys lazily inits under a write lock,
		// so the per-config tasks below would otherwise all pile up on the first init
		// (same rationale as IAssetLoader::Discover).
		if (SourceFacade->GetIn())
		{
			(void)SourceFacade->Source->GetInKeys();
		}

		PCGEX_ASYNC_SUBGROUP_CHKD_RET(TaskManager, InParentHandle, PrefetchAttributesTask, false)

		PrefetchAttributesTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnLoadingEnd();
		};

		if (SourceFacade->bSupportsScopedGet)
		{
			PrefetchAttributesTask->OnSubLoopStartCallback =
				[PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					if (const TSharedPtr<FFacade> InternalFacade = This->GetDataFacade())
					{
						This->Fetch(InternalFacade.ToSharedRef(), Scope);
					}
				};

			PrefetchAttributesTask->StartSubLoops(SourceFacade->GetNum(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		}
		else
		{
			PrefetchAttributesTask->OnIterationCallback =
				[PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					if (const TSharedPtr<FFacade> InternalFacade = This->GetDataFacade())
					{
						This->Read(InternalFacade.ToSharedRef(), Index);
					}
				};

			PrefetchAttributesTask->StartIterations(Num(), 1);
		}

		return true;
	}

	void FFacadePreloader::OnLoadingEnd()
	{
		if (bLoaded)
		{
			return;
		}

		PCGEX_SHARED_CONTEXT_VOID(ContextHandle)
		bLoaded = true;

		if (TSharedPtr<FFacade> InternalFacade = GetDataFacade())
		{
			InternalFacade->MarkCurrentBuffersReadAsComplete();
		}
		if (OnCompleteCallback)
		{
			OnCompleteCallback();
		}
	}

	FMultiFacadePreloader::FMultiFacadePreloader(const TArray<TSharedPtr<FFacade>>& InDataFacades)
	{
		Preloaders.Reserve(InDataFacades.Num());
		for (const TSharedPtr<FFacade>& InDataFacade : InDataFacades)
		{
			TSharedPtr<FFacadePreloader> Preloader = MakeShared<FFacadePreloader>(InDataFacade);
			Preloaders.Add(Preloader);
		}
	}

	FMultiFacadePreloader::FMultiFacadePreloader(const TArray<TSharedRef<FFacade>>& InDataFacades)
	{
		Preloaders.Reserve(InDataFacades.Num());
		for (const TSharedRef<FFacade>& InDataFacade : InDataFacades)
		{
			TSharedPtr<FFacadePreloader> Preloader = MakeShared<FFacadePreloader>(InDataFacade);
			Preloaders.Add(Preloader);
		}
	}

	void FMultiFacadePreloader::ForEach(FPreloaderItCallback&& It)
	{
		for (const TSharedPtr<FFacadePreloader>& Preloader : Preloaders)
		{
			It(*Preloader.Get());
		}
	}

	void FMultiFacadePreloader::StartLoading(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const TSharedPtr<PCGExMT::IAsyncHandleGroup>& InParentHandle)
	{
		for (const TSharedPtr<FFacadePreloader>& Preloader : Preloaders)
		{
			Preloader->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				FPlatformAtomics::InterlockedIncrement(&This->NumCompleted);
				This->OnSubloadComplete();
			};
		}
		{
			PCGEX_ASYNC_SCHEDULING_SCOPE(TaskManager)
			for (const TSharedPtr<FFacadePreloader>& Preloader : Preloaders)
			{
				Preloader->StartLoading(TaskManager, InParentHandle);
			}
		}
	}

	void FMultiFacadePreloader::OnSubloadComplete()
	{
		if (bLoaded || NumCompleted != Preloaders.Num())
		{
			return;
		}
		OnLoadingEnd();
	}

	void FMultiFacadePreloader::OnLoadingEnd()
	{
		if (bLoaded)
		{
			return;
		}
		bLoaded = true;
		if (OnCompleteCallback)
		{
			auto Callback = MoveTemp(OnCompleteCallback);
			Callback();
		}
	}
}
