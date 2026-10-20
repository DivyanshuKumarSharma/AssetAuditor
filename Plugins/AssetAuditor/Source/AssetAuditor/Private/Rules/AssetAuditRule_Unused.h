#pragma once

#include "AssetAuditRule.h"
#include "Engine/EngineTypes.h"
#include "AssetAuditRule_Unused.generated.h"

/**
 * Assets that no other package references.
 * Info severity and no auto-fix on purpose: the Asset Registry can't see references made by string
 * paths in code or config, so a human must confirm before anything is deleted.
 */
UCLASS(meta = (DisplayName = "Unused Asset"))
class UAssetAuditRule_Unused : public UAssetAuditRule
{
	GENERATED_BODY()

public:
	UAssetAuditRule_Unused();

	virtual bool AppliesTo(const FAssetData& Asset) const override;
	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const override;

private:
	/** Folders whose content is expected to be unreferenced (personal sandboxes, test content...). */
	UPROPERTY(Config, EditAnywhere, Category = "Unused", meta = (LongPackageName))
	TArray<FDirectoryPath> IgnoredPaths;
};
