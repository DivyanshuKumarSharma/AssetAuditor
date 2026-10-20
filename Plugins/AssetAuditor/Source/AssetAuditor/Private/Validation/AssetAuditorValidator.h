#pragma once

#include "EditorValidatorBase.h"
#include "AssetAuditorValidator.generated.h"

/**
 * Bridges the audit rules into the engine's Data Validation framework, so the same checks run
 * on save, from "Validate Assets" in the Content Browser, and at source-control submit time.
 * The Data Validation subsystem discovers this class by reflection; no registration is needed.
 */
UCLASS()
class UAssetAuditorValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
