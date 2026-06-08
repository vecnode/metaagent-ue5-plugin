// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ParticleRuntime/MetaAgentParticleShapeCache.h"

#include "Async/Async.h"
#include "Core/MetaAgent.h"
#include "HAL/CriticalSection.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"

namespace
{
	struct FMetaAgentImageMaskCacheKey
	{
		FString SourceImagePath;
		FDateTime SourceFileTimestamp;
		int64 SourceFileSize = 0;
		EMetaAgentParticleImageSamplingMode ImageSamplingMode = EMetaAgentParticleImageSamplingMode::SobelEdges;
		float AlphaThreshold = 0.0f;
		float EdgeThreshold = 0.0f;
		bool bUseLuminance = false;
		int32 SampleResolution = 0;
		float GrayscaleGamma = 0.0f;
		float DensityGridScale = 0.0f;
		float TargetJitterNormalized = 0.0f;
		int32 DesiredPointCount = 0;

		bool operator==(const FMetaAgentImageMaskCacheKey& Other) const
		{
			return SourceImagePath == Other.SourceImagePath
				&& SourceFileTimestamp == Other.SourceFileTimestamp
				&& SourceFileSize == Other.SourceFileSize
				&& ImageSamplingMode == Other.ImageSamplingMode
				&& FMath::IsNearlyEqual(AlphaThreshold, Other.AlphaThreshold)
				&& FMath::IsNearlyEqual(EdgeThreshold, Other.EdgeThreshold)
				&& bUseLuminance == Other.bUseLuminance
				&& SampleResolution == Other.SampleResolution
				&& FMath::IsNearlyEqual(GrayscaleGamma, Other.GrayscaleGamma)
				&& FMath::IsNearlyEqual(DensityGridScale, Other.DensityGridScale)
				&& FMath::IsNearlyEqual(TargetJitterNormalized, Other.TargetJitterNormalized)
				&& DesiredPointCount == Other.DesiredPointCount;
		}
	};

	uint32 GetImageMaskCacheKeyHash(const FMetaAgentImageMaskCacheKey& Key)
	{
		uint32 Hash = FCrc::StrCrc32(*Key.SourceImagePath);
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourceFileTimestamp.GetTicks()));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SourceFileSize));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.ImageSamplingMode));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.AlphaThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.EdgeThreshold));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.bUseLuminance));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SampleResolution));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.GrayscaleGamma));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.DensityGridScale));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.TargetJitterNormalized));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.DesiredPointCount));
		return Hash;
	}

	FORCEINLINE uint32 GetTypeHash(const FMetaAgentImageMaskCacheKey& Key)
	{
		return GetImageMaskCacheKeyHash(Key);
	}

	FMetaAgentImageMaskCacheKey MakeCacheKey(const FMetaAgentImageMaskBuildParams& Params)
	{
		FMetaAgentImageMaskCacheKey Key;
		Key.SourceImagePath = Params.SourceImagePath;
		Key.SourceFileTimestamp = Params.SourceFileTimestamp;
		Key.SourceFileSize = Params.SourceFileSize;
		Key.ImageSamplingMode = Params.ImageSamplingMode;
		Key.AlphaThreshold = Params.AlphaThreshold;
		Key.EdgeThreshold = Params.EdgeThreshold;
		Key.bUseLuminance = Params.bUseLuminance;
		Key.SampleResolution = Params.SampleResolution;
		Key.GrayscaleGamma = Params.GrayscaleGamma;
		Key.DensityGridScale = Params.DensityGridScale;
		Key.TargetJitterNormalized = Params.TargetJitterNormalized;
		Key.DesiredPointCount = Params.DesiredPointCount;
		return Key;
	}

	struct FMetaAgentImageMaskCacheEntry
	{
		bool bSuccess = false;
		TArray<FVector> LocalPointsCm;
		FString DebugInfo;
	};

	struct FMetaAgentImageMaskAsyncJob
	{
		FMetaAgentImageMaskCacheKey Key;
		TFuture<FMetaAgentImageMaskBuildOutput> Future;
	};

	FCriticalSection GMaskCacheMutex;
	TMap<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskCacheEntry> GMaskCache;
	TMap<FMetaAgentImageMaskCacheKey, TSharedPtr<FMetaAgentImageMaskAsyncJob>> GInFlightJobs;
}

void FMetaAgentParticleShapeCache::InvalidateAll()
{
	FScopeLock Lock(&GMaskCacheMutex);
	GMaskCache.Reset();
	GInFlightJobs.Reset();
}

void FMetaAgentParticleShapeCache::InvalidateForPath(const FString& SourceImagePath)
{
	if (SourceImagePath.IsEmpty())
	{
		return;
	}

	FScopeLock Lock(&GMaskCacheMutex);

	for (auto It = GMaskCache.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceImagePath == SourceImagePath)
		{
			It.RemoveCurrent();
		}
	}

	for (auto It = GInFlightJobs.CreateIterator(); It; ++It)
	{
		if (It.Key().SourceImagePath == SourceImagePath)
		{
			It.RemoveCurrent();
		}
	}
}

void FMetaAgentParticleShapeCache::Tick()
{
	TArray<TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>> CompletedJobs;
	CompletedJobs.Reserve(4);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		for (auto It = GInFlightJobs.CreateIterator(); It; ++It)
		{
			const TSharedPtr<FMetaAgentImageMaskAsyncJob>& Job = It.Value();
			if (!Job.IsValid() || !Job->Future.IsReady())
			{
				continue;
			}

			CompletedJobs.Add(TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>(It.Key(), Job->Future.Get()));
			It.RemoveCurrent();
		}
	}

	for (const TPair<FMetaAgentImageMaskCacheKey, FMetaAgentImageMaskBuildOutput>& CompletedJob : CompletedJobs)
	{
		FMetaAgentImageMaskCacheEntry Entry;
		Entry.bSuccess = CompletedJob.Value.bSuccess;
		Entry.LocalPointsCm = CompletedJob.Value.LocalPointsCm;
		Entry.DebugInfo = CompletedJob.Value.DebugInfo;

		FScopeLock Lock(&GMaskCacheMutex);
		GMaskCache.Add(CompletedJob.Key, MoveTemp(Entry));

		UE_LOG(LogMetaAgent, Log,
			TEXT("ParticleShapeCache: async mask build finished success=%s points=%d | %s"),
			Entry.bSuccess ? TEXT("true") : TEXT("false"),
			Entry.LocalPointsCm.Num(),
			*Entry.DebugInfo);
	}
}

EMetaAgentImageMaskAvailability FMetaAgentParticleShapeCache::RequestBuild(
	const FMetaAgentImageMaskBuildParams& Params)
{
	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		return EMetaAgentImageMaskAvailability::Unavailable;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		return EMetaAgentImageMaskAvailability::Unavailable;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			return CachedEntry->bSuccess
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
		}

		if (GInFlightJobs.Contains(Key))
		{
			return EMetaAgentImageMaskAvailability::Building;
		}
	}

	TSharedPtr<FMetaAgentImageMaskAsyncJob> Job = MakeShared<FMetaAgentImageMaskAsyncJob>();
	Job->Key = Key;
	Job->Future = Async(
		EAsyncExecution::LargeThreadPool,
		[FreshParams]()
		{
			FMetaAgentImageMaskBuildOutput Output;
			MetaAgentImageMask::BuildMaskOnWorkerThread(FreshParams, Output);
			return Output;
		});

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			return CachedEntry->bSuccess
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
		}

		GInFlightJobs.Add(Key, Job);
	}

	UE_LOG(LogMetaAgent, Log,
		TEXT("ParticleShapeCache: async mask build started for '%s' (%d points @ %dpx)."),
		*FPaths::GetCleanFilename(FreshParams.SourceImagePath),
		FreshParams.DesiredPointCount,
		FreshParams.SampleResolution);

	return EMetaAgentImageMaskAvailability::Building;
}

FMetaAgentImageMaskLookupResult FMetaAgentParticleShapeCache::ResolveMask(
	const FMetaAgentImageMaskBuildParams& Params)
{
	FMetaAgentImageMaskLookupResult Result;
	Tick();

	if (Params.SourceImagePath.IsEmpty() || Params.DesiredPointCount <= 0)
	{
		Result.Availability = EMetaAgentImageMaskAvailability::Unavailable;
		Result.DebugInfo = TEXT("No image path or particle count for mask build.");
		return Result;
	}

	FDateTime CurrentTimestamp = Params.SourceFileTimestamp;
	int64 CurrentFileSize = Params.SourceFileSize;
	if (!MetaAgentImageMask::GetImageFileIdentity(Params.SourceImagePath, CurrentTimestamp, CurrentFileSize))
	{
		Result.Availability = EMetaAgentImageMaskAvailability::Unavailable;
		Result.DebugInfo = TEXT("Image file not found on disk.");
		return Result;
	}

	FMetaAgentImageMaskBuildParams FreshParams = Params;
	FreshParams.SourceFileTimestamp = CurrentTimestamp;
	FreshParams.SourceFileSize = CurrentFileSize;
	const FMetaAgentImageMaskCacheKey Key = MakeCacheKey(FreshParams);

	{
		FScopeLock Lock(&GMaskCacheMutex);
		if (const FMetaAgentImageMaskCacheEntry* CachedEntry = GMaskCache.Find(Key))
		{
			Result.LocalPointsCm = CachedEntry->LocalPointsCm;
			Result.DebugInfo = CachedEntry->DebugInfo;
			Result.Availability = CachedEntry->bSuccess
				? EMetaAgentImageMaskAvailability::Ready
				: EMetaAgentImageMaskAvailability::Failed;
			return Result;
		}

		if (GInFlightJobs.Contains(Key))
		{
			Result.Availability = EMetaAgentImageMaskAvailability::Building;
			Result.DebugInfo = TEXT("Building image mask on background thread...");
			return Result;
		}
	}

	const EMetaAgentImageMaskAvailability RequestedAvailability = RequestBuild(FreshParams);
	Result.Availability = RequestedAvailability;
	if (RequestedAvailability == EMetaAgentImageMaskAvailability::Building)
	{
		Result.DebugInfo = TEXT("Building image mask on background thread...");
	}
	else if (RequestedAvailability == EMetaAgentImageMaskAvailability::Unavailable)
	{
		Result.DebugInfo = TEXT("Mask build unavailable.");
	}

	return Result;
}
