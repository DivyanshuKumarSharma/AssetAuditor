#include "Rules/AssetAuditRule_Unused.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

UAssetAuditRule_Unused::UAssetAuditRule_Unused()
{
	// Every newly created asset is "unused" at the moment it's first saved, so reporting this through
	// Data Validation on save would be pure noise. It stays on for manual scans and CI.
	bRunDuringDataValidation = false;

	IgnoredPaths.AddDefaulted_GetRef().Path = TEXT("/Game/Developers");
}

bool UAssetAuditRule_Unused::AppliesTo(const FAssetData& Asset) const
{
	// Maps are roots: nothing references a level, the game loads it by name.
	if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
	{
		return false;
	}

	const FString PackagePath = Asset.PackagePath.ToString();
	for (const FDirectoryPath& Ignored : IgnoredPaths)
	{
		if (!Ignored.Path.IsEmpty() && (PackagePath == Ignored.Path || PackagePath.StartsWith(Ignored.Path + TEXT("/"))))
		{
			return false;
		}
	}
	return true;
}

void UAssetAuditRule_Unused::Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Package-level dependency graph, hard and soft references alike. This is a lookup in the
	// registry's in-memory graph; nothing is loaded.
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(Asset.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
	Referencers.Remove(Asset.PackageName);
	if (Referencers.Num() > 0)
	{
		return;
	}

	// Primary assets (per Asset Manager settings) are cooked and loaded by id, so they're roots too.
	if (UAssetManager::IsInitialized() && UAssetManager::Get().GetPrimaryAssetIdForData(Asset).IsValid())
	{
		return;
	}

	AddIssue(OutIssues, Asset, EAssetAuditSeverity::Info,
		LOCTEXT("Unused", "Nothing references this asset. Check for code/config string references before deleting."));
}

#undef LOCTEXT_NAMESPACE
