#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UnrealType.h"

namespace UE::AssetAuditor
{
	/** Read an integer asset registry tag. False if the tag is missing (e.g. asset saved by an older engine). */
	inline bool GetIntTag(const FAssetData& Asset, FName Tag, int32& OutValue)
	{
		FString Value;
		if (!Asset.GetTagValue(Tag, Value) || !Value.IsNumeric())
		{
			return false;
		}
		OutValue = FCString::Atoi(*Value);
		return true;
	}

	/**
	 * Change one property the same way the Details panel does:
	 *  Modify()               -> snapshots the object into the open transaction (Ctrl+Z works) and dirties the package
	 *  PreEditChange/PostEdit -> lets the asset react (textures rebuild, meshes re-cook) and notifies open editors
	 */
	template <typename ObjectType, typename MutatorType>
	void EditProperty(ObjectType* Object, FName PropertyName, MutatorType&& Mutate)
	{
		FProperty* Property = FindFProperty<FProperty>(ObjectType::StaticClass(), PropertyName);
		check(Property);

		Object->Modify();
		Object->PreEditChange(Property);
		Mutate();
		FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
		Object->PostEditChangeProperty(ChangedEvent);
	}
}
