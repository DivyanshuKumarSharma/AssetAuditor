#pragma once

#include "AssetAuditRule.h"
#include "AssetAuditRule_TextureSize.generated.h"

/** Textures must be power-of-two and no larger than a size budget. */
UCLASS(meta = (DisplayName = "Texture Size"))
class UAssetAuditRule_TextureSize : public UAssetAuditRule
{
	GENERATED_BODY()

public:
	virtual bool AppliesTo(const FAssetData& Asset) const override;
	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const override;
	virtual bool ApplyFix(const FAssetAuditIssue& Issue) const override;

private:
	/** True if the source size alone could violate a setting, i.e. it's worth loading the texture to look closer. */
	bool IsSuspicious(const FIntPoint& Size) const;

	/** Largest allowed side in pixels, after the texture's own Max Texture Size is applied. */
	UPROPERTY(Config, EditAnywhere, Category = "Texture", meta = (ClampMin = "1"))
	int32 MaxDimension = 2048;

	/** Non-power-of-two textures don't get mipmaps or streaming, which costs memory and causes shimmering. */
	UPROPERTY(Config, EditAnywhere, Category = "Texture")
	bool bRequirePowerOfTwo = true;

	/** UI textures are drawn 1:1 without mips, so non-power-of-two sizes are usually fine there. */
	UPROPERTY(Config, EditAnywhere, Category = "Texture", meta = (EditCondition = "bRequirePowerOfTwo"))
	bool bAllowNonPowerOfTwoForUI = true;
};
