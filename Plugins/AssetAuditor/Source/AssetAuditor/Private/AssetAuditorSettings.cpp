#include "AssetAuditorSettings.h"

UAssetAuditorSettings::UAssetAuditorSettings()
{
	DefaultScanPath.Path = TEXT("/Game");
}

bool UAssetAuditorSettings::IsExcluded(const FName PackagePath) const
{
	const FString Path = PackagePath.ToString();
	for (const FDirectoryPath& Excluded : ExcludedPaths)
	{
		// Compare whole folder names so excluding /Game/Art doesn't also exclude /Game/ArtTests.
		if (!Excluded.Path.IsEmpty() && (Path == Excluded.Path || Path.StartsWith(Excluded.Path + TEXT("/"))))
		{
			return true;
		}
	}
	return false;
}
