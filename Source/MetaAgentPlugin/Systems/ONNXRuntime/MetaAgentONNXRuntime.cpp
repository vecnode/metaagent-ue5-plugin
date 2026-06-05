// Project-specific implementation and modifications Copyright (c) vecnode, 2026.

#include "Systems/ONNXRuntime/MetaAgentONNXRuntime.h"

#include "Core/MetaAgent.h"
#include "Gameplay/Controllers/MetaAgentPlayerController.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace
{
	struct FMetaAgentONNXSessionCache
	{
		TObjectPtr<UNNEModelData> TextEncoderModelData = nullptr;
		TObjectPtr<UNNEModelData> UNetModelData = nullptr;
		TObjectPtr<UNNEModelData> VAEDecoderModelData = nullptr;
		TSharedPtr<UE::NNE::IModelCPU> TextEncoderModel;
		TSharedPtr<UE::NNE::IModelCPU> UNetModel;
		TSharedPtr<UE::NNE::IModelCPU> VAEDecoderModel;
		TSharedPtr<UE::NNE::IModelInstanceCPU> TextEncoderInstance;
		TSharedPtr<UE::NNE::IModelInstanceCPU> UNetInstance;
		TSharedPtr<UE::NNE::IModelInstanceCPU> VAEDecoderInstance;
		TMap<FString, int32> Vocabulary;
		TMap<FString, int32> MergeRanks;
		TArray<FString> ByteEncoder;
		TMap<FString, FString> BPETokenCache;
		int32 ModelMaxLength = 77;
		FString RuntimeName;
		FString LoadedModelPath;
		FString TextEncoderModelPath;
		FString UNetModelPath;
		FString VAEDecoderModelPath;
		FString ModelRootPath;
	};

	TMap<TWeakObjectPtr<AMetaAgentPlayerController>, FMetaAgentONNXSessionCache> GONNXRuntimeCaches;
	const FString OnnxExternalDataDescriptorKey(TEXT("OnnxExternalDataDescriptor"));
	const FString OnnxExternalDataBytesKey(TEXT("OnnxExternalDataBytes"));

	FMetaAgentONNXSessionCache& GetOrCreateCache(AMetaAgentPlayerController& Controller)
	{
		return GONNXRuntimeCaches.FindOrAdd(TWeakObjectPtr<AMetaAgentPlayerController>(&Controller));
	}

	void ClearCache(AMetaAgentPlayerController& Controller)
	{
		GONNXRuntimeCaches.Remove(TWeakObjectPtr<AMetaAgentPlayerController>(&Controller));
	}

	FString ResolveRuntimeName(const FString& PreferredRuntimeName)
	{
		if (!PreferredRuntimeName.IsEmpty())
		{
			if (UE::NNE::GetRuntime<INNERuntimeCPU>(PreferredRuntimeName).IsValid())
			{
				return PreferredRuntimeName;
			}
		}

		const TArray<FString> RuntimeNames = UE::NNE::GetAllRuntimeNames<INNERuntimeCPU>();
		for (const FString& RuntimeName : RuntimeNames)
		{
			if (RuntimeName.Contains(TEXT("ORT"), ESearchCase::IgnoreCase))
			{
				return RuntimeName;
			}
		}

		return RuntimeNames.Num() > 0 ? RuntimeNames[0] : FString();
	}

	bool FileExists(const FString& AbsolutePath)
	{
		return IFileManager::Get().FileExists(*AbsolutePath);
	}

	FString FindStableDiffusionTextEncoderModelPath(const FString& RootPath)
	{
		const FString CanonicalPath = RootPath / TEXT("text_encoder/model.onnx");
		return FileExists(CanonicalPath) ? CanonicalPath : FString();
	}

	FString FindStableDiffusionUNetModelPath(const FString& RootPath)
	{
		const FString CanonicalPath = RootPath / TEXT("unet/model.onnx");
		return FileExists(CanonicalPath) ? CanonicalPath : FString();
	}

	FString FindStableDiffusionVaeDecoderModelPath(const FString& RootPath)
	{
		const FString CanonicalPath = RootPath / TEXT("vae_decoder/model.onnx");
		if (FileExists(CanonicalPath))
		{
			return CanonicalPath;
		}

		TArray<FString> OnnxFiles;
		IFileManager::Get().FindFilesRecursive(OnnxFiles, *RootPath, TEXT("*.onnx"), true, false, false);
		for (const FString& Candidate : OnnxFiles)
		{
			if (Candidate.Contains(TEXT("vae_decoder"), ESearchCase::IgnoreCase)
				&& Candidate.Contains(TEXT("model.onnx"), ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}

		for (const FString& Candidate : OnnxFiles)
		{
			if (Candidate.Contains(TEXT("decoder"), ESearchCase::IgnoreCase)
				&& !Candidate.Contains(TEXT("encoder"), ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}

		return FString();
	}

	bool ValidateStableDiffusionManifest(const FString& RootPath, FString& OutError)
	{
		const TArray<FString> RequiredFiles = {
			TEXT("model_index.json"),
			TEXT("scheduler/scheduler_config.json"),
			TEXT("tokenizer/merges.txt"),
			TEXT("tokenizer/vocab.json"),
			TEXT("tokenizer/tokenizer_config.json"),
			TEXT("tokenizer/special_tokens_map.json"),
			TEXT("text_encoder/model.onnx"),
			TEXT("unet/model.onnx"),
			TEXT("vae_decoder/model.onnx")
		};

		TArray<FString> MissingFiles;
		for (const FString& RelativePath : RequiredFiles)
		{
			if (!FileExists(RootPath / RelativePath))
			{
				MissingFiles.Add(RelativePath);
			}
		}

		if (MissingFiles.Num() > 0)
		{
			OutError = FString::Printf(
				TEXT("Load failed: missing SD files (%s)"),
				*FString::Join(MissingFiles, TEXT(", ")));
			return false;
		}

		return true;
	}

	void InitializeByteEncoder(TArray<FString>& OutByteEncoder)
	{
		if (OutByteEncoder.Num() == 256)
		{
			return;
		}

		OutByteEncoder.SetNum(256);
		TArray<int32> ByteValues;
		TArray<int32> CodePoints;

		for (int32 Value = 33; Value <= 126; ++Value)
		{
			ByteValues.Add(Value);
			CodePoints.Add(Value);
		}
		for (int32 Value = 161; Value <= 172; ++Value)
		{
			ByteValues.Add(Value);
			CodePoints.Add(Value);
		}
		for (int32 Value = 174; Value <= 255; ++Value)
		{
			ByteValues.Add(Value);
			CodePoints.Add(Value);
		}

		int32 ExtraIndex = 0;
		for (int32 Value = 0; Value < 256; ++Value)
		{
			if (!ByteValues.Contains(Value))
			{
				ByteValues.Add(Value);
				CodePoints.Add(256 + ExtraIndex++);
			}
		}

		for (int32 Index = 0; Index < ByteValues.Num(); ++Index)
		{
			OutByteEncoder[ByteValues[Index]] = FString::Chr(CodePoints[Index]);
		}
	}

	bool LoadVocabulary(const FString& VocabularyPath, TMap<FString, int32>& OutVocabulary)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *VocabularyPath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		OutVocabulary.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RootObject->Values)
		{
			OutVocabulary.Add(Pair.Key, static_cast<int32>(Pair.Value->AsNumber()));
		}

		return OutVocabulary.Num() > 0;
	}

	bool LoadMergeRanks(const FString& MergePath, TMap<FString, int32>& OutMergeRanks)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *MergePath))
		{
			return false;
		}

		OutMergeRanks.Reset();
		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString Line = Lines[LineIndex].TrimStartAndEnd();
			if (Line.IsEmpty())
			{
				continue;
			}

			FString Left;
			FString Right;
			if (!Line.Split(TEXT(" "), &Left, &Right))
			{
				continue;
			}

			OutMergeRanks.Add(Left + TEXT(" ") + Right, OutMergeRanks.Num());
		}

		return OutMergeRanks.Num() > 0;
	}

	TArray<FString> SplitTokenToPieces(const FString& Token)
	{
		TArray<FString> Pieces;
		for (int32 Index = 0; Index < Token.Len(); ++Index)
		{
			Pieces.Add(Token.Mid(Index, 1));
		}
		if (Pieces.Num() > 0)
		{
			Pieces.Last() += TEXT("</w>");
		}
		return Pieces;
	}

	FString RunBPE(FMetaAgentONNXSessionCache& Cache, const FString& Token)
	{
		if (const FString* Cached = Cache.BPETokenCache.Find(Token))
		{
			return *Cached;
		}

		TArray<FString> Word = SplitTokenToPieces(Token);
		if (Word.Num() == 0)
		{
			Cache.BPETokenCache.Add(Token, Token);
			return Token;
		}

		while (Word.Num() > 1)
		{
			int32 BestRank = TNumericLimits<int32>::Max();
			int32 BestIndex = INDEX_NONE;

			for (int32 Index = 0; Index < Word.Num() - 1; ++Index)
			{
				const FString PairKey = Word[Index] + TEXT(" ") + Word[Index + 1];
				if (const int32* Rank = Cache.MergeRanks.Find(PairKey))
				{
					if (*Rank < BestRank)
					{
						BestRank = *Rank;
						BestIndex = Index;
					}
				}
			}

			if (BestIndex == INDEX_NONE)
			{
				break;
			}

			Word[BestIndex] += Word[BestIndex + 1];
			Word.RemoveAt(BestIndex + 1);
		}

		const FString Result = FString::Join(Word, TEXT(" "));
		Cache.BPETokenCache.Add(Token, Result);
		return Result;
	}

	FString EncodeTokenBytes(const FString& Token, const TArray<FString>& ByteEncoder)
	{
		FTCHARToUTF8 Utf8(*Token);
		FString Encoded;
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			const uint8 ByteValue = static_cast<uint8>(Utf8.Get()[Index]);
			Encoded += ByteEncoder[ByteValue];
		}
		return Encoded;
	}

	TArray<FString> BasicCLIPSplit(const FString& InPrompt)
	{
		const FString Prompt = InPrompt.ToLower();
		TArray<FString> Tokens;
		int32 Index = 0;
		bool bPrefixSpace = false;

		while (Index < Prompt.Len())
		{
			while (Index < Prompt.Len() && FChar::IsWhitespace(Prompt[Index]))
			{
				bPrefixSpace = true;
				++Index;
			}

			if (Index >= Prompt.Len())
			{
				break;
			}

			const bool bAlphaNum = FChar::IsAlnum(Prompt[Index]);
			int32 Start = Index;
			while (Index < Prompt.Len())
			{
				const bool bCurrentAlphaNum = FChar::IsAlnum(Prompt[Index]);
				if (bCurrentAlphaNum != bAlphaNum || FChar::IsWhitespace(Prompt[Index]))
				{
					break;
				}
				++Index;
			}

			FString Token = Prompt.Mid(Start, Index - Start);
			if (bPrefixSpace)
			{
				Token = TEXT(" ") + Token;
				bPrefixSpace = false;
			}
			Tokens.Add(Token);
		}

		return Tokens;
	}

	TArray<int32> TokenizePrompt(FMetaAgentONNXSessionCache& Cache, const FString& Prompt)
	{
		const int32 BOS = Cache.Vocabulary.FindRef(TEXT("<|startoftext|>"));
		const int32 EOS = Cache.Vocabulary.FindRef(TEXT("<|endoftext|>"));

		TArray<int32> TokenIds;
		TokenIds.Reserve(Cache.ModelMaxLength);
		TokenIds.Add(BOS);

		const TArray<FString> SplitTokens = BasicCLIPSplit(Prompt);
		for (const FString& Token : SplitTokens)
		{
			const FString Encoded = EncodeTokenBytes(Token, Cache.ByteEncoder);
			const FString BPED = RunBPE(Cache, Encoded);
			TArray<FString> Pieces;
			BPED.ParseIntoArray(Pieces, TEXT(" "), true);

			for (const FString& Piece : Pieces)
			{
				if (const int32* TokenId = Cache.Vocabulary.Find(Piece))
				{
					TokenIds.Add(*TokenId);
				}
				else
				{
					TokenIds.Add(EOS);
				}

				if (TokenIds.Num() >= Cache.ModelMaxLength - 1)
				{
					break;
				}
			}

			if (TokenIds.Num() >= Cache.ModelMaxLength - 1)
			{
				break;
			}
		}

		TokenIds.Add(EOS);
		while (TokenIds.Num() < Cache.ModelMaxLength)
		{
			TokenIds.Add(EOS);
		}
		if (TokenIds.Num() > Cache.ModelMaxLength)
		{
			TokenIds.SetNum(Cache.ModelMaxLength);
			TokenIds.Last() = EOS;
		}

		return TokenIds;
	}

	UNNEModelData* CreateModelDataFromOnnxFile(UObject* Outer, const FString& OnnxPath)
	{
		TArray64<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *OnnxPath))
		{
			return nullptr;
		}

		TMap<FString, TConstArrayView64<uint8>> AdditionalBuffers;
		TArray<FString> PBFiles;
		IFileManager::Get().FindFiles(PBFiles, *(FPaths::GetPath(OnnxPath) / TEXT("*.pb")), true, false);
		if (PBFiles.Num() > 0)
		{
			PBFiles.Sort();

			TMap<FString, int64> ExternalDataSizes;
			TArray64<uint8> ExternalDataBytes;
			for (const FString& PBFile : PBFiles)
			{
				TArray64<uint8> Buffer;
				if (!FFileHelper::LoadFileToArray(Buffer, *(FPaths::GetPath(OnnxPath) / PBFile)))
				{
					continue;
				}

				ExternalDataSizes.Add(PBFile, Buffer.Num());
				ExternalDataBytes.Append(Buffer);
			}

			if (ExternalDataSizes.Num() > 0)
			{
				TArray64<uint8> ExternalDataDescriptor;
				FMemoryWriter64 DescriptorWriter(ExternalDataDescriptor, true);
				DescriptorWriter << ExternalDataSizes;

				AdditionalBuffers.Add(
					OnnxExternalDataDescriptorKey,
					TConstArrayView64<uint8>(ExternalDataDescriptor.GetData(), ExternalDataDescriptor.Num()));
				AdditionalBuffers.Add(
					OnnxExternalDataBytesKey,
					TConstArrayView64<uint8>(ExternalDataBytes.GetData(), ExternalDataBytes.Num()));

				UNNEModelData* ModelData = NewObject<UNNEModelData>(Outer);
				const TConstArrayView64<uint8> DataView(FileData.GetData(), FileData.Num());
				ModelData->Init(TEXT("onnx"), DataView, AdditionalBuffers);
				return ModelData;
			}
		}

		UNNEModelData* ModelData = NewObject<UNNEModelData>(Outer);
		const TConstArrayView64<uint8> DataView(FileData.GetData(), FileData.Num());
		ModelData->Init(TEXT("onnx"), DataView, AdditionalBuffers);
		return ModelData;
	}

	bool CreateModelInstance(UObject* Outer, TWeakInterfacePtr<INNERuntimeCPU> Runtime, const FString& ModelPath, TObjectPtr<UNNEModelData>& OutModelData, TSharedPtr<UE::NNE::IModelCPU>& OutModel, TSharedPtr<UE::NNE::IModelInstanceCPU>& OutInstance)
	{
		OutModelData = CreateModelDataFromOnnxFile(Outer, ModelPath);
		if (!OutModelData)
		{
			return false;
		}

		if (Runtime->CanCreateModelCPU(OutModelData) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		OutModel = Runtime->CreateModelCPU(OutModelData);
		if (!OutModel.IsValid())
		{
			return false;
		}

		OutInstance = OutModel->CreateModelInstanceCPU();
		return OutInstance.IsValid();
	}

	TArray<float> BuildAlphaCumprod(int32 NumTrainTimesteps, float BetaStart, float BetaEnd)
	{
		TArray<float> AlphaCumprod;
		AlphaCumprod.SetNum(NumTrainTimesteps);
		float RunningProduct = 1.0f;
		const float SqrtStart = FMath::Sqrt(BetaStart);
		const float SqrtEnd = FMath::Sqrt(BetaEnd);
		for (int32 Index = 0; Index < NumTrainTimesteps; ++Index)
		{
			const float T = static_cast<float>(Index) / FMath::Max(1, NumTrainTimesteps - 1);
			const float Beta = FMath::Square(FMath::Lerp(SqrtStart, SqrtEnd, T));
			RunningProduct *= (1.0f - Beta);
			AlphaCumprod[Index] = RunningProduct;
		}
		return AlphaCumprod;
	}

	TArray<int32> BuildInferenceTimesteps(int32 InferenceSteps, int32 TrainSteps, int32 StepsOffset)
	{
		const int32 StepRatio = FMath::Max(1, TrainSteps / FMath::Max(1, InferenceSteps));
		TArray<int32> Timesteps;
		Timesteps.Reserve(InferenceSteps);
		for (int32 Index = InferenceSteps - 1; Index >= 0; --Index)
		{
			Timesteps.Add(FMath::Clamp(Index * StepRatio + StepsOffset, 0, TrainSteps - 1));
		}
		return Timesteps;
	}

	int32 FindTensorIndex(TConstArrayView<UE::NNE::FTensorDesc> TensorDescs, const TArray<FString>& NameHints, int32 PreferredRank = INDEX_NONE, ENNETensorDataType PreferredType = ENNETensorDataType::None)
	{
		for (const FString& Hint : NameHints)
		{
			for (int32 Index = 0; Index < TensorDescs.Num(); ++Index)
			{
				const UE::NNE::FTensorDesc& Desc = TensorDescs[Index];
				const bool bRankMatches = PreferredRank == INDEX_NONE || Desc.GetShape().Rank() == PreferredRank;
				const bool bTypeMatches = PreferredType == ENNETensorDataType::None || Desc.GetDataType() == PreferredType;
				if (bRankMatches && bTypeMatches && Desc.GetName().Contains(Hint, ESearchCase::IgnoreCase))
				{
					return Index;
				}
			}
		}

		for (int32 Index = 0; Index < TensorDescs.Num(); ++Index)
		{
			const UE::NNE::FTensorDesc& Desc = TensorDescs[Index];
			const bool bRankMatches = PreferredRank == INDEX_NONE || Desc.GetShape().Rank() == PreferredRank;
			const bool bTypeMatches = PreferredType == ENNETensorDataType::None || Desc.GetDataType() == PreferredType;
			if (bRankMatches && bTypeMatches)
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	TArray<uint8> AllocateTensorBuffer(const UE::NNE::FTensorDesc& Desc, const UE::NNE::FTensorShape& Shape)
	{
		const uint64 TensorBytes = Shape.Volume() * Desc.GetElementByteSize();
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(static_cast<int32>(TensorBytes));
		return Buffer;
	}

	bool ExtractFloatTensor(const TArray<uint8>& Buffer, TArray<float>& OutValues)
	{
		if ((Buffer.Num() % sizeof(float)) != 0)
		{
			return false;
		}

		const int32 NumFloats = Buffer.Num() / sizeof(float);
		OutValues.SetNumUninitialized(NumFloats);
		FMemory::Memcpy(OutValues.GetData(), Buffer.GetData(), Buffer.Num());
		return true;
	}

	bool RunTextEncoderModel(FMetaAgentONNXSessionCache& Cache, const TArray<int32>& TokenIds, TArray<float>& OutEmbeddings)
	{
		if (!Cache.TextEncoderInstance.IsValid())
		{
			return false;
		}

		const TConstArrayView<UE::NNE::FTensorDesc> InputDescs = Cache.TextEncoderInstance->GetInputTensorDescs();
		const int32 InputIndex = FindTensorIndex(InputDescs, {TEXT("input_ids")}, 2);
		if (InputIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> InputShapes;
		InputShapes.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			InputShapes[Index] = (Index == InputIndex)
				? UE::NNE::FTensorShape::Make({1u, static_cast<uint32>(Cache.ModelMaxLength)})
				: UE::NNE::FTensorShape::MakeFromSymbolic(InputDescs[Index].GetShape());
		}

		if (Cache.TextEncoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		TArray<TArray<uint8>> InputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> InputBindings;
		InputBuffers.SetNum(InputDescs.Num());
		InputBindings.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			InputBuffers[Index] = AllocateTensorBuffer(InputDescs[Index], InputShapes[Index]);
			InputBindings[Index] = {InputBuffers[Index].GetData(), static_cast<uint64>(InputBuffers[Index].Num())};
		}

		if (InputDescs[InputIndex].GetDataType() == ENNETensorDataType::Int64)
		{
			int64* Data = reinterpret_cast<int64*>(InputBuffers[InputIndex].GetData());
			for (int32 Index = 0; Index < TokenIds.Num(); ++Index)
			{
				Data[Index] = TokenIds[Index];
			}
		}
		else
		{
			int32* Data = reinterpret_cast<int32*>(InputBuffers[InputIndex].GetData());
			for (int32 Index = 0; Index < TokenIds.Num(); ++Index)
			{
				Data[Index] = TokenIds[Index];
			}
		}

		const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = Cache.TextEncoderInstance->GetOutputTensorDescs();
		const int32 OutputIndex = FindTensorIndex(OutputDescs, {TEXT("last_hidden_state"), TEXT("hidden_state")}, 3, ENNETensorDataType::Float);
		if (OutputIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> OutputShapes;
		OutputShapes.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputShapes[Index] = (Index == OutputIndex)
				? UE::NNE::FTensorShape::Make({1u, static_cast<uint32>(Cache.ModelMaxLength), 768u})
				: UE::NNE::FTensorShape::MakeFromSymbolic(OutputDescs[Index].GetShape());
		}

		TArray<TArray<uint8>> OutputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
		OutputBuffers.SetNum(OutputDescs.Num());
		OutputBindings.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputBuffers[Index] = AllocateTensorBuffer(OutputDescs[Index], OutputShapes[Index]);
			OutputBindings[Index] = {OutputBuffers[Index].GetData(), static_cast<uint64>(OutputBuffers[Index].Num())};
		}

		if (Cache.TextEncoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		return ExtractFloatTensor(OutputBuffers[OutputIndex], OutEmbeddings);
	}

	bool RunUNetModel(FMetaAgentONNXSessionCache& Cache, const TArray<float>& Latents, int32 LatentWidth, int32 LatentHeight, int32 Timestep, const TArray<float>& EncoderHiddenStates, TArray<float>& OutNoise)
	{
		if (!Cache.UNetInstance.IsValid())
		{
			return false;
		}

		const TConstArrayView<UE::NNE::FTensorDesc> InputDescs = Cache.UNetInstance->GetInputTensorDescs();
		const int32 SampleIndex = FindTensorIndex(InputDescs, {TEXT("sample"), TEXT("latent")}, 4, ENNETensorDataType::Float);
		const int32 TimeIndex = FindTensorIndex(InputDescs, {TEXT("timestep"), TEXT("time")});
		const int32 HiddenIndex = FindTensorIndex(InputDescs, {TEXT("encoder_hidden_states"), TEXT("hidden_states")}, 3, ENNETensorDataType::Float);
		if (SampleIndex == INDEX_NONE || TimeIndex == INDEX_NONE || HiddenIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> InputShapes;
		InputShapes.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			if (Index == SampleIndex)
			{
				InputShapes[Index] = UE::NNE::FTensorShape::Make({1u, 4u, static_cast<uint32>(LatentHeight), static_cast<uint32>(LatentWidth)});
			}
			else if (Index == TimeIndex)
			{
				InputShapes[Index] = UE::NNE::FTensorShape::Make({1u});
			}
			else if (Index == HiddenIndex)
			{
				InputShapes[Index] = UE::NNE::FTensorShape::Make({1u, static_cast<uint32>(Cache.ModelMaxLength), 768u});
			}
			else
			{
				InputShapes[Index] = UE::NNE::FTensorShape::MakeFromSymbolic(InputDescs[Index].GetShape());
			}
		}

		if (Cache.UNetInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		TArray<TArray<uint8>> InputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> InputBindings;
		InputBuffers.SetNum(InputDescs.Num());
		InputBindings.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			InputBuffers[Index] = AllocateTensorBuffer(InputDescs[Index], InputShapes[Index]);
			InputBindings[Index] = {InputBuffers[Index].GetData(), static_cast<uint64>(InputBuffers[Index].Num())};
		}

		FMemory::Memcpy(InputBuffers[SampleIndex].GetData(), Latents.GetData(), InputBuffers[SampleIndex].Num());
		if (InputDescs[TimeIndex].GetDataType() == ENNETensorDataType::Float)
		{
			reinterpret_cast<float*>(InputBuffers[TimeIndex].GetData())[0] = static_cast<float>(Timestep);
		}
		else if (InputDescs[TimeIndex].GetDataType() == ENNETensorDataType::Int64)
		{
			reinterpret_cast<int64*>(InputBuffers[TimeIndex].GetData())[0] = Timestep;
		}
		else
		{
			reinterpret_cast<int32*>(InputBuffers[TimeIndex].GetData())[0] = Timestep;
		}
		FMemory::Memcpy(InputBuffers[HiddenIndex].GetData(), EncoderHiddenStates.GetData(), InputBuffers[HiddenIndex].Num());

		const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = Cache.UNetInstance->GetOutputTensorDescs();
		const int32 OutputIndex = FindTensorIndex(OutputDescs, {TEXT("out_sample"), TEXT("sample")}, 4, ENNETensorDataType::Float);
		if (OutputIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> OutputShapes;
		OutputShapes.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputShapes[Index] = (Index == OutputIndex)
				? UE::NNE::FTensorShape::Make({1u, 4u, static_cast<uint32>(LatentHeight), static_cast<uint32>(LatentWidth)})
				: UE::NNE::FTensorShape::MakeFromSymbolic(OutputDescs[Index].GetShape());
		}

		TArray<TArray<uint8>> OutputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
		OutputBuffers.SetNum(OutputDescs.Num());
		OutputBindings.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputBuffers[Index] = AllocateTensorBuffer(OutputDescs[Index], OutputShapes[Index]);
			OutputBindings[Index] = {OutputBuffers[Index].GetData(), static_cast<uint64>(OutputBuffers[Index].Num())};
		}

		if (Cache.UNetInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		return ExtractFloatTensor(OutputBuffers[OutputIndex], OutNoise);
	}

	bool RunVAEDecoderModel(FMetaAgentONNXSessionCache& Cache, const TArray<float>& Latents, int32 LatentWidth, int32 LatentHeight, int32 OutputWidth, int32 OutputHeight, TArray<float>& OutImage)
	{
		if (!Cache.VAEDecoderInstance.IsValid())
		{
			return false;
		}

		const TConstArrayView<UE::NNE::FTensorDesc> InputDescs = Cache.VAEDecoderInstance->GetInputTensorDescs();
		const int32 InputIndex = FindTensorIndex(InputDescs, {TEXT("latent_sample"), TEXT("latents"), TEXT("sample")}, 4, ENNETensorDataType::Float);
		if (InputIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> InputShapes;
		InputShapes.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			InputShapes[Index] = (Index == InputIndex)
				? UE::NNE::FTensorShape::Make({1u, 4u, static_cast<uint32>(LatentHeight), static_cast<uint32>(LatentWidth)})
				: UE::NNE::FTensorShape::MakeFromSymbolic(InputDescs[Index].GetShape());
		}

		if (Cache.VAEDecoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		TArray<TArray<uint8>> InputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> InputBindings;
		InputBuffers.SetNum(InputDescs.Num());
		InputBindings.SetNum(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			InputBuffers[Index] = AllocateTensorBuffer(InputDescs[Index], InputShapes[Index]);
			InputBindings[Index] = {InputBuffers[Index].GetData(), static_cast<uint64>(InputBuffers[Index].Num())};
		}
		FMemory::Memcpy(InputBuffers[InputIndex].GetData(), Latents.GetData(), InputBuffers[InputIndex].Num());

		const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = Cache.VAEDecoderInstance->GetOutputTensorDescs();
		const int32 OutputIndex = FindTensorIndex(OutputDescs, {TEXT("sample"), TEXT("image")}, 4, ENNETensorDataType::Float);
		if (OutputIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<UE::NNE::FTensorShape> OutputShapes;
		OutputShapes.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputShapes[Index] = (Index == OutputIndex)
				? UE::NNE::FTensorShape::Make({1u, 3u, static_cast<uint32>(OutputHeight), static_cast<uint32>(OutputWidth)})
				: UE::NNE::FTensorShape::MakeFromSymbolic(OutputDescs[Index].GetShape());
		}

		TArray<TArray<uint8>> OutputBuffers;
		TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
		OutputBuffers.SetNum(OutputDescs.Num());
		OutputBindings.SetNum(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			OutputBuffers[Index] = AllocateTensorBuffer(OutputDescs[Index], OutputShapes[Index]);
			OutputBindings[Index] = {OutputBuffers[Index].GetData(), static_cast<uint64>(OutputBuffers[Index].Num())};
		}

		if (Cache.VAEDecoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
		{
			return false;
		}

		return ExtractFloatTensor(OutputBuffers[OutputIndex], OutImage);
	}

	void StepDDIM(const TArray<float>& AlphaCumprod, int32 Timestep, int32 PrevTimestep, TArray<float>& InOutLatents, const TArray<float>& NoisePrediction)
	{
		const float AlphaT = AlphaCumprod[Timestep];
		const float AlphaPrev = PrevTimestep >= 0 ? AlphaCumprod[PrevTimestep] : 1.0f;
		const float SqrtAlphaT = FMath::Sqrt(AlphaT);
		const float SqrtOneMinusAlphaT = FMath::Sqrt(FMath::Max(0.0f, 1.0f - AlphaT));
		const float SqrtAlphaPrev = FMath::Sqrt(AlphaPrev);
		const float SqrtOneMinusAlphaPrev = FMath::Sqrt(FMath::Max(0.0f, 1.0f - AlphaPrev));

		for (int32 Index = 0; Index < InOutLatents.Num(); ++Index)
		{
			const float PredOriginal = (InOutLatents[Index] - SqrtOneMinusAlphaT * NoisePrediction[Index]) / FMath::Max(1e-6f, SqrtAlphaT);
			InOutLatents[Index] = SqrtAlphaPrev * PredOriginal + SqrtOneMinusAlphaPrev * NoisePrediction[Index];
		}
	}

	UE::NNE::FTensorShape BuildLatentInputShape(const UE::NNE::FTensorDesc& TensorDesc, int32 TargetWidth, int32 TargetHeight)
	{
		const TConstArrayView<int32> SymbolicShape = TensorDesc.GetShape().GetData();
		const int32 LatentWidth = FMath::Max(8, TargetWidth / 8);
		const int32 LatentHeight = FMath::Max(8, TargetHeight / 8);

		TArray<uint32> Dims;
		Dims.Reserve(SymbolicShape.Num());

		for (int32 Index = 0; Index < SymbolicShape.Num(); ++Index)
		{
			const int32 SymbolicDim = SymbolicShape[Index];
			if (SymbolicDim > 0)
			{
				Dims.Add(static_cast<uint32>(SymbolicDim));
				continue;
			}

			if (SymbolicShape.Num() == 4)
			{
				if (Index == 0)
				{
					Dims.Add(1u);
				}
				else if (Index == 1)
				{
					Dims.Add(4u);
				}
				else if (Index == 2)
				{
					Dims.Add(static_cast<uint32>(LatentHeight));
				}
				else if (Index == 3)
				{
					Dims.Add(static_cast<uint32>(LatentWidth));
				}
			}
			else
			{
				Dims.Add(1u);
			}
		}

		if (Dims.Num() == 0)
		{
			Dims = {1u, 4u, static_cast<uint32>(LatentHeight), static_cast<uint32>(LatentWidth)};
		}

		return UE::NNE::FTensorShape::Make(Dims);
	}

	UE::NNE::FTensorShape BuildConcreteShape(const UE::NNE::FTensorDesc& TensorDesc, int32 TargetWidth, int32 TargetHeight)
	{
		const FString TensorName = TensorDesc.GetName();
		if (TensorName.Contains(TEXT("latent"), ESearchCase::IgnoreCase)
			|| TensorName.Contains(TEXT("sample"), ESearchCase::IgnoreCase))
		{
			return BuildLatentInputShape(TensorDesc, TargetWidth, TargetHeight);
		}

		const TConstArrayView<int32> SymbolicShape = TensorDesc.GetShape().GetData();
		TArray<uint32> Dims;
		Dims.Reserve(SymbolicShape.Num());

		for (int32 Index = 0; Index < SymbolicShape.Num(); ++Index)
		{
			const int32 SymbolicDim = SymbolicShape[Index];
			if (SymbolicDim > 0)
			{
				Dims.Add(static_cast<uint32>(SymbolicDim));
				continue;
			}

			if (SymbolicShape.Num() == 4 && (Index == 2 || Index == 3))
			{
				Dims.Add(static_cast<uint32>(Index == 2 ? FMath::Max(8, TargetHeight) : FMath::Max(8, TargetWidth)));
			}
			else
			{
				Dims.Add(1u);
			}
		}

		if (Dims.Num() == 0)
		{
			Dims.Add(1u);
		}

		return UE::NNE::FTensorShape::Make(Dims);
	}

	UE::NNE::FTensorShape BuildDecodedImageOutputShape(const UE::NNE::FTensorDesc& TensorDesc, int32 TargetWidth, int32 TargetHeight)
	{
		const TConstArrayView<int32> SymbolicShape = TensorDesc.GetShape().GetData();
		TArray<uint32> Dims;
		Dims.Reserve(SymbolicShape.Num());

		for (int32 Index = 0; Index < SymbolicShape.Num(); ++Index)
		{
			const int32 SymbolicDim = SymbolicShape[Index];
			if (SymbolicDim > 0)
			{
				Dims.Add(static_cast<uint32>(SymbolicDim));
				continue;
			}

			if (SymbolicShape.Num() == 4)
			{
				if (Index == 0)
				{
					Dims.Add(1u);
				}
				else if (Index == 1)
				{
					Dims.Add(3u);
				}
				else if (Index == 2)
				{
					Dims.Add(static_cast<uint32>(TargetHeight));
				}
				else if (Index == 3)
				{
					Dims.Add(static_cast<uint32>(TargetWidth));
				}
			}
			else
			{
				Dims.Add(1u);
			}
		}

		if (Dims.Num() == 0)
		{
			Dims = {1u, 3u, static_cast<uint32>(TargetHeight), static_cast<uint32>(TargetWidth)};
		}

		return UE::NNE::FTensorShape::Make(Dims);
	}

	FString ResolveConfiguredModelRootPath(const FString& ConfiguredPath)
	{
		FString Path = ConfiguredPath.TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			return FString();
		}

		const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (!UserProfile.IsEmpty())
		{
			Path.ReplaceInline(TEXT("%USERPROFILE%"), *UserProfile, ESearchCase::IgnoreCase);

			if (Path.StartsWith(TEXT("~/")) || Path.StartsWith(TEXT("~\\")))
			{
				Path = UserProfile / Path.RightChop(2);
			}

			if (Path.StartsWith(TEXT("Desktop/"), ESearchCase::IgnoreCase)
				|| Path.StartsWith(TEXT("Desktop\\"), ESearchCase::IgnoreCase))
			{
				Path = UserProfile / Path;
			}
		}

		return FPaths::ConvertRelativePathToFull(Path);
	}

	bool SaveRgbImageToPng(const FString& OutputPath, int32 Width, int32 Height, const TArray<uint8>& RGB)
	{
		if (RGB.Num() != Width * Height * 3)
		{
			return false;
		}

		TArray<uint8> RGBA;
		RGBA.Reserve(Width * Height * 4);
		for (int32 PixelIndex = 0; PixelIndex < Width * Height; ++PixelIndex)
		{
			const int32 RGBIndex = PixelIndex * 3;
			RGBA.Add(RGB[RGBIndex + 0]);
			RGBA.Add(RGB[RGBIndex + 1]);
			RGBA.Add(RGB[RGBIndex + 2]);
			RGBA.Add(255);
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Writer = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Writer.IsValid())
		{
			return false;
		}

		if (!Writer->SetRaw(RGBA.GetData(), RGBA.Num(), Width, Height, ERGBFormat::RGBA, 8))
		{
			return false;
		}

		const TArray64<uint8>& Compressed = Writer->GetCompressed(100);
		return FFileHelper::SaveArrayToFile(Compressed, *OutputPath);
	}

	void BuildProceduralFallbackImage(int32 Width, int32 Height, int32 Seed, TArray<uint8>& OutRGB)
	{
		OutRGB.Reset();
		OutRGB.SetNumZeroed(Width * Height * 3);

		FRandomStream Random(Seed);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = (Y * Width + X) * 3;
				const float U = static_cast<float>(X) / FMath::Max(1, Width - 1);
				const float V = static_cast<float>(Y) / FMath::Max(1, Height - 1);
				const uint8 R = static_cast<uint8>(FMath::Clamp((0.60f * U + 0.40f * Random.FRand()) * 255.0f, 0.0f, 255.0f));
				const uint8 G = static_cast<uint8>(FMath::Clamp((0.60f * V + 0.40f * Random.FRand()) * 255.0f, 0.0f, 255.0f));
				const uint8 B = static_cast<uint8>(FMath::Clamp((0.45f * (1.0f - U) + 0.55f * Random.FRand()) * 255.0f, 0.0f, 255.0f));
				OutRGB[PixelIndex + 0] = R;
				OutRGB[PixelIndex + 1] = G;
				OutRGB[PixelIndex + 2] = B;
			}
		}
	}

	void ConvertOutputToRGB(
		const TArray<uint8>& OutputBuffer,
		const UE::NNE::FTensorDesc& OutputDesc,
		const UE::NNE::FTensorShape& OutputShape,
		int32 TargetWidth,
		int32 TargetHeight,
		TArray<uint8>& OutRGB,
		int32& OutWidth,
		int32& OutHeight)
	{
		OutWidth = TargetWidth;
		OutHeight = TargetHeight;

		const TConstArrayView<uint32> Dims = OutputShape.GetData();
		const bool bFloatOutput = OutputDesc.GetDataType() == ENNETensorDataType::Float;

		if (!bFloatOutput || Dims.Num() != 4)
		{
			BuildProceduralFallbackImage(TargetWidth, TargetHeight, 1337, OutRGB);
			return;
		}

		const float* FloatData = reinterpret_cast<const float*>(OutputBuffer.GetData());
		const int32 NumFloats = OutputBuffer.Num() / sizeof(float);
		if (NumFloats <= 0)
		{
			BuildProceduralFallbackImage(TargetWidth, TargetHeight, 1337, OutRGB);
			return;
		}

		const int32 N = static_cast<int32>(Dims[0]);
		const int32 D1 = static_cast<int32>(Dims[1]);
		const int32 D2 = static_cast<int32>(Dims[2]);
		const int32 D3 = static_cast<int32>(Dims[3]);
		const bool bLikelyNCHW = D1 > 0 && D1 <= 4;
		const bool bLikelyNHWC = D3 > 0 && D3 <= 4;

		if (N < 1 || (!bLikelyNCHW && !bLikelyNHWC))
		{
			BuildProceduralFallbackImage(TargetWidth, TargetHeight, 1337, OutRGB);
			return;
		}

		const int32 Height = bLikelyNCHW ? D2 : D1;
		const int32 Width = bLikelyNCHW ? D3 : D2;
		const int32 Channels = bLikelyNCHW ? D1 : D3;
		if (Width <= 0 || Height <= 0 || Channels <= 0)
		{
			BuildProceduralFallbackImage(TargetWidth, TargetHeight, 1337, OutRGB);
			return;
		}

		OutRGB.Reset();
		OutRGB.SetNumZeroed(Width * Height * 3);
		OutWidth = Width;
		OutHeight = Height;

		float MinValue = TNumericLimits<float>::Max();
		float MaxValue = TNumericLimits<float>::Lowest();
		for (int32 Index = 0; Index < NumFloats; ++Index)
		{
			MinValue = FMath::Min(MinValue, FloatData[Index]);
			MaxValue = FMath::Max(MaxValue, FloatData[Index]);
		}
		const bool bLooksLikeNormalizedImage = MinValue >= -2.0f && MaxValue <= 2.0f;
		const float Scale = (!bLooksLikeNormalizedImage && MaxValue > MinValue) ? (255.0f / (MaxValue - MinValue)) : 1.0f;

		auto ReadTensorValue = [FloatData, bLikelyNCHW, Width, Height, Channels](int32 X, int32 Y, int32 C) -> float
		{
			if (bLikelyNCHW)
			{
				const int32 Index = ((C * Height + Y) * Width + X);
				return FloatData[Index];
			}
			const int32 Index = ((Y * Width + X) * Channels + C);
			return FloatData[Index];
		};

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 PixelIndex = (Y * Width + X) * 3;
				const float Rf = ReadTensorValue(X, Y, 0);
				const float Gf = ReadTensorValue(X, Y, FMath::Min(1, Channels - 1));
				const float Bf = ReadTensorValue(X, Y, FMath::Min(2, Channels - 1));
				if (bLooksLikeNormalizedImage)
				{
					OutRGB[PixelIndex + 0] = static_cast<uint8>(FMath::Clamp((Rf * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
					OutRGB[PixelIndex + 1] = static_cast<uint8>(FMath::Clamp((Gf * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
					OutRGB[PixelIndex + 2] = static_cast<uint8>(FMath::Clamp((Bf * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
				}
				else
				{
					OutRGB[PixelIndex + 0] = static_cast<uint8>(FMath::Clamp((Rf - MinValue) * Scale, 0.0f, 255.0f));
					OutRGB[PixelIndex + 1] = static_cast<uint8>(FMath::Clamp((Gf - MinValue) * Scale, 0.0f, 255.0f));
					OutRGB[PixelIndex + 2] = static_cast<uint8>(FMath::Clamp((Bf - MinValue) * Scale, 0.0f, 255.0f));
				}
			}
		}
	}
}

void FMetaAgentONNXRuntime::RunLoadPipelineSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentONNXState& ONNXState)
{
	ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Loading;
	ONNXState.LastStatus = TEXT("Loading ONNX pipeline...");
	ONNXState.LastOutputImagePath.Empty();

	const FString RootPath = ResolveConfiguredModelRootPath(ONNXState.ModelRootPath);
	if (RootPath.IsEmpty())
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Load failed: ONNX model root path is empty");
		ClearCache(Controller);
		return;
	}

	if (!FPaths::DirectoryExists(RootPath))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = FString::Printf(TEXT("Load failed: directory not found (%s)"), *RootPath);
		ClearCache(Controller);
		return;
	}

	FString ManifestError;
	if (!ValidateStableDiffusionManifest(RootPath, ManifestError))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = ManifestError;
		ClearCache(Controller);
		return;
	}

	const FString TextEncoderPath = FindStableDiffusionTextEncoderModelPath(RootPath);
	const FString UNetPath = FindStableDiffusionUNetModelPath(RootPath);
	const FString VAEDecoderPath = FindStableDiffusionVaeDecoderModelPath(RootPath);
	if (TextEncoderPath.IsEmpty() || UNetPath.IsEmpty() || VAEDecoderPath.IsEmpty())
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Load failed: unable to locate text_encoder/unet/vae_decoder model files");
		ClearCache(Controller);
		return;
	}

	const FString RuntimeName = ResolveRuntimeName(ONNXState.PreferredRuntimeName);
	if (RuntimeName.IsEmpty())
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Load failed: no CPU NNE runtime available");
		ClearCache(Controller);
		return;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = FString::Printf(TEXT("Load failed: runtime '%s' not available"), *RuntimeName);
		ClearCache(Controller);
		return;
	}

	FMetaAgentONNXSessionCache NewCache;
	NewCache.RuntimeName = RuntimeName;
	NewCache.TextEncoderModelPath = TextEncoderPath;
	NewCache.UNetModelPath = UNetPath;
	NewCache.VAEDecoderModelPath = VAEDecoderPath;
	NewCache.LoadedModelPath = VAEDecoderPath;
	NewCache.ModelRootPath = RootPath;
	InitializeByteEncoder(NewCache.ByteEncoder);
	if (!LoadVocabulary(RootPath / TEXT("tokenizer/vocab.json"), NewCache.Vocabulary)
		|| !LoadMergeRanks(RootPath / TEXT("tokenizer/merges.txt"), NewCache.MergeRanks))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Load failed: unable to parse tokenizer vocab/merges");
		ClearCache(Controller);
		return;
	}

	if (!CreateModelInstance(&Controller, Runtime, TextEncoderPath, NewCache.TextEncoderModelData, NewCache.TextEncoderModel, NewCache.TextEncoderInstance)
		|| !CreateModelInstance(&Controller, Runtime, UNetPath, NewCache.UNetModelData, NewCache.UNetModel, NewCache.UNetInstance)
		|| !CreateModelInstance(&Controller, Runtime, VAEDecoderPath, NewCache.VAEDecoderModelData, NewCache.VAEDecoderModel, NewCache.VAEDecoderInstance))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Load failed: unable to create SD model instances");
		ClearCache(Controller);
		return;
	}

	GetOrCreateCache(Controller) = NewCache;
	ONNXState.LastLoadedModelPath = RootPath;
	ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Loaded;
	ONNXState.LastStatus = FString::Printf(TEXT("Loaded SD text_encoder + unet + vae_decoder via %s"), *RuntimeName);

	UE_LOG(LogMetaAgent, Log, TEXT("ONNXRuntime: loaded SD pipeline from '%s' using runtime '%s'."), *RootPath, *RuntimeName);
}

void FMetaAgentONNXRuntime::RunGenerateImageSequence(
	AMetaAgentPlayerController& Controller,
	FMetaAgentONNXState& ONNXState)
{
	FMetaAgentONNXSessionCache* Cache = GONNXRuntimeCaches.Find(TWeakObjectPtr<AMetaAgentPlayerController>(&Controller));
	if (!Cache || !Cache->TextEncoderInstance.IsValid() || !Cache->UNetInstance.IsValid() || !Cache->VAEDecoderInstance.IsValid())
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Generate failed: model not loaded (press K)");
		return;
	}

	if (ONNXState.Prompt.TrimStartAndEnd().IsEmpty())
	{
		ONNXState.Prompt = TEXT("a cinematic portrait, centered composition, soft volumetric light, neutral background");
	}
	if (ONNXState.NegativePrompt.TrimStartAndEnd().IsEmpty())
	{
		ONNXState.NegativePrompt = TEXT("blurry, low quality, distorted, cropped");
	}

	ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Generating;
	ONNXState.LastStatus = TEXT("Generating Stable Diffusion image...");

	const int32 SafeWidth = FMath::Max(64, ONNXState.TargetWidth - (ONNXState.TargetWidth % 8));
	const int32 SafeHeight = FMath::Max(64, ONNXState.TargetHeight - (ONNXState.TargetHeight % 8));
	const int32 LatentWidth = FMath::Max(8, SafeWidth / 8);
	const int32 LatentHeight = FMath::Max(8, SafeHeight / 8);
	const int32 EffectiveSeed = ONNXState.Seed == 0 ? 1337 : ONNXState.Seed;

	const TArray<int32> PromptTokens = TokenizePrompt(*Cache, ONNXState.Prompt);
	const TArray<int32> NegativeTokens = TokenizePrompt(*Cache, ONNXState.NegativePrompt);

	TArray<float> PromptEmbeddings;
	TArray<float> NegativeEmbeddings;
	if (!RunTextEncoderModel(*Cache, PromptTokens, PromptEmbeddings)
		|| !RunTextEncoderModel(*Cache, NegativeTokens, NegativeEmbeddings))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Generate failed: text encoder inference failed");
		return;
	}

	TArray<float> Latents;
	Latents.SetNum(LatentWidth * LatentHeight * 4);
	FRandomStream Random(EffectiveSeed);
	for (float& Value : Latents)
	{
		Value = Random.GetFraction() * 2.0f - 1.0f;
	}

	const TArray<float> AlphaCumprod = BuildAlphaCumprod(1000, 0.00085f, 0.012f);
	const int32 StepCount = FMath::Max(1, ONNXState.StepCount);
	const TArray<int32> Timesteps = BuildInferenceTimesteps(StepCount, 1000, 1);

	for (int32 StepIndex = 0; StepIndex < Timesteps.Num(); ++StepIndex)
	{
		const int32 Timestep = Timesteps[StepIndex];
		const int32 PrevTimestep = (StepIndex + 1) < Timesteps.Num() ? Timesteps[StepIndex + 1] : -1;

		TArray<float> NegativeNoise;
		TArray<float> PromptNoise;
		if (!RunUNetModel(*Cache, Latents, LatentWidth, LatentHeight, Timestep, NegativeEmbeddings, NegativeNoise)
			|| !RunUNetModel(*Cache, Latents, LatentWidth, LatentHeight, Timestep, PromptEmbeddings, PromptNoise))
		{
			ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
			ONNXState.LastStatus = TEXT("Generate failed: UNet inference failed");
			return;
		}

		TArray<float> GuidedNoise;
		GuidedNoise.SetNum(NegativeNoise.Num());
		for (int32 Index = 0; Index < GuidedNoise.Num(); ++Index)
		{
			GuidedNoise[Index] = NegativeNoise[Index] + ONNXState.CFGScale * (PromptNoise[Index] - NegativeNoise[Index]);
		}

		StepDDIM(AlphaCumprod, Timestep, PrevTimestep, Latents, GuidedNoise);
	}

	for (float& Value : Latents)
	{
		Value /= 0.18215f;
	}

	TArray<float> DecodedImage;
	if (!RunVAEDecoderModel(*Cache, Latents, LatentWidth, LatentHeight, SafeWidth, SafeHeight, DecodedImage))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Generate failed: VAE decoder inference failed");
		return;
	}

	TArray<uint8> RGB;
	RGB.SetNumZeroed(SafeWidth * SafeHeight * 3);
	for (int32 PixelIndex = 0; PixelIndex < SafeWidth * SafeHeight; ++PixelIndex)
	{
		const int32 X = PixelIndex % SafeWidth;
		const int32 Y = PixelIndex / SafeWidth;
		const int32 BaseIndex = Y * SafeWidth + X;
		const float R = DecodedImage[(0 * SafeHeight + Y) * SafeWidth + X];
		const float G = DecodedImage[(1 * SafeHeight + Y) * SafeWidth + X];
		const float B = DecodedImage[(2 * SafeHeight + Y) * SafeWidth + X];
		RGB[BaseIndex * 3 + 0] = static_cast<uint8>(FMath::Clamp((R * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
		RGB[BaseIndex * 3 + 1] = static_cast<uint8>(FMath::Clamp((G * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
		RGB[BaseIndex * 3 + 2] = static_cast<uint8>(FMath::Clamp((B * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
	}

	const int32 OutputWidth = SafeWidth;
	const int32 OutputHeight = SafeHeight;

	const FString OutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Renders") / TEXT("ONNXRuntime"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDir);

	const FString OutputPath = OutputDir / FString::Printf(TEXT("onnx_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	if (!SaveRgbImageToPng(OutputPath, OutputWidth, OutputHeight, RGB))
	{
		ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Error;
		ONNXState.LastStatus = TEXT("Generate failed: unable to save output PNG");
		return;
	}

	ONNXState.LastOutputImagePath = OutputPath;
	ONNXState.RuntimeState = EMetaAgentONNXRuntimeState::Loaded;
	ONNXState.LastStatus = FString::Printf(TEXT("Image generated: %s"), *OutputPath);

	UE_LOG(LogMetaAgent, Log, TEXT("ONNXRuntime: image generated at '%s'."), *OutputPath);
}
