// Copyright 2025-2026 Over2K. All Rights Reserved.

#include "TCATLayerConfigCustomization.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Core/TCATSettings.h"
#include "Curves/CurveFloat.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "TCATLayerConfigCustomization"

TSharedRef<IPropertyTypeCustomization> FTCATLayerConfigCustomization::MakeInstance()
{
	return MakeShareable(new FTCATLayerConfigCustomization);
}

void FTCATLayerConfigCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
}

void FTCATLayerConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid()) continue;

		FName PropName = ChildHandle->GetProperty()->GetFName();

		// 1. Tag Customization (Base or Composite)
		bool bIsBaseTag = (PropName == FName("BaseLayerTag") || PropName == FName("MapTag"));
		bool bIsCompositeTag = (PropName == FName("CompositeLayerTag"));

		if (bIsBaseTag || bIsCompositeTag)
		{
			bIsCompositeTarget = bIsCompositeTag;
			TargetTagHandle = ChildHandle;

			ChildBuilder.AddProperty(ChildHandle.ToSharedRef())
				.CustomWidget()
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(250.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						ChildHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(ContextMenuAnchor, SMenuAnchor)
						.Placement(MenuPlacement_ComboBoxRight)
						.OnGetMenuContent(this, &FTCATLayerConfigCustomization::HandleGetMenuContent)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ToolTipText(LOCTEXT("ManageTagsTooltip", "Manage Tags (Create/Delete)"))
							.OnClicked_Lambda([this]()
							{
								if (ContextMenuAnchor.IsValid())
								{
									ContextMenuAnchor->SetIsOpen(!ContextMenuAnchor->IsOpen());
								}
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text(FText::FromString("+"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
								.ColorAndOpacity(FLinearColor::White)
							]
						]
					]
				];
		}
		// 2. Curve Customization (SObjectPropertyEntryBox with Filter)
		else if (PropName == FName("FalloffCurve"))
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef())
				.CustomWidget()
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(250.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.PropertyHandle(ChildHandle)
					.AllowedClass(UCurveFloat::StaticClass())
					.OnShouldFilterAsset(this, &FTCATLayerConfigCustomization::OnShouldFilterCurveAsset)
					.DisplayThumbnail(true) // Show Thumbnail!
				];
		}
		else
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}
}

TSharedRef<SWidget> FTCATLayerConfigCustomization::HandleGetMenuContent()
{
	FSimpleDelegate CloseMenuDelegate = FSimpleDelegate::CreateLambda([this]()
	{
		if (ContextMenuAnchor.IsValid())
		{
			ContextMenuAnchor->SetIsOpen(false);
		}
	});

	return GenerateMenuContent(TargetTagHandle, CloseMenuDelegate);
}

TSharedRef<SWidget> FTCATLayerConfigCustomization::GenerateMenuContent(TSharedPtr<IPropertyHandle> ChildHandle, FSimpleDelegate OnCloseMenu)
{
	TSharedPtr<SEditableTextBox> NewTagTextBox;
	
	FText TitleText = bIsCompositeTarget ? LOCTEXT("CreateCompTitle", "Create Composite Tag") : LOCTEXT("CreateBaseTitle", "Create Base Tag");
	FText HintText = bIsCompositeTarget ? LOCTEXT("CompTagHint", "Composite Tag Name...") : LOCTEXT("BaseTagHint", "Base Tag Name...");

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(10.0f)
		[
			SNew(SVerticalBox)
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(TitleText)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(NewTagTextBox, SEditableTextBox)
					.MinDesiredWidth(180.0f)
					.HintText(HintText)
					.OnTextCommitted_Lambda([this, ChildHandle, OnCloseMenu](const FText& Text, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnEnter)
						{
							FString NewTagName = Text.ToString();
							if (!NewTagName.IsEmpty())
							{
								RegisterNewTag(NewTagName);
								if (ChildHandle.IsValid()) ChildHandle->SetValue(FName(*NewTagName));
								OnCloseMenu.ExecuteIfBound();
							}
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddBtn", "Add"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.OnClicked_Lambda([this, ChildHandle, OnCloseMenu, NewTagTextBox]()
					{
						if (NewTagTextBox.IsValid())
						{
							FString NewTagName = NewTagTextBox->GetText().ToString();
							if (!NewTagName.IsEmpty())
							{
								RegisterNewTag(NewTagName);
								if (ChildHandle.IsValid()) ChildHandle->SetValue(FName(*NewTagName));
								OnCloseMenu.ExecuteIfBound();
							}
						}
						return FReply::Handled();
					})
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ListTitle", "Existing Tags"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(200.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					GenerateTagList(OnCloseMenu)
				]
			]
		];
}

TSharedRef<SWidget> FTCATLayerConfigCustomization::GenerateTagList(FSimpleDelegate OnCloseMenu)
{
	const UTCATSettings* Settings = GetDefault<UTCATSettings>();
	TArray<FName> FilteredTags;

	if (bIsCompositeTarget)
	{
		for (const FName& Tag : Settings->CompositeInfluenceTags) FilteredTags.Add(Tag);
	}
	else
	{
		for (const FName& Tag : Settings->BaseInfluenceTags) FilteredTags.Add(Tag);
	}

	FilteredTags.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);

	if (FilteredTags.Num() == 0)
	{
		ListBox->AddSlot()
			.AutoHeight()
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoTags", "No tags found."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		return ListBox;
	}

	for (const FName& TagName : FilteredTags)
	{
		FString TagStr = TagName.ToString();

		ListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)
					
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TagStr))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
						.ContentPadding(FMargin(2.0f, 0.0f))
						.ToolTipText(LOCTEXT("DeleteTooltip", "Delete this tag globally"))
						.OnClicked_Lambda([this, TagStr, OnCloseMenu]()
						{
							DeleteTag(TagStr);
							OnCloseMenu.ExecuteIfBound();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("×")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FLinearColor(0.8f, 0.2f, 0.2f))
						]
					]
				]
			];
	}

	return ListBox;
}

void FTCATLayerConfigCustomization::RegisterNewTag(const FString& TagName)
{
	UTCATSettings* Settings = GetMutableDefault<UTCATSettings>();
	FName NewTag = FName(*TagName);
	bool bChanged = false;

	if (bIsCompositeTarget)
	{
		if (!Settings->CompositeInfluenceTags.Contains(NewTag))
		{
			Settings->CompositeInfluenceTags.Add(NewTag);
			bChanged = true;
		}
	}
	else
	{
		if (!Settings->BaseInfluenceTags.Contains(NewTag))
		{
			Settings->BaseInfluenceTags.Add(NewTag);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Settings->TryUpdateDefaultConfigFile();
	}
}

void FTCATLayerConfigCustomization::DeleteTag(const FString& TagName)
{
	UTCATSettings* Settings = GetMutableDefault<UTCATSettings>();
	FName TagToRemove = FName(*TagName);
	bool bChanged = false;

	if (bIsCompositeTarget)
	{
		if (Settings->CompositeInfluenceTags.Contains(TagToRemove))
		{
			Settings->CompositeInfluenceTags.Remove(TagToRemove);
			bChanged = true;
		}
	}
	else
	{
		if (Settings->BaseInfluenceTags.Contains(TagToRemove))
		{
			Settings->BaseInfluenceTags.Remove(TagToRemove);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Settings->TryUpdateDefaultConfigFile();
	}
}

bool FTCATLayerConfigCustomization::OnShouldFilterCurveAsset(const FAssetData& AssetData)
{
	// OnShouldFilterAsset: Return true to HIDE the asset, false to SHOW it.
	
	FString Path = AssetData.PackagePath.ToString();
	// If path DOES NOT start with Target Path, Filter it (Hide it).
	return !Path.StartsWith(TEXT("/TCAT/TCAT/Curves"));
}

#undef LOCTEXT_NAMESPACE
