#pragma once

#include "AssetAuditRule.h"
#include "AssetAuditRule_StaticMesh.generated.h"

/** Static meshes need LODs (unless tiny or Nanite) and simple collision (unless deliberately collision-free). */
UCLASS(meta = (DisplayName = "Static Mesh LODs & Collision"))
class UAssetAuditRule_StaticMesh : public UAssetAuditRule
{
	GENERATED_BODY()

public:
	virtual bool AppliesTo(const FAssetData& Asset) const override;
	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const override;
	virtual bool ApplyFix(const FAssetAuditIssue& Issue) const override;

private:
	UPROPERTY(Config, EditAnywhere, Category = "LODs")
	bool bRequireLODs = true;

	/** LOD0 counts, so 2 means "at least one reduced LOD". */
	UPROPERTY(Config, EditAnywhere, Category = "LODs", meta = (ClampMin = "2", EditCondition = "bRequireLODs"))
	int32 MinLODCount = 2;

	/** A 12-triangle crate gains nothing from LODs; don't nag about it. */
	UPROPERTY(Config, EditAnywhere, Category = "LODs", meta = (ClampMin = "0", EditCondition = "bRequireLODs"))
	int32 MinTrianglesForLODs = 500;

	/** Nanite handles detail reduction itself, so classic LODs are not expected. */
	UPROPERTY(Config, EditAnywhere, Category = "LODs", meta = (EditCondition = "bRequireLODs"))
	bool bIgnoreNaniteMeshes = true;

	/** How many LODs (including LOD0) the "Generate LODs" fix creates; each halves the triangle count. */
	UPROPERTY(Config, EditAnywhere, Category = "LODs", meta = (ClampMin = "2", ClampMax = "8"))
	int32 GeneratedLODCount = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Collision")
	bool bRequireSimpleCollision = true;
};
