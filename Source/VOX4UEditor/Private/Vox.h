// Copyright 2016-2018 mik14a / Admix Network. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <RawMesh.h>

class UTexture2D;
class UVoxImportOption;


/**
 * @struct FVox
 * VOX format implementation.
 * @see https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox.txt

 for updated version:
 https://github.com/ephtracy/voxel-model/blob/master/MagicaVoxel-file-format-vox-extension.txt
 */
struct FVox
{
	/** Filename */
	FString Filename;

	/** Magic number ( 'V' 'O' 'X' 'space' ) and terminate */
	ANSICHAR MagicNumber[5];
	/** version number ( current version is 150 ) */
	uint32 VersionNumber;

	/** Size */
	FIntVector Size;
	/** Voxel */
	TMap<FIntVector, uint8> Voxel;
	/** Palette */
	TArray<FColor> Palette;

	//scene model name
	FString modelName;

public:

	/** Create empty vox data */
	FVox();

	/** Create vox data from archive */
	FVox(const FString& Filename, FArchive& Ar, const UVoxImportOption* ImportOption, bool importSingleModel);

	/** Import vox data from archive */ //and merge them to single messy mesh
	bool Import(FArchive& Ar, const UVoxImportOption* ImportOption);

	//continues importing at current archive position
	bool ImportSingleModel(FArchive& Ar, const UVoxImportOption* ImportOption);

	/** Create FRawMesh from Voxel */
	bool CreateRawMesh(FRawMesh& OutRawMesh, const UVoxImportOption* ImportOption) const;

	/** Create FRawMesh from Voxel use Monotone mesh generation */
	bool CreateOptimizedRawMesh(FRawMesh& OutRawMesh, const UVoxImportOption* ImportOption) const;

	/** Create FRawMeshes from Voxel models array, use Monotone mesh generation */
	bool CreateOptimizedRawMeshes(TArray<FRawMesh>& OutRawMeshes, const UVoxImportOption* ImportOption) const;

	/** Create raw meshes from Voxel */
	bool CreateRawMeshes(TArray<FRawMesh>& OutRawMeshes, const UVoxImportOption* ImportOption) const;

	/** Create UTexture2D from Palette */
	bool CreateTexture(UTexture2D* const& OutTexture, UVoxImportOption* ImportOption) const;

	/** Create one raw mesh */
	static bool CreateMesh(FRawMesh& OutRawMesh, const UVoxImportOption* ImportOption);

	//read serialized string from vox archive into FString
	static FString ReadVoxString(FArchive& arch);

	//reads string map structure from vox file
	static TMap<FString, FString> ReadVoxDictionary(FArchive& arch);
};
