// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Core/TCATTypes.h"
#include "TCATCompositeRecipe.generated.h"
/**
 * One warning entry produced by UTCATCompositeRecipe::GatherSelfInfluenceWarnings().
 *
 * LayerTag:
 * - The input (source) map tag whose self-influence removal is considered disabled for this composite,
 *   based on the current operation sequence.
 *
 * Message:
 * - Localized, human-readable text describing which operation caused the disable.
 */
struct TCAT_API FTCATSelfInfluenceWarningMessage
{
    FTCATSelfInfluenceWarningMessage() = default;
    FTCATSelfInfluenceWarningMessage(FName InLayerTag, FText InMessage)
        : LayerTag(InLayerTag)
        , Message(MoveTemp(InMessage)){}

    FName LayerTag = NAME_None;
    FText Message;
};

UCLASS(BlueprintType)
class TCAT_API UTCATCompositeRecipe : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Ordered list of composition operations that defines how a composite map is built.
	 *
	 * Self-Influence Removal Notes:
	 * - TCAT derives per-source "reverse removal recipes" from this operation stream.
	 * - Some operations act as a boundary where source contribution tracking becomes disabled
	 *   for self-influence removal (see GatherSelfInfluenceWarnings()).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
	TArray<FTCATCompositeOperation> Operations;

	/** Editor dropdown options for base map tags (raw maps). */
	UFUNCTION()
	TArray<FString> GetBaseTagOptions() const;

	UFUNCTION()
	TArray<FString> GetCompositeTagOptions() const;

	UFUNCTION()
	TArray<FString> GetAllTagOptions() const;

	/**
	 * Builds authoring-time warnings about self-influence removal availability per input map.
	 *
	 * What this function does (exact behavior):
	 * - Scans Operations in order and tracks a set of "ActiveSources" (map tags) that are currently being
	 *   accumulated via linear ops (Add/Subtract).
	 * - When a boundary operation occurs, all currently ActiveSources are marked as "Disabled" and a warning
	 *   is emitted once per map tag (deduplicated by DisabledSources).
	 *
	 * Boundary operations (disable tracking in this warning heuristic):
	 * - Multiply / Divide:
	 *   * Marks the current Operation.InputMapTag (if any) as disabled.
	 *   * Marks all previously ActiveSources as disabled.
	 *   * Clears ActiveSources.
	 *
	 * - Normalize (ETCATCompositeOp::Normalize):
	 *   * Marks all previously ActiveSources as disabled.
	 *   * Clears ActiveSources.
	 *
	 * Non-boundary operations:
	 * - Add / Subtract:
	 *   * Adds Operation.InputMapTag into ActiveSources (unless it is already disabled).
	 *
	 * Important notes:
	 * - Per-operation input normalization (FTCATCompositeOperation::bNormalizeInput) is NOT treated as a boundary.
	 *   It is supported by the runtime reverse-recipe builder (it contributes to normalized coefficients).
	 * - Clamp (FTCATCompositeOperation::bClampInput) is NOT evaluated by this warning function.
	 *   Clamp can make self-influence removal approximate at runtime (see recipe flags), but it does not emit warnings here.
	 * - Invert is intentionally ignored because it has no InputMapTag and does not, by itself, disable tracking
	 *   under this heuristic.
	 * - Warnings are conservative and order-dependent: a map can be valid early in the stream and become
	 *   disabled after a later boundary operation.
	 */
	void GatherSelfInfluenceWarnings(TArray<FTCATSelfInfluenceWarningMessage>& OutWarnings) const;
};
