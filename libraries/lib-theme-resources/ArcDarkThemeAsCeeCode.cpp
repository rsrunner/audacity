/*!********************************************************************
 
 Audacity: A Digital Audio Editor
 
 @file ArcDarkThemeAsCeeCode.cpp
 
 rsrunner split from Theme.cpp
 
 **********************************************************************/

#include <vector>
#include "Theme.h"

static const std::vector<unsigned char> ImageCacheAsData {
// Include the generated file full of numbers
#include "ArcDarkThemeAsCeeCode.h"
};

static ThemeBase::RegisteredTheme theme{
   { "arc-dark", XO("Arc-Dark") }, PreferredSystemAppearance::Dark, ImageCacheAsData
};
