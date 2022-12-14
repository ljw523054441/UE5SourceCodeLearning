// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/ManagedArray.h"
#include "GeometryCollection/ManagedArrayTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GeometryCollection/GeometryCollectionSection.h"

class FSimulationProperties;

namespace Chaos
{
	class FChaosArchive;
}

/**
* ManagedArrayCollection
*
*   The ManagedArrayCollection is an entity system that implements a homogeneous, dynamically allocated, manager of
*   primitive TArray structures. The collection stores groups of TArray attributes, where each attribute within a
*   group is the same length. The collection will make use of the TManagedArray class, providing limited interaction
*   with the underlying TArray. 
*
*   For example:
*
	FManagedArrayCollection* Collection(NewObject<FManagedArrayCollection>());
	Collection->AddElements(10, "GroupBar"); // Create a group GroupBar and add 10 elements.
	Collection->AddAttribute<FVector3f>("AttributeFoo", "GroupBar"); // Add a FVector array named AttributeFoo to GroupBar.
	TManagedArray<FVector3f>&  Foo = Collection->GetAttribute<FVector3f>("AttributeFoo", "GroupBar"); // Get AttributeFoo
	for (int32 i = 0; i < Foo.Num(); i++)
	{
		Foo[i] = FVector(i, i, i); // Update AttribureFoo's elements
	}
*
*
*
*/
class CHAOS_API FManagedArrayCollection
{
	friend FSimulationProperties;

public:

	FManagedArrayCollection();
	virtual ~FManagedArrayCollection() {}

	FManagedArrayCollection(const FManagedArrayCollection&) = delete;
	FManagedArrayCollection& operator=(const FManagedArrayCollection&) = delete;
	FManagedArrayCollection(FManagedArrayCollection&&) = default;
	FManagedArrayCollection& operator=(FManagedArrayCollection&&) = default;

	static int8 Invalid;
	typedef EManagedArrayType EArrayType;

	/**
	*
	*/
	struct FConstructionParameters {

		FConstructionParameters(FName GroupIndexDependencyIn = ""
			, bool SavedIn = true)
			: GroupIndexDependency(GroupIndexDependencyIn)
			, Saved(SavedIn)
		{}

		FName GroupIndexDependency;
		bool Saved;
	};

	struct FProcessingParameters
	{
		FProcessingParameters() : bDoValidation(true), bReindexDependentAttibutes(true) {}

		bool bDoValidation ;
		bool bReindexDependentAttibutes;
	};

	/**
	* Add an attribute of Type(T) to the group
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	* @return reference to the created ManagedArray<T>
	*/
	template<typename T>
	TManagedArray<T>& AddAttribute(FName Name, FName Group, FConstructionParameters Parameters = FConstructionParameters())
	{
		if (!HasAttribute(Name, Group))
		{
			if (!HasGroup(Group))
			{
				AddGroup(Group);
			}

			FValueType Value(ManagedArrayType<T>(), *(new TManagedArray<T>()));
			Value.Value->Resize(NumElements(Group));
			Value.Saved = Parameters.Saved;
			if (ensure(!HasCycle(Group, Parameters.GroupIndexDependency)))
			{
				Value.GroupIndexDependency = Parameters.GroupIndexDependency;
			}
			else
			{
				Value.GroupIndexDependency = "";
			}
			Map.Add(FManagedArrayCollection::MakeMapKey(Name, Group), MoveTemp(Value));
		}
		return GetAttribute<T>(Name, Group);
	}

	/**
	* Add an external attribute of Type(T) to the group for size management. Lifetime is managed by the caller, must make sure the array is alive when the collection is
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	* @param ValueIn - The array to be managed
	*/
	template<typename T>
	void AddExternalAttribute(FName Name, FName Group, TManagedArray<T>& ValueIn, FConstructionParameters Parameters = FConstructionParameters())
	{
		check(!HasAttribute(Name, Group));

		if (!HasGroup(Group))
		{
			AddGroup(Group);
		}

		FValueType Value(ManagedArrayType<T>(), ValueIn);
		Value.Value->Resize(NumElements(Group));
		Value.Saved = Parameters.Saved;
		if (ensure(!HasCycle(Group, Parameters.GroupIndexDependency)))
		{
			Value.GroupIndexDependency = Parameters.GroupIndexDependency;
		}
		else
		{
			Value.GroupIndexDependency = "";
		}
		Value.bExternalValue = true;
		Map.Add(FManagedArrayCollection::MakeMapKey(Name, Group), MoveTemp(Value));
	}

	/**
	* Create a group on the collection. Adding attribute will also create unknown groups.
	* @param Group - The group name
	*/
	void AddGroup(FName Group);


	/**
	* List all the attributes in a group names.
	* @return list of group names
	*/
	TArray<FName> AttributeNames(FName Group) const;

	/**
	* Add elements to a group
	* @param NumberElements - The number of array entries to add
	* @param Group - The group to append entries to.
	* @return starting index of the new ManagedArray<T> entries.
	*/
	int32 AddElements(int32 NumberElements, FName Group);

	/**
	* Returns attribute(Name) of Type(T) from the group
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	* @return ManagedArray<T> &
	*/
	template<typename T>
	TManagedArray<T>* FindAttribute(FName Name, FName Group)
	{
		if (HasAttribute(Name, Group))
		{
			FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
			return static_cast<TManagedArray<T>*>(Map[Key].Value);
		}
		return nullptr;
	};

	template<typename T>
	const TManagedArray<T>* FindAttribute(FName Name, FName Group) const
	{
		if (HasAttribute(Name, Group))
		{
			FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
			return static_cast<TManagedArray<T>*>(Map[Key].Value);
		}
		return nullptr;
	};

	/**
	* Returns attribute(Name) of Type(T) from the group if and only if the types of T and the array match
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	* @return ManagedArray<T> &
	*/
	template<typename T>
	TManagedArray<T>* FindAttributeTyped(FName Name, FName Group)
	{
		if(HasAttribute(Name, Group))
		{
			FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
			FValueType& FoundValue = Map[Key];

			if(FoundValue.ArrayType == ManagedArrayType<T>())
			{
				return static_cast<TManagedArray<T>*>(Map[Key].Value);
			}
		}
		return nullptr;
	};

	template<typename T>
	const TManagedArray<T>* FindAttributeTyped(FName Name, FName Group) const
	{
		if(HasAttribute(Name, Group))
		{
			FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
			const FValueType& FoundValue = Map[Key];

			if(FoundValue.ArrayType == ManagedArrayType<T>())
			{
				return static_cast<TManagedArray<T>*>(Map[Key].Value);
			}
		}
		return nullptr;
	};

	/**
	* Returns attribute access of Type(T) from the group
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	* @return ManagedArray<T> &
	*/
	template<typename T>
	TManagedArray<T>& GetAttribute(FName Name, FName Group)
	{
		check(HasAttribute(Name, Group))
		FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
		return *(static_cast<TManagedArray<T>*>(Map[Key].Value));
	};

	template<typename T>
	const TManagedArray<T>& GetAttribute(FName Name, FName Group) const
	{
		check(HasAttribute(Name, Group))
		FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
		return *(static_cast<TManagedArray<T>*>(Map[Key].Value));
	};

	/**
	* Remove the element at index and reindex the dependent arrays 
	*/
	virtual void RemoveElements(const FName & Group, const TArray<int32> & SortedDeletionList, FProcessingParameters Params = FProcessingParameters());


	/**
	* Remove the attribute from the collection.
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	*/
	void RemoveAttribute(FName Name, FName Group);


	/**
	* Remove the group from the collection.
	* @param Group - The group that manages the attribute
	*/
	void RemoveGroup(FName Group);


	/**
	* List all the group names.
	*/
	TArray<FName> GroupNames() const;

	/**
	* Check for the existence of a attribute
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	*/
	bool HasAttribute(FName Name, FName Group) const;

	/**
	* Check for the existence of a group
	* @param Group - The group name
	*/
	FORCEINLINE bool HasGroup(FName Group) const { return GroupInfo.Contains(Group); }

	/**
	*
	*/
	void SetDependency(FName Name, FName Group, FName DependencyGroup);

	/**
	*
	*/
	void RemoveDependencyFor(FName Group);

	/**
	* Copy an attribute. Will perform an implicit group sync. Attribute must exist in both MasterCollection and this collection
	* @param Name - The name of the attribute
	* @param Group - The group that manages the attribute
	*/
	void CopyAttribute(const FManagedArrayCollection& InCollection, FName Name, FName Group);

	/**
	* Copy attributes that match the input collection. This is a utility to easily sync collections
	* @param InCollection - All groups from this collection found in the input will be sized accordingly
	* @param SkipList - Group/Attrs to skip. Keys are group names, values are attributes in those groups.
	*/
	void CopyMatchingAttributesFrom(const FManagedArrayCollection& InCollection, const TMap<FName, TSet<FName>>* SkipList=nullptr);

	/**
	* Number of elements in a group
	* @return the group size, and Zero(0) if group does not exist.
	*/
	int32 NumElements(FName Group) const;

	/**
	* Resize a group
	* @param Size - The size of the group
	* @param Group - The group to resize
	*/
	void Resize(int32 Size, FName Group);

	/**
	* Reserve a group
	* @param Size - The size of the group
	* @param Group - The group to reserve
	*/
	void Reserve(int32 Size, FName Group);

	/**
	* Empty the group 
	*  @param Group - The group to empty
	*/
	void EmptyGroup(FName Group);

	/**
	* Reorders elements in a group. NewOrder must be the same length as the group.
	*/
	virtual void ReorderElements(FName Group, const TArray<int32>& NewOrder);
#if 0	//not needed until we support per instance serialization
	/**
	* Swap elements within a group
	* @param Index1 - The first location to be swapped
	* @param Index2 - The second location to be swapped
	* @param Group - The group to resize
	*/
	void SwapElements(int32 Index1, int32 Index2, FName Group);
#endif

	/**
	* Updated for Render ( Mark it dirty ) question: is this the right place for this?
	*/
	void MakeDirty() { bDirty = true; }
	void MakeClean() { bDirty = false; }
	bool IsDirty() const { return bDirty; }

	/**
	* Serialize
		*/
	virtual void Serialize(Chaos::FChaosArchive& Ar);

	/**
	* Dump the contents to a FString
	*/
	FString ToString() const;

private:

	/****
	*  Mapping Key/Value
	*
	*    The Key and Value pairs for the attribute arrays allow for grouping
	*    of array attributes based on the Name,Group pairs. Array attributes
	*    that share Group names will have the same lengths.
	*
	*    The FKeyType is a tuple of <FName,FName> where the Get<0>() parameter
	*    is the AttributeName, and the Get<1>() parameter is the GroupName.
	*    Construction of the FKeyType will follow the following pattern:
	*    FKeyType("AttributeName","GroupName");
	*
	*/
	typedef TTuple<FName, FName> FKeyType;
	struct FGroupInfo
	{
		int32 Size;
	};

	static FKeyType MakeMapKey(FName Name, FName Group)
	{
		return FKeyType(Name, Group);
	};

	struct FValueType
	{
		EArrayType ArrayType;
		FName GroupIndexDependency;
		bool Saved;
		bool bExternalValue;	//External arrays have external memory management.

		FManagedArrayBase* Value;

		FValueType()
			: ArrayType(EArrayType::FNoneType)
			, GroupIndexDependency("")
			, Saved(true)
			, bExternalValue(false)
			, Value(nullptr) {};

		FValueType(EArrayType ArrayTypeIn, FManagedArrayBase& In)
			: ArrayType(ArrayTypeIn)
			, GroupIndexDependency("")
			, Saved(false)
			, bExternalValue(false)
			, Value(&In) {};

		
		FValueType(FValueType&& Other)
			: ArrayType(Other.ArrayType)
			, GroupIndexDependency(Other.GroupIndexDependency)
			, Saved(Other.Saved)
			, bExternalValue(Other.bExternalValue)
			, Value(Other.Value)
		{
			if (&Other != this)
			{
				Other.Value = nullptr;
			}
		}

		~FValueType()
		{
			if (Value && !bExternalValue)
			{
				delete Value;
			}
		}

		FValueType(const FValueType&) = delete;	//no copies because we are treating this as a simple unique ptr (not using TUniquePtr because it's only true when bExternalValue is false)
		FValueType& operator=(const FValueType& Other) = delete;
	};

	virtual void SetDefaults(FName Group, uint32 StartSize, uint32 NumElements) {};

	TMap< FKeyType, FValueType> Map;	//data is owned by the map explicitly
	TMap< FName, FGroupInfo> GroupInfo;
	bool bDirty;

	FName GetDependency(FName SearchGroup);
	bool HasCycle(FName Group, FName DependencyGroup);

protected:

	/**
	* Size and order a group so that it matches the group found in the input collection.
	* @param InCollection - The collection we are ordering our group against. 
	* @param Group - The group that manages the attribute
	*/
	void SyncGroupSizeFrom(const FManagedArrayCollection& InCollection, FName Group);

	/**
	 * Version to indicate need for conditioning to current expected data layout during serialization loading.
	 */
	int32 Version;


	/**
	*   operator<<(FGroupInfo)
	*/
	friend FArchive& operator<<(FArchive& Ar, FGroupInfo& GroupInfo);

	/**
	*   operator<<(FValueType)
	*/
	friend FArchive& operator<<(FArchive& Ar, FValueType& ValueIn);

};


/*
*  FManagedArrayInterface
*/
class CHAOS_API FManagedArrayInterface
{
public:
	FManagedArrayInterface() : ManagedCollection(nullptr) {}
	FManagedArrayInterface(FManagedArrayCollection* InManagedArray)
		: ManagedCollection(InManagedArray) {}

	virtual void InitializeInterface() = 0;
	virtual void CleanInterfaceForCook() = 0; 
	virtual void RemoveInterfaceAttributes() = 0;

protected:
	FManagedArrayCollection* ManagedCollection;

};
