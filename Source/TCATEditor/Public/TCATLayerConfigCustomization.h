// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class SMenuAnchor;
class SEditableTextBox;
class UTCATCompositeRecipe;

/**
 * Customizes FTCATBaseLayerConfig(DisplayName = Base Map Config) (Volume) and FTCATInfluenceConfigEntry (Component).
 * Adds a [+] button that opens a comprehensive "Tag Manager Popup" (Create + List + Delete).
 */
class FTCATLayerConfigCustomization : public IPropertyTypeCustomization
{
public:
	FTCATLayerConfigCustomization();
	virtual ~FTCATLayerConfigCustomization() override;
	
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Callback for SMenuAnchor to generate content */
	TSharedRef<SWidget> HandleGetMenuContent();

	/** Generates the popup content (Create Input + Tag List) */
	TSharedRef<SWidget> GenerateMenuContent(TSharedPtr<IPropertyHandle> ChildHandle, FSimpleDelegate OnCloseMenu);

	/** Generates the list of existing tags with delete buttons */
	TSharedRef<SWidget> GenerateTagList(FSimpleDelegate OnCloseMenu);

	void RegisterNewTag(const FString& TagName);
	void DeleteTag(const FString& TagName);

	// --- Curve Filtering ---
	/** Filters assets to allow only those in specific TCAT folder */
	bool OnShouldFilterCurveAsset(const struct FAssetData& AssetData);

	// --- Composite Asset Warning Logic ---
	void OnCompositeAssetChanged();
	void HandleCompositeAssetPropertyChanged(UObject* Object, struct FPropertyChangedEvent& PropertyChangedEvent);
	void RefreshWarnings() const;
	FText GetWarningText() const;
	EVisibility GetWarningVisibility() const;
private:
	// Tag Customization Vars
	/** Cached handle to the tag property being customized */
	TSharedPtr<IPropertyHandle> TargetTagHandle;

	/** The menu anchor widget instance */
	TSharedPtr<SMenuAnchor> ContextMenuAnchor;

	/** True if customizing a Composite Tag, False for Base Tag */
	bool bIsCompositeTarget = false;

	// Composite Asset Warning Vars
	TSharedPtr<IPropertyHandle> CompositeAssetHandle;
	mutable TWeakObjectPtr<UTCATCompositeRecipe> CachedCompositeAsset;
	FDelegateHandle PropertyChangedHandle;

	mutable bool bWarningsDirty = true;
	mutable FText CachedWarningText;
	mutable int32 CachedWarningCount = 0;
};
