//-----------------------------------------------------------------------------
// Copyright (c) 2013 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#ifndef SHAPE_ANIMATION_ASSET_H
#include "ShapeAnimationAsset.h"
#endif

#ifndef _ASSET_MANAGER_H_
#include "assets/assetManager.h"
#endif

#ifndef _CONSOLETYPES_H_
#include "console/consoleTypes.h"
#endif

#ifndef _TAML_
#include "persistence/taml/taml.h"
#endif

#ifndef _ASSET_PTR_H_
#include "assets/assetPtr.h"
#endif

#include "core/resourceManager.h"

// Debug Profiling.
#include "platform/profiler.h"

#include "console/typeValidators.h"
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(ShapeAnimationAsset);

ConsoleType(ShapeAnimationAssetPtr, TypeShapeAnimationAssetPtr, ShapeAnimationAsset, ASSET_ID_FIELD_PREFIX)

//-----------------------------------------------------------------------------

ConsoleGetType(TypeShapeAnimationAssetPtr)
{
   // Fetch asset Id.
   return (*((AssetPtr<ShapeAnimationAsset>*)dptr)).getAssetId();
}

//-----------------------------------------------------------------------------

ConsoleSetType(TypeShapeAnimationAssetPtr)
{
   // Was a single argument specified?
   if (argc == 1)
   {
      // Yes, so fetch field value.
      const char* pFieldValue = argv[0];

      // Fetch asset pointer.
      AssetPtr<ShapeAnimationAsset>* pAssetPtr = dynamic_cast<AssetPtr<ShapeAnimationAsset>*>((AssetPtrBase*)(dptr));

      // Is the asset pointer the correct type?
      if (pAssetPtr == NULL)
      {
         // No, so fail.
         //Con::warnf("(TypeShapeAnimationAssetPtr) - Failed to set asset Id '%d'.", pFieldValue);
         return;
      }

      // Set asset.
      pAssetPtr->setAssetId(pFieldValue);

      return;
   }

   // Warn.
   Con::warnf("(TypeShapeAnimationAssetPtr) - Cannot set multiple args to a single asset.");
}

const String ShapeAnimationAsset::mErrCodeStrings[] =
{
   "TooManyBones",
   "UnKnown"
};

//-----------------------------------------------------------------------------

ShapeAnimationAsset::ShapeAnimationAsset() : 
   mIsEmbedded(false), mIsCyclical(true), mIsBlend(false), mBlendFrame(0), mStartFrame(0), mEndFrame(-1), mPadRotation(true), mPadTransforms(false)
{
   mFileName = StringTable->EmptyString();
   mFilePath = StringTable->EmptyString();
   mAnimationName = StringTable->EmptyString();

   mBlendAnimAssetName = StringTable->EmptyString();
}

//-----------------------------------------------------------------------------

ShapeAnimationAsset::~ShapeAnimationAsset()
{
}

//-----------------------------------------------------------------------------

void ShapeAnimationAsset::initPersistFields()
{
   docsURL;
   // Call parent.
   Parent::initPersistFields();

   addProtectedField("animationFile", TypeAssetLooseFilePath, Offset(mFileName, ShapeAnimationAsset),
      &setAnimationFile, &getAnimationFile, "Path to the file name containing the animation");

   addField("animationName", TypeString, Offset(mAnimationName, ShapeAnimationAsset), "Name of the animation");

   addField("isEmbedded", TypeBool, Offset(mIsEmbedded, ShapeAnimationAsset), "If true, this animation asset just referrs to an embedded animation of a regular shape mesh. If false, it is a self-contained animation file");

   addField("isCyclic", TypeBool, Offset(mIsCyclical, ShapeAnimationAsset), "Is this animation looping?");

   addField("isBlend", TypeBool, Offset(mIsBlend, ShapeAnimationAsset), "Is this animation blended with another?");
   addField("blendRefAnimation", TypeString, Offset(mBlendAnimAssetName, ShapeAnimationAsset), "AssetID of the animation to reference for our blending");
   addFieldV("blendFrame", TypeRangedS32, Offset(mBlendFrame, ShapeAnimationAsset), &CommonValidators::PositiveInt, "Which frame of the reference animation do we use for our blending");

   addFieldV("startFrame", TypeRangedS32, Offset(mStartFrame, ShapeAnimationAsset), &CommonValidators::PositiveInt, "What frame does this animation clip start on");
   addFieldV("endFrame", TypeRangedS32, Offset(mEndFrame, ShapeAnimationAsset), &CommonValidators::PositiveInt, "What fram does this animation clip end on");
   addField("padRotation", TypeBool, Offset(mPadRotation, ShapeAnimationAsset), "Are the rotation values padded");
   addField("padTransforms", TypeBool, Offset(mPadTransforms, ShapeAnimationAsset), "Are the transform values padded");
}

//------------------------------------------------------------------------------

void ShapeAnimationAsset::copyTo(SimObject* object)
{
   // Call to parent.
   Parent::copyTo(object);
}

void ShapeAnimationAsset::initializeAsset(void)
{
   if (!mIsEmbedded)
   {
      //If we're not embedded, we need to load in our initial shape and do some prepwork
      mFilePath = getOwned() ? expandAssetFilePath(mFileName) : mFilePath;

      mSourceShape = ResourceManager::get().load(mFilePath);

      if (!mSourceShape || !mSourceShape->addSequence("ambient", "", mAnimationName, mStartFrame, mEndFrame, mPadRotation, mPadTransforms))
      {
         Con::errorf("ShapeAnimationAsset::initializeAsset - Unable to do initial setup of the animation clip named %s for asset %s", mAnimationName, getAssetName());
         return;
      }

      S32 sequenceId = mSourceShape->findSequence(mAnimationName);

      if(mIsCyclical)
         mSourceShape->sequences[sequenceId].flags |= TSShape::Cyclic;
      else
         mSourceShape->sequences[sequenceId].flags &= (~(TSShape::Cyclic));
   }
}

void ShapeAnimationAsset::onAssetRefresh(void)
{

}

void ShapeAnimationAsset::setAnimationFile(const char* pAnimationFile)
{
   // Sanity!
   AssertFatal(pAnimationFile != NULL, "Cannot use a NULL animation file.");

   // Fetch image file.
   pAnimationFile = StringTable->insert(pAnimationFile, true);

   // Ignore no change,
   if (pAnimationFile == mFileName)
      return;

   // Update.
   mFileName = getOwned() ? expandAssetFilePath(pAnimationFile) : pAnimationFile;

   // Refresh the asset.
   refreshAsset();
}

S32 ShapeAnimationAsset::getAnimationCount()
{ 
   if (mSourceShape == nullptr)
      return 0;

   return mSourceShape->sequences.size(); 
}

DefineEngineMethod(ShapeAnimationAsset, getAnimationCount, S32, (), ,
   "Gets the number of animations for this shape asset.\n"
   "@return Animation count.\n")
{
   return object->getAnimationCount();
}

DefineEngineMethod(ShapeAnimationAsset, getAnimationPath, const char*, (), ,
   "Gets the Animation file path associated to this asset.")
{
   return object->getAnimationPath();
}
