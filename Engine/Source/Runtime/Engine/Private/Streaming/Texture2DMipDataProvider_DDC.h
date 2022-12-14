// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DMipDataProvider_DDC.h : Implementation of FTextureMipDataProvider using DDC requests.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Memory/SharedBuffer.h"
#include "Streaming/TextureMipDataProvider.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataRequestOwner.h"

/**
* FTexture2DMipDataProvider_DDC implements FTextureMipAllocator using DDC requests from FDerivedDataCacheInterface.
*/
class FTexture2DMipDataProvider_DDC : public FTextureMipDataProvider
{
public:

	FTexture2DMipDataProvider_DDC(const UTexture* Texture);
	~FTexture2DMipDataProvider_DDC();


	// ********************************************************
	// ********* FTextureMipDataProvider implementation **********
	// ********************************************************

	void Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions) final override;
	int32 GetMips(const FTextureUpdateContext& Context, int32 StartingMipIndex, const FTextureMipInfoArray& MipInfos, const FTextureUpdateSyncOptions& SyncOptions) final override;
	bool PollMips(const FTextureUpdateSyncOptions& SyncOptions) final override;
	void CleanUp(const FTextureUpdateSyncOptions& SyncOptions) final override;
	void Cancel(const FTextureUpdateSyncOptions& SyncOptions) final override;
	ETickThread GetCancelThread() const final override;

protected:

	// Helper to route incomplete DDC requests to the FAbandonedDDCHandleManager.
	void ReleaseDDCResources();

	bool SerializeMipInfo(const FTextureUpdateContext& Context, FArchive& Ar, int32 MipIndex, int64 MipSize, const FTextureMipInfo& OutMipInfo);

private:

	// The list of DDC handle for each mips (0 based on the highest mip).
	TArray<uint32, TInlineAllocator<MAX_TEXTURE_MIP_COUNT>> DDCHandles;
	TArray<FSharedBuffer, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > DDCBuffers;
	UE::DerivedData::FRequestOwner DDCRequestOwner;
};

#endif //WITH_EDITORONLY_DATA
