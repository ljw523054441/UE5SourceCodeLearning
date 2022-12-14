// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/PackageWriterToSharedBuffer.h"

#include "Misc/ScopeRWLock.h"
#include "Serialization/LargeMemoryWriter.h"

TUniquePtr<FLargeMemoryWriter> IPackageWriter::CreateLinkerArchive(FName PackageName, UObject* Asset)
{
	// The LargeMemoryWriter does not need to be persistent; the LinkerSave wraps it and reports Persistent=true
	bool bIsPersistent = false; 
	return TUniquePtr<FLargeMemoryWriter>(new FLargeMemoryWriter(0, bIsPersistent, *PackageName.ToString()));
}

static FSharedBuffer IoBufferToSharedBuffer(const FIoBuffer& InBuffer)
{
	InBuffer.EnsureOwned();
	const uint64 DataSize = InBuffer.DataSize();
	FIoBuffer MutableBuffer(InBuffer);
	uint8* DataPtr = MutableBuffer.Release().ValueOrDie();
	return FSharedBuffer::TakeOwnership(DataPtr, DataSize, FMemory::Free);
};

void FPackageWriterRecords::BeginPackage(FPackage* Record, const IPackageWriter::FBeginPackageInfo& Info)
{
	checkf(Record, TEXT("TPackageWriterToSharedBuffer ConstructRecord must return non-null"));
	{
		FWriteScopeLock MapScopeLock(MapLock);
		TUniquePtr<FPackage>& Existing = Map.FindOrAdd(Info.PackageName);
		checkf(!Existing, TEXT("IPackageWriter->BeginPackage must not be called twice without calling CommitPackage."));
		Existing.Reset(Record);
	}
	Record->Begin = Info;
}

void FPackageWriterRecords::WritePackageData(const IPackageWriter::FPackageInfo& Info,
	FLargeMemoryWriter& ExportsArchive, const TArray<FFileRegion>& FileRegions)
{
	FPackage& Record = FindRecordChecked(Info.InputPackageName);
	int64 DataSize = ExportsArchive.TotalSize();
	checkf(DataSize > 0, TEXT("IPackageWriter->WritePackageData must not be called with an empty ExportsArchive"));
	checkf(static_cast<uint64>(DataSize) >= Info.HeaderSize,
		TEXT("IPackageWriter->WritePackageData must not be called with HeaderSize > ExportsArchive.TotalSize"));
	FSharedBuffer Buffer = FSharedBuffer::TakeOwnership(ExportsArchive.ReleaseOwnership(), DataSize,
		FMemory::Free);
	Record.Packages.Insert(FWritePackage{ Info, MoveTemp(Buffer), FileRegions }, Info.MultiOutputIndex);
}

void FPackageWriterRecords::WriteBulkData(const IPackageWriter::FBulkDataInfo& Info, const FIoBuffer& BulkData,
	const TArray<FFileRegion>& FileRegions)
{
	FPackage& Record = FindRecordChecked(Info.InputPackageName);
	Record.BulkDatas.Add(FBulkData{ Info, IoBufferToSharedBuffer(BulkData), FileRegions });
}

void FPackageWriterRecords::WriteAdditionalFile(const IPackageWriter::FAdditionalFileInfo& Info,
	const FIoBuffer& FileData)
{
	FPackage& Record = FindRecordChecked(Info.InputPackageName);
	Record.AdditionalFiles.Add(FAdditionalFile{ Info, IoBufferToSharedBuffer(FileData) });
}

void FPackageWriterRecords::WriteLinkerAdditionalData(const IPackageWriter::FLinkerAdditionalDataInfo& Info,
	const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions)
{
	FPackage& Record = FindRecordChecked(Info.InputPackageName);
	Record.LinkerAdditionalDatas.Add(
		FLinkerAdditionalData{ Info, IoBufferToSharedBuffer(Data), FileRegions });
}

FPackageWriterRecords::FPackage& FPackageWriterRecords::FindRecordChecked(FName InPackageName) const
{
	const TUniquePtr<FPackage>* Record;
	{
		FReadScopeLock MapScopeLock(MapLock);
		Record = Map.Find(InPackageName);
	}
	checkf(Record && *Record,
		TEXT("IPackageWriter->BeginPackage must be called before any other functions on IPackageWriter"));
	return **Record;
}

TUniquePtr<FPackageWriterRecords::FPackage> FPackageWriterRecords::FindAndRemoveRecordChecked(FName InPackageName)
{
	TUniquePtr<FPackage> Record;
	{
		FWriteScopeLock MapScopeLock(MapLock);
		Map.RemoveAndCopyValue(InPackageName, Record);
	}
	checkf(Record, TEXT("IPackageWriter->BeginPackage must be called before any other functions on IPackageWriter"));
	return Record;
}

void FPackageWriterRecords::ValidateCommit(FPackage& Record, const IPackageWriter::FCommitPackageInfo& Info) const
{
	checkf(Info.bSucceeded == false || Record.Packages.Num() > 0,
		TEXT("IPackageWriter->WritePackageData must be called before Commit if the Package save was successful."));
	checkf(Info.bSucceeded == false || Record.Packages.FindByPredicate([](const FPackageWriterRecords::FWritePackage& Package) { return Package.Info.MultiOutputIndex == 0; }),
		TEXT("SavePackage must provide output 0 when saving multioutput packages."));
	uint8 HasBulkDataType[IPackageWriter::FBulkDataInfo::NumTypes]{};
	for (FBulkData& BulkRecord : Record.BulkDatas)
	{
		checkf((HasBulkDataType[(int32)BulkRecord.Info.BulkDataType] & (1 << BulkRecord.Info.MultiOutputIndex)) == 0,
			TEXT("IPackageWriter->WriteBulkData must not be called with more than one BulkData of the same type."));
		HasBulkDataType[(int32)BulkRecord.Info.BulkDataType] |= 1 << BulkRecord.Info.MultiOutputIndex;
	}
}