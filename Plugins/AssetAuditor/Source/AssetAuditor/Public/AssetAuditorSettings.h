#pragma once

#include "CoreMinimal.h"
#include "AssetAuditTypes.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "AssetAuditorSettings.generated.h"

/**
 * Global options, under Project Settings > Plugins > Asset Auditor.
 * UDeveloperSettings registers itself with the settings UI automatically; Config=Editor + DefaultConfig
 * means values are written to Config/DefaultEditor.ini, which is source-controlled and shared by the team.
 *
 * Per-rule thresholds deliberately do NOT live here: each rule owns its config (see UAssetAuditRule),
 * so adding a rule never requires editing this class.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "Asset Auditor"))
class ASSETAUDITOR_API UAssetAuditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAssetAuditorSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Folder the panel scans by default, and the commandlet scans when no -Path is given. */
	UPROPERTY(Config, EditAnywhere, Category = "Scanning", meta = (LongPackageName))
	FDirectoryPath DefaultScanPath;

	/** Never audited, by any rule (marketplace packs, generated content...). */
	UPROPERTY(Config, EditAnywhere, Category = "Scanning", meta = (LongPackageName))
	TArray<FDirectoryPath> ExcludedPaths;

	/** The commandlet exits with code 1 when any issue is at least this severe. Override with -FailOn=. */
	UPROPERTY(Config, EditAnywhere, Category = "Continuous Integration")
	EAssetAuditSeverity FailOnSeverity = EAssetAuditSeverity::Error;

	bool IsExcluded(const FName PackagePath) const;
};
