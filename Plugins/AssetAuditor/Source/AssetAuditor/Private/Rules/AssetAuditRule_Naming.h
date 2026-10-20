#pragma once

#include "AssetAuditRule.h"
#include "AssetAuditRule_Naming.generated.h"

USTRUCT()
struct FAssetAuditNamingConvention
{
	GENERATED_BODY()

	/** Soft reference so we can name classes from modules we don't link against (UMGEditor, Niagara...). */
	UPROPERTY(EditAnywhere, Category = "Naming", meta = (AllowAbstract = "true"))
	TSoftClassPtr<UObject> AssetClass;

	UPROPERTY(EditAnywhere, Category = "Naming")
	FString Prefix;
};

/** Asset names must start with the prefix configured for their type (T_, SM_, M_, MI_, BP_...). */
UCLASS(meta = (DisplayName = "Naming Convention"))
class UAssetAuditRule_Naming : public UAssetAuditRule
{
	GENERATED_BODY()

public:
	UAssetAuditRule_Naming();

	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const override;
	virtual bool ApplyFix(const FAssetAuditIssue& Issue) const override;

	/** The name the asset should have, or empty if it already complies or no convention covers its class. */
	FString GetExpectedName(const FAssetData& Asset) const;

private:
	/** Most specific match wins: an AnimBlueprint is also a Blueprint, but should get ABP_, not BP_. */
	const FAssetAuditNamingConvention* FindConvention(const UClass* AssetClass) const;

	UPROPERTY(Config, EditAnywhere, Category = "Naming")
	TArray<FAssetAuditNamingConvention> Conventions;
};
