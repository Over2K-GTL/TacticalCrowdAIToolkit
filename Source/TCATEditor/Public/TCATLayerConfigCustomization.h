// Copyright 2025-2026 Over2K. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;
class SMenuAnchor;
class SEditableTextBox;

/**
 * Customizes FTCATBaseLayerConfig (Volume) and FTCATInfluenceConfigEntry (Component).
 * Adds a [+] button that opens a comprehensive "Tag Manager Popup" (Create + List + Delete).
 */
class FTCATLayerConfigCustomization : public IPropertyTypeCustomization
{
public:
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

private:
	/** Cached handle to the tag property being customized */
	TSharedPtr<IPropertyHandle> TargetTagHandle;

	/** The menu anchor widget instance */
	TSharedPtr<SMenuAnchor> ContextMenuAnchor;

	/** True if customizing a Composite Tag, False for Base Tag */
	bool bIsCompositeTarget = false;
};
