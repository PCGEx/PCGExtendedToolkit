// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExAttributeHasher.h"

#include "PCGExCoreSettingsCache.h"
#include "PCGExSettingsCacheBody.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"

namespace PCGEx
{
	FAttributeHasher::FAttributeHasher(const FPCGExAttributeHashConfig& InConfig)
		: Config(InConfig)
	{
	}

	bool FAttributeHasher::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InFacade)
	{
		NumValues = InFacade->GetNum();
		if (NumValues <= 0)
		{
			return false;
		}

		DataFacade = InFacade;

		bool bDirectFetch = !RequiresCompilation();

		PCGExData::FProxyDescriptor Descriptor(DataFacade, PCGExData::EProxyRole::Read);
		if (bDirectFetch)
		{
			Descriptor.AddFlags(PCGExData::EProxyFlags::Direct);
		}

		if (!Descriptor.CaptureStrict(InContext, Config.SourceAttribute, PCGExData::EIOSide::In, true))
		{
			return false;
		}

		// Hash-only reads: container and extended-type attributes hash too.
		ValuesBuffer = PCGExData::GetProxyBuffer(InContext, Descriptor, PCGExData::EProxyAccess::TypeErased);

		if (!ValuesBuffer)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(InContext, Source Attribute, Config.SourceAttribute)
			return false;
		}

		if (bDirectFetch)
		{
			DataFacade->Fetch(PCGExMT::FScope(0, 1, 0));
			DataFacade->Fetch(PCGExMT::FScope(NumValues - 1, 1, 1));

			const PCGExValueHash A = ValuesBuffer->ReadValueHash(0);
			const PCGExValueHash B = ValuesBuffer->ReadValueHash(NumValues - 1);

			if (Config.Scope == EPCGExDataHashScope::First)
			{
				OutHash = A;
			}
			else if (Config.Scope == EPCGExDataHashScope::Last)
			{
				OutHash = B;
			}
			else if (Config.Scope == EPCGExDataHashScope::FirstAndLast)
			{
				if (Config.bSortInputValues)
				{
					if (Config.Sorting == EPCGExSortDirection::Ascending)
					{
						OutHash = A < B ? HashCombineFast(A, B) : HashCombineFast(B, A);
					}
					else
					{
						OutHash = A < B ? HashCombineFast(B, A) : HashCombineFast(A, B);
					}
				}
				else
				{
					OutHash = HashCombineFast(A, B);
				}
			}
		}

		return true;
	}

	bool FAttributeHasher::RequiresCompilation()
	{
		switch (Config.Scope)
		{
		case EPCGExDataHashScope::All:
		case EPCGExDataHashScope::Uniques:
			return true;
		case EPCGExDataHashScope::FirstAndLast:
		case EPCGExDataHashScope::First:
		case EPCGExDataHashScope::Last:
			return false;
		}

		return false;
	}

	void FAttributeHasher::Compile(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, PCGExMT::FSimpleCallback&& InCallback)
	{
		CompleteCallback = InCallback;

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, CompileHash)
		CompileHash->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnCompilationComplete();
		};

		CompileHash->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->ScopedHashes = MakeShared<PCGExMT::TScopedArray<PCGExValueHash>>(Loops, 0);
		};

		CompileHash->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->CompileScope(Scope);
		};

		CompileHash->StartSubLoops(NumValues, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
	}

	void FAttributeHasher::CompileScope(const PCGExMT::FScope& Scope)
	{
		DataFacade->Fetch(Scope);
		TArray<PCGExValueHash>& LocalHashes = ScopedHashes->Get_Ref(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			LocalHashes.Add(ValuesBuffer->ReadValueHash(Index));
		}
	}

	// Finalizes the parallel hash computation: collapses per-scope hash arrays into one,
	// optionally deduplicates (Uniques mode), sorts for determinism, then computes
	// a single CityHash32 over the entire hash sequence for use as a data fingerprint.
	void FAttributeHasher::OnCompilationComplete()
	{
		ScopedHashes->Collapse(Hashes);
		if (Config.Scope == EPCGExDataHashScope::Uniques)
		{
			TSet<PCGExValueHash> HashesSet;
			TArray<PCGExValueHash> UniqueHashes;

			HashesSet.Reserve(NumValues / 2);
			UniqueHashes.Reserve(NumValues / 2);

			for (const PCGExValueHash V : Hashes)
			{
				bool bIsAlreadySet = false;
				HashesSet.Add(V, &bIsAlreadySet);
				if (!bIsAlreadySet)
				{
					UniqueHashes.Add(V);
				}
			}

			Hashes.Empty();
			Hashes = MoveTemp(UniqueHashes);
		}

		if (Config.bSortInputValues)
		{
			if (Config.Sorting == EPCGExSortDirection::Ascending)
			{
				Hashes.Sort([](const int32 A, const int32 B)
				{
					return A < B;
				});
			}
			else
			{
				Hashes.Sort([](const int32 A, const int32 B)
				{
					return A > B;
				});
			}
		}

		//FXxHash64::HashBuffer(Hashes.GetData(), Hashes.Num() * sizeof(uint32)).Hash
		OutHash = CityHash32(reinterpret_cast<const char*>(Hashes.GetData()), Hashes.Num() * sizeof(uint32));

		if (CompleteCallback)
		{
			CompleteCallback();
		}
	}
}
