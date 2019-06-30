// Copyright 2016-2018 mik14a / Admix Network. All Rights Reserved.

#include "VoxelFactory.h"
#include <ApexDestructibleAssetImport.h>
#include <DestructibleMesh.h>
#include <Editor.h>
#include <EditorFramework/AssetImportData.h>
#include <Engine/SkeletalMesh.h>
#include <Engine/StaticMesh.h>
#include <HAL/FileManager.h>
#include <Materials/MaterialExpressionVectorParameter.h>
#include <Materials/MaterialInstanceConstant.h>
#include <PhysicsEngine/BodySetup.h>
#include <PhysicsEngine/BoxElem.h>
#include <RawMesh.h>
#include "VOX.h"
#include "VoxAssetImportData.h"
#include "VoxImportOption.h"
#include "Voxel.h"
#include "Runtime/Engine/Classes/PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelFactory, Log, All)

UVoxelFactory::UVoxelFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ImportOption(nullptr)
	, bShowOption(true)
{
	Formats.Add(TEXT("vox;MagicaVoxel"));

	bCreateNew = false;
	bText = false;
	bEditorImport = true;
}

void UVoxelFactory::PostInitProperties()
{
	Super::PostInitProperties();
	ImportOption = NewObject<UVoxImportOption>(this, NAME_None, RF_NoFlags);
}

bool UVoxelFactory::DoesSupportClass(UClass * Class)
{
	return Class == UStaticMesh::StaticClass()
		|| Class == USkeletalMesh::StaticClass()
		|| Class == UDestructibleMesh::StaticClass()
		|| Class == UVoxel::StaticClass();
}

UClass* UVoxelFactory::ResolveSupportedClass()
{
	UClass* Class = nullptr;
	if (ImportOption->VoxImportType == EVoxImportType::StaticMesh) {
		Class = UStaticMesh::StaticClass();
	} else if (ImportOption->VoxImportType == EVoxImportType::SkeletalMesh) {
		Class = USkeletalMesh::StaticClass();
	} else if (ImportOption->VoxImportType == EVoxImportType::DestructibleMesh) {
		Class = UDestructibleMesh::StaticClass();
	} else if (ImportOption->VoxImportType == EVoxImportType::Voxel) {
		Class = UVoxel::StaticClass();
	}
	return Class;
}

UObject* UVoxelFactory::FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	UObject* Result = nullptr;	
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

	bool bImportAll = true;
	if (!bShowOption || ImportOption->GetImportOption(bImportAll)) {
		bShowOption = !bImportAll;
		FBufferReader Reader((void*)Buffer, BufferEnd - Buffer, false);		
		if (bImportAll)
		{
			FVoxFileInfo voxArch(GetVoxArchiveInfo(Reader));
			bool importMaterials = ImportOption->bImportMaterial;
			UMaterialInterface* mat = nullptr;
			
			if (voxArch.valid) for (int32 i = 0; i < voxArch.voxels.Num(); ++i) {
				//name model either by given name in magicavoxel+index or as filename+index int
				FName leName = InName;
				FString modelName = voxArch.GetName(i);
				if (modelName.IsEmpty()) modelName = FPaths::GetBaseFilename(voxArch.archiveName);
				modelName.AppendInt(i);//in case there can be multiple models with same name in archive
				leName = *modelName;

				FVox Vox;
				Vox.Filename = GetCurrentFilename();// modelName;
				Vox.Size = voxArch.sizes[i];
				Vox.Voxel = voxArch.voxels[i];
				Vox.modelName = modelName;
				Vox.Palette = voxArch.palette;
				//import material only for first model if any
				ImportOption->bImportMaterial = importMaterials && i == 0;

				switch (ImportOption->VoxImportType) {
				case EVoxImportType::StaticMesh:
					Result = CreateStaticMesh(InParent, leName, Flags, &Vox);					
					break;
				case EVoxImportType::SkeletalMesh:
					Result = CreateSkeletalMesh(InParent, leName, Flags, &Vox);
					break;
				case EVoxImportType::DestructibleMesh:
					Result = CreateDestructibleMesh(InParent, leName, Flags, &Vox);
					break;
				case EVoxImportType::Voxel:
					Result = CreateVoxel(InParent, leName, Flags, &Vox);
					break;
				default:
					break;
				}
				
				//failed attempt to assign same material to all imported meshes, prolly they need to be saved before material can be assigned
				/*
				if (importMaterials && i == 0) {
					//if this is first model then save the material ref
					UStaticMesh* mesh = Cast<UStaticMesh>(Result);
					if (mesh)
						mat = mesh->GetMaterial(0)->GetMaterial();
				}
				if (importMaterials && i > 0)
				{
					//for rest of models use material from first one
					UStaticMesh* mesh = Cast<UStaticMesh>(Result);
					if (mesh && mat)
						mesh->SetMaterial(0, mat);
				}*/			
			}
		} else
		{
			FVox Vox(GetCurrentFilename(), Reader, ImportOption, false);
			switch (ImportOption->VoxImportType) {
			case EVoxImportType::StaticMesh:
				Result = CreateStaticMesh(InParent, InName, Flags, &Vox);
				break;
			case EVoxImportType::SkeletalMesh:
				Result = CreateSkeletalMesh(InParent, InName, Flags, &Vox);
				break;
			case EVoxImportType::DestructibleMesh:
				Result = CreateDestructibleMesh(InParent, InName, Flags, &Vox);
				break;
			case EVoxImportType::Voxel:
				Result = CreateVoxel(InParent, InName, Flags, &Vox);
				break;
			default:
				break;
			}
		}
	}		
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Result);
	return Result;
}

bool UVoxelFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);
	UVoxel* Voxel = Cast<UVoxel>(Obj);

	const auto& AssetImportData = StaticMesh != nullptr ? StaticMesh->AssetImportData
		: SkeletalMesh != nullptr ? SkeletalMesh->AssetImportData
		: DestructibleMesh != nullptr ? DestructibleMesh->AssetImportData
		: Voxel != nullptr ? Voxel->AssetImportData
		: nullptr;
	if (AssetImportData != nullptr) {
		const auto& SourcePath = AssetImportData->GetFirstFilename();
		FString Path, Filename, Extension;
		FPaths::Split(SourcePath, Path, Filename, Extension);
		if (Extension.Compare("vox", ESearchCase::IgnoreCase) == 0) {
			AssetImportData->ExtractFilenames(OutFilenames);
			return true;
		}
	}
	return false;
}

void UVoxelFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);
	UVoxel* Voxel = Cast<UVoxel>(Obj);

	const auto& AssetImportData = StaticMesh ? StaticMesh->AssetImportData
		: SkeletalMesh ? SkeletalMesh->AssetImportData
		: DestructibleMesh ? DestructibleMesh->AssetImportData
		: Voxel ? Voxel->AssetImportData
		: nullptr;
	if (AssetImportData && ensure(NewReimportPaths.Num() == 1)) {
		AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UVoxelFactory::Reimport(UObject* Obj)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	UDestructibleMesh* DestructibleMesh = Cast<UDestructibleMesh>(Obj);
	UVoxel* Voxel = Cast<UVoxel>(Obj);

	const auto& AssetImportData = StaticMesh ? Cast<UVoxAssetImportData>(StaticMesh->AssetImportData)
		: SkeletalMesh ? Cast<UVoxAssetImportData>(SkeletalMesh->AssetImportData)
		: DestructibleMesh ? Cast<UVoxAssetImportData>(DestructibleMesh->AssetImportData)
		: Voxel ? Cast<UVoxAssetImportData>(Voxel->AssetImportData)
		: nullptr;
	if (!AssetImportData) {
		return EReimportResult::Failed;
	}

	const auto& Filename = AssetImportData->GetFirstFilename();
	if (!Filename.Len() || IFileManager::Get().FileSize(*Filename) == INDEX_NONE) {
		return EReimportResult::Failed;
	}

	auto Result = EReimportResult::Failed;
	auto OutCanceled = false;
	AssetImportData->ToVoxImportOption(*ImportOption);
	bShowOption = false;
	if (ImportObject(Obj->GetClass(), Obj->GetOuter(), *Obj->GetName(), RF_Public | RF_Standalone, Filename, nullptr, OutCanceled) != nullptr) {
		UE_LOG(LogVoxelFactory, Verbose, TEXT("Reimport successfully."));
		AssetImportData->Update(Filename);
		if (Obj->GetOuter()) {
			Obj->GetOuter()->MarkPackageDirty();
		} else {
			Obj->MarkPackageDirty();
		}
		Result = EReimportResult::Succeeded;
	} else {
		if (OutCanceled) {
			UE_LOG(LogVoxelFactory, Warning, TEXT("Reimport canceled."));
		} else {
			UE_LOG(LogVoxelFactory, Warning, TEXT("Reimport failed."));
		}
		Result = EReimportResult::Failed;
	}
	return Result;
}

UStaticMesh* UVoxelFactory::CreateStaticMesh(UObject* InParent, FName InName, EObjectFlags Flags, const FVox* Vox) const
{
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(InParent, InName, Flags | RF_Public);
	if (!StaticMesh->AssetImportData || !StaticMesh->AssetImportData->IsA<UVoxAssetImportData>()) {
		auto AssetImportData = NewObject<UVoxAssetImportData>(StaticMesh);
		AssetImportData->FromVoxImportOption(*ImportOption);
		StaticMesh->AssetImportData = AssetImportData;
	}

	FRawMesh RawMesh;
	Vox->CreateOptimizedRawMesh(RawMesh, ImportOption);
	UMaterialInterface* Material = CreateMaterial(InParent, InName, Flags, Vox);
	StaticMesh->StaticMaterials.Add(FStaticMaterial(Material));
	BuildStaticMesh(StaticMesh, RawMesh);
	if (ImportOption->bComplexCollisionAsSimple)
		StaticMesh->BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	StaticMesh->AssetImportData->Update(Vox->Filename);	
	return StaticMesh;
}

USkeletalMesh* UVoxelFactory::CreateSkeletalMesh(UObject* InParent, FName InName, EObjectFlags Flags, const FVox* Vox) const
{
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(InParent, InName, Flags | RF_Public);
	if (!SkeletalMesh->AssetImportData || !SkeletalMesh->AssetImportData->IsA<UVoxAssetImportData>()) {
		auto AssetImportData = NewObject<UVoxAssetImportData>(SkeletalMesh);
		AssetImportData->FromVoxImportOption(*ImportOption);
		SkeletalMesh->AssetImportData = AssetImportData;
	}
	if (ImportOption->bComplexCollisionAsSimple)
		SkeletalMesh->BodySetup->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	SkeletalMesh->AssetImportData->Update(Vox->Filename);
	return SkeletalMesh;
}

/**
 * CreateDestructibleMesh
 * @param InParent Import package
 * @param InName Package name
 * @param Flags Import flags
 * @param Vox Voxel file data
 */
UDestructibleMesh* UVoxelFactory::CreateDestructibleMesh(UObject* InParent, FName InName, EObjectFlags Flags, const FVox* Vox) const
{
	UDestructibleMesh* DestructibleMesh = NewObject<UDestructibleMesh>(InParent, InName, Flags | RF_Public);
	if (!DestructibleMesh->AssetImportData || !DestructibleMesh->AssetImportData->IsA<UVoxAssetImportData>()) {
		auto AssetImportData = NewObject<UVoxAssetImportData>(DestructibleMesh);
		AssetImportData->FromVoxImportOption(*ImportOption);
		DestructibleMesh->AssetImportData = AssetImportData;
	}

	FRawMesh RawMesh;
	Vox->CreateOptimizedRawMesh(RawMesh, ImportOption);
	UMaterialInterface* Material = CreateMaterial(InParent, InName, Flags, Vox);
	UStaticMesh* RootMesh = NewObject<UStaticMesh>();
	RootMesh->StaticMaterials.Add(FStaticMaterial(Material));
	BuildStaticMesh(RootMesh, RawMesh);
	DestructibleMesh->SourceStaticMesh = RootMesh;

	TArray<FRawMesh> RawMeshes;
	Vox->CreateRawMeshes(RawMeshes, ImportOption);
	TArray<UStaticMesh*> FractureMeshes;
	for (FRawMesh& RawMesh : RawMeshes) {
		UStaticMesh* FructureMesh = NewObject<UStaticMesh>();
		FructureMesh->StaticMaterials.Add(FStaticMaterial(Material));
		BuildStaticMesh(FructureMesh, RawMesh);
		FractureMeshes.Add(FructureMesh);
	}
	DestructibleMesh->SetupChunksFromStaticMeshes(FractureMeshes);
	BuildDestructibleMeshFromFractureSettings(*DestructibleMesh, nullptr);
	DestructibleMesh->SourceStaticMesh = nullptr;
	DestructibleMesh->AssetImportData->Update(Vox->Filename);

	return DestructibleMesh;
}

UVoxel* UVoxelFactory::CreateVoxel(UObject* InParent, FName InName, EObjectFlags Flags, const FVox* Vox) const
{
	UVoxel* Voxel = NewObject<UVoxel>(InParent, InName, Flags | RF_Public);
	if (!Voxel->AssetImportData || !Voxel->AssetImportData->IsA<UVoxAssetImportData>()) {
		auto AssetImportData = NewObject<UVoxAssetImportData>(Voxel);
		AssetImportData->FromVoxImportOption(*ImportOption);
		Voxel->AssetImportData = AssetImportData;
	}
	Voxel->Size = Vox->Size;
	TArray<uint8> Palette;
	for (const auto& cell : Vox->Voxel) {
		Palette.AddUnique(cell.Value);
	}

	UMaterial* Material = NewObject<UMaterial>(InParent, *FString::Printf(TEXT("%s_MT"), *InName.GetPlainNameString()), Flags | RF_Public);
	Material->TwoSided = false;
	Material->SetShadingModel(MSM_DefaultLit);
	UMaterialExpressionVectorParameter* Expression = NewObject<UMaterialExpressionVectorParameter>(Material);
	Expression->ParameterName = TEXT("Color");
	Expression->DefaultValue = FLinearColor::Gray;
	Expression->MaterialExpressionEditorX = -250;
	Material->Expressions.Add(Expression);
	Material->BaseColor.Expression = Expression;
	Material->PostEditChange();

	for (uint8 color : Palette) {
		UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(InParent, *FString::Printf(TEXT("%s_MI%d"), *InName.GetPlainNameString(), color), Flags | RF_Public);
		MaterialInstance->SetParentEditorOnly(Material);
		FLinearColor LinearColor = FLinearColor::FromSRGBColor(Vox->Palette[color - 1]);
		MaterialInstance->SetVectorParameterValueEditorOnly(TEXT("Color"), LinearColor);

		FRawMesh RawMesh;
		FVox::CreateMesh(RawMesh, ImportOption);
		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(InParent, *FString::Printf(TEXT("%s_SM%d"), *InName.GetPlainNameString(), color), Flags | RF_Public);
		StaticMesh->StaticMaterials.Add(FStaticMaterial(MaterialInstance));
		BuildStaticMesh(StaticMesh, RawMesh);

		const FVector& Scale = ImportOption->GetBuildSettings().BuildScale3D;
		FKBoxElem BoxElem(Scale.X, Scale.Y, Scale.Z);
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);

		Voxel->Mesh.Add(StaticMesh);
	}
	for (const auto& cell : Vox->Voxel) {
		Voxel->Voxel.Add(cell.Key, Palette.IndexOfByKey(cell.Value));
		check(INDEX_NONE != Palette.IndexOfByKey(cell.Value));
	}
	Voxel->bXYCenter = ImportOption->bImportXYCenter;
	Voxel->CalcCellBounds();
	Voxel->AssetImportData->Update(Vox->Filename);
	return Voxel;
}

UStaticMesh* UVoxelFactory::BuildStaticMesh(UStaticMesh* OutStaticMesh, FRawMesh& RawMesh) const
{
	check(OutStaticMesh);
	FStaticMeshSourceModel* StaticMeshSourceModel = new(OutStaticMesh->SourceModels) FStaticMeshSourceModel();
	StaticMeshSourceModel->BuildSettings = ImportOption->GetBuildSettings();
	StaticMeshSourceModel->RawMeshBulkData->SaveRawMesh(RawMesh);
	TArray<FText> Errors;
	OutStaticMesh->Build(false, &Errors);
	return OutStaticMesh;
}

UMaterialInterface* UVoxelFactory::CreateMaterial(UObject* InParent, FName& InName, EObjectFlags Flags, const FVox* Vox) const
{
	if (ImportOption->bImportMaterial) {
		UMaterial* Material = NewObject<UMaterial>(InParent, *FString::Printf(TEXT("%s_MT"), *InName.GetPlainNameString()), Flags | RF_Public);
		UTexture2D * Texture = NewObject<UTexture2D>(InParent, *FString::Printf(TEXT("%s_TX"), *InName.GetPlainNameString()), Flags | RF_Public);
		if (Vox->CreateTexture(Texture, ImportOption)) {
			Material->TwoSided = false;
			Material->SetShadingModel(MSM_DefaultLit);
			UMaterialExpressionTextureSample* Expression = NewObject<UMaterialExpressionTextureSample>(Material);
			Material->Expressions.Add(Expression);
			Material->BaseColor.Expression = Expression;
			Expression->Texture = Texture;
			Material->PostEditChange();
		}
		return Material;
	}
	else return NewObject<UMaterial>(InParent);
}

FVoxFileInfo UVoxelFactory::GetVoxArchiveInfo(FArchive& Ar)
{
	/** Magic number ( 'V' 'O' 'X' 'space' ) and terminate */
	ANSICHAR MagicNumber[5];
	/** version number ( current version is 150 ) */
	uint32 VersionNumber = 0;

	Ar.Serialize(MagicNumber, 4);

	FVoxFileInfo info;
	info.archiveName = GetCurrentFilename();
	info.versionNumber = VersionNumber;
	info.valid = true;

	if (0 != FCStringAnsi::Strncmp("VOX ", MagicNumber, 4)) {
		UE_LOG(LogVoxelFactory, Error, TEXT("not a vox format"));
		info.valid = false;
	}
	UE_LOG(LogVoxelFactory, Verbose, TEXT("MAGIC NUMBER: %s"), &MagicNumber);

	Ar << VersionNumber;
	UE_LOG(LogVoxelFactory, Display, TEXT("VERSION NUMBER: %d"), VersionNumber);

	if (150 < VersionNumber) {
		UE_LOG(LogVoxelFactory, Error, TEXT("unsupported version."));
		info.valid = false;
	}
	if (!info.valid) return info;

	ANSICHAR ChunkId[5] = { 0, };
	uint32 SizeOfChunkContents;
	uint32 TotalSizeOfChildrenChunks;

	FIntVector Size;


	do {
		Ar.Serialize(ChunkId, 4);
		Ar << SizeOfChunkContents;
		Ar << TotalSizeOfChildrenChunks;
		if (0 == FCStringAnsi::Strncmp("MAIN", ChunkId, 4)) {
			//UE_LOG(LogVox, Display, TEXT("MAIN: "));
		}
		else if (0 == FCStringAnsi::Strncmp("PACK", ChunkId, 4)) {
			//apparently this is obsolete and not used when there is scene with multiple models
			//UE_LOG(LogVox, Display, TEXT("PACK:"));
			int NumModels;
			Ar << NumModels;
			//UE_LOG(LogVox, Display, TEXT("      NumModels %d"), NumModels);
		}
		//nTRN = transform Node Chunk : "nTRN". we use it to read scene model name
		else if (0 == FCStringAnsi::Strncmp("nTRN", ChunkId, 4)) {
			/*
			int32	: node id
			DICT	: node attributes
				  (_name : string)
				  (_hidden : 0/1)
			int32 	: child node id
			int32 	: reserved id (must be -1)
			int32	: layer id
			int32	: num of frames (must be 1)

			// for each frame
			{
			DICT	: frame attributes
				  (_r : int8) ROTATION, see (c)
				  (_t : int32x3) translation
			}xN
			*/
			int64 posStart = Ar.Tell();
			int32 nodeId, childId, reserverdId, layerId, frames = -1;
			Ar << nodeId;
			//UE_LOG(LogVox, Display, TEXT("transform chunk Id: %d"), nodeId);
			TMap<FString, FString> attribs = FVox::ReadVoxDictionary(Ar);
			FString modelName;
			if (attribs.Num() > 0) {
				FString chunkIdStr;
				chunkIdStr.AppendChars(ANSI_TO_TCHAR(ChunkId), 5);//this is not chunk id we are looking for..

				
				modelName = *attribs.Find("_name");
				
			}
			//add either empty or valid name to list
			info.names.Add(modelName);
			//read some more
			Ar << childId << reserverdId << layerId << frames;
			//UE_LOG(LogVox, Display, TEXT("childId: %d"), childId);
			//not really interested in frame attributes but need to read them anyways
			for (int i = 0; i < frames; i++) {
				TMap<FString, FString> frameAttrib = FVox::ReadVoxDictionary(Ar);
			}
			int64 posEnd = Ar.Tell();
			int bytesRead = posEnd - posStart;
			int bytesRemaining = SizeOfChunkContents - bytesRead;

			//debug- consume rest of remaining bytes and don't crash
			uint8 byte;
			for (int i = 0; i < bytesRemaining; ++i) {
				Ar << byte;
			}

		}
		else if (0 == FCStringAnsi::Strncmp("SIZE", ChunkId, 4)) {
			
			Ar << Size.X << Size.Y << Size.Z;
			if (ImportOption->bImportXForward) {
				int32 temp = Size.X;
				Size.X = Size.Y;
				Size.Y = temp;
			}
			//UE_LOG(LogVox, Display, TEXT("SIZE: %s"), *Size.ToString());
			info.sizes.Add(Size);
		}
		else if (0 == FCStringAnsi::Strncmp("XYZI", ChunkId, 4)) {
			uint32 NumVoxels;
			Ar << NumVoxels;
			//UE_LOG(LogVox, Display, TEXT("XYZI: NumVoxels=%d"), NumVoxels);
			uint8 X, Y, Z, I;
			TMap<FIntVector, uint8> voxel;
			for (uint32 i = 0; i < NumVoxels; ++i) {
				Ar << X << Y << Z << I;
				if (ImportOption->bImportXForward) {
					uint8 temp = X;
					X = Size.X - Y - 1;
					Y = Size.Y - temp - 1;
				}
				else {
					X = Size.X - X - 1;
				}
				//UE_LOG(LogVox, Verbose, TEXT("      Voxel X=%d Y=%d Z=%d I=%d"), X, Y, Z, I);
				//Voxel.Add(FIntVector(X, Y, Z), I);
				voxel.Add(FIntVector(X, Y, Z), I);
			}
			info.voxels.Add(voxel);
		}
		else if (0 == FCStringAnsi::Strncmp("RGBA", ChunkId, 4)) {
			//UE_LOG(LogVox, Verbose, TEXT("RGBA:"));
			FColor Color;
			for (uint32 i = 0; i < SizeOfChunkContents / 4; ++i) {
				Ar << Color.R << Color.G << Color.B << Color.A;
				//UE_LOG(LogVox, Verbose, TEXT("      %s"), *Color.ToString());
				info.palette.Add(Color);
			}
		}
		else if (0 == FCStringAnsi::Strncmp("MATT", ChunkId, 4)) {
			//UE_LOG(LogVox, Warning, TEXT("Unsupported MATT chunk."));
			//btw: * the MATT chunk is deprecated, replaced by the MATL chunk, see (4)
			uint8 byte;
			for (uint32 i = 0; i < SizeOfChunkContents; ++i) {
				Ar << byte;
			}
		}
		else {
			FString UnknownChunk(ChunkId);
			//UE_LOG(LogVox, Warning, TEXT("Unsupported chunk [ %s ]. Skipping %d byte of chunk contents."), *UnknownChunk, SizeOfChunkContents);
			uint8 byte;
			for (uint32 i = 0; i < SizeOfChunkContents; ++i) {
				Ar << byte;
			}
		}
	} while (!Ar.AtEnd());

	return info;
}
