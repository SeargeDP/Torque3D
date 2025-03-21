//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
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

#include "platform/platform.h"
#include "forest/editor/forestBrushElement.h"

#include "console/engineAPI.h"
#include "forest/forestItem.h"


//-------------------------------------------------------------------------
// ForestBrushElement
//-------------------------------------------------------------------------

IMPLEMENT_CONOBJECT( ForestBrushElement );

ConsoleDocClass( ForestBrushElement,
   "@brief Represents a type of ForestItem and parameters for how it is placed"
   " when painting with a ForestBrush that contains it.\n\n"
   "@ingroup Forest"
);

ForestBrushElement::ForestBrushElement()
: mData( NULL ),
  mProbability( 1 ),
  mRotationRange( 360 ),
  mScaleMin( 1 ),
  mScaleMax( 1 ),
  mScaleExponent( 1 ),
  mSinkMin( 0.0f ),
  mSinkMax( 0.0f ),
  mSinkRadius( 1 ),
  mSlopeMin( 0.0f ),
  mSlopeMax( 90.0f ),
  mElevationMin( -10000.0f ),
  mElevationMax( 10000.0f ) 
{
}


void ForestBrushElement::initPersistFields()
{
   docsURL;
   Parent::initPersistFields();

   addGroup( "ForestBrushElement" );

      addField( "forestItemData", TYPEID< ForestItemData >(), Offset( mData, ForestBrushElement ), 
         "The type of ForestItem this element holds placement parameters for." );

      addFieldV( "probability", TypeRangedF32, Offset( mProbability, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "The probability that this element will be created during an editor brush stroke "
         "is the sum of all element probabilities in the brush divided by the probability "
         "of this element." );

      addFieldV( "rotationRange", TypeRangedF32, Offset( mRotationRange, ForestBrushElement ), &CommonValidators::PosDegreeRange,
         "The max rotation in degrees that items will be placed." );

      addFieldV( "scaleMin", TypeRangedF32, Offset( mScaleMin, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "The minimum random size for each item." );

      addFieldV( "scaleMax", TypeRangedF32, Offset( mScaleMax, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "The maximum random size of each item." );

      addFieldV( "scaleExponent", TypeRangedF32, Offset( mScaleExponent, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "An exponent used to bias between the minimum and maximum random sizes." );

      addFieldV( "sinkMin", TypeRangedF32, Offset( mSinkMin, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "Min variation in the sink radius." );

      addFieldV( "sinkMax", TypeRangedF32, Offset( mSinkMax, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "Max variation in the sink radius." );

      addFieldV( "sinkRadius", TypeRangedF32, Offset( mSinkRadius, ForestBrushElement ), &CommonValidators::PositiveFloat,
         "This is the radius used to calculate how much to sink the trunk at "
         "its base and is used to sink the tree into the ground when its on a slope." );

      addFieldV( "slopeMin", TypeRangedF32, Offset( mSlopeMin, ForestBrushElement ), &CommonValidators::PosDegreeRangeQuarter,
         "The min surface slope in degrees this item will be placed on." );

      addFieldV( "slopeMax", TypeRangedF32, Offset( mSlopeMax, ForestBrushElement ), &CommonValidators::PosDegreeRangeQuarter,
         "The max surface slope in degrees this item will be placed on." );

      addFieldV( "elevationMin", TypeRangedF32, Offset( mElevationMin, ForestBrushElement ), &CommonValidators::F32Range,
         "The min world space elevation this item will be placed." );

      addFieldV( "elevationMax", TypeRangedF32, Offset( mElevationMax, ForestBrushElement ), &CommonValidators::F32Range,
         "The max world space elevation this item will be placed." );

   endGroup( "ForestBrushElement" );      
}


//-------------------------------------------------------------------------
// ForestBrushElementSet
//-------------------------------------------------------------------------

SimObjectPtr<SimGroup> ForestBrush::smGroup = NULL;

IMPLEMENT_CONOBJECT( ForestBrush );

ConsoleDocClass( ForestBrush,
   "@brief Container class for ForestBrushElements\n\n"
   "Editor use only.\n\n"
   "@internal"
);

ForestBrush::ForestBrush()
{

}

bool ForestBrush::onAdd()
{
   if ( !Parent::onAdd() )
      return false;

   getGroup()->addObject( this );

   return true;
}

void ForestBrush::addObject( SimObject *inObj )
{
   ForestBrushElement *ele = dynamic_cast<ForestBrushElement*>( inObj );
   if ( !ele )
      return;

   //if ( containsItemData( ele->mData ) )
   //   return;

   Parent::addObject( inObj );
}

SimGroup* ForestBrush::getGroup()
{   
   if ( !smGroup )
   {
      SimGroup *dummy;
      if ( Sim::findObject( "ForestBrushGroup", dummy ) )
      {
         smGroup = dummy;
         return smGroup;
      }

      smGroup = new SimGroup;
      smGroup->assignName( "ForestBrushGroup" );
      smGroup->registerObject();
      Sim::getRootGroup()->addObject( smGroup );
   }

   return smGroup;
}

bool ForestBrush::containsItemData( const ForestItemData *inData )
{
   SimObjectList::iterator iter = mObjectList.begin();
   for ( ; iter != mObjectList.end(); iter++ )
   {
      ForestBrushElement *pElement = dynamic_cast<ForestBrushElement*>(*iter);

      if ( !pElement )
         continue;

      if ( pElement->mData == inData )
         return true;
   }   

   return false;
}

DefineEngineMethod( ForestBrush, containsItemData, bool, ( const char * obj ), , "( ForestItemData obj )" )
{
   ForestItemData *data = NULL;
   if ( !Sim::findObject( obj, data ) )
   {
      Con::warnf( "ForestBrush::containsItemData - invalid object passed" );
      return false;
   }

   return object->containsItemData( data );
}

//-------------------------------------------------------------------------
// ForestBrushGroupSet
//-------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(ForestBrushGroup);

ConsoleDocClass(ForestBrushGroup,
   "@brief Container class for ForestBrushes\n\n"
   "Editor use only.\n\n"
   "@internal"
);

ForestBrushGroup::ForestBrushGroup()
{

}

bool ForestBrushGroup::onAdd()
{
   if (!Parent::onAdd())
      return false;

   SimSet* forestBrushSet;
   if (!Sim::findObject("ForestBrushSet", forestBrushSet))
   {
      Con::errorf("ForestBrushGroup::onAdd() - failed to find ForestBrushSet to add new ForestBrushGroup to!");
   }

   forestBrushSet->addObject(this);

   return true;
}

void ForestBrushGroup::addObject(SimObject* inObj)
{
   ForestBrush* ele = dynamic_cast<ForestBrush*>(inObj);
   if (!ele)
      return;

   //if ( containsItemData( ele->mData ) )
   //   return;

   Parent::addObject(inObj);
}

bool ForestBrushGroup::containsBrushData(const ForestBrush* inData)
{
   SimObjectList::iterator iter = mObjectList.begin();
   for (; iter != mObjectList.end(); iter++)
   {
      ForestBrush* pElement = dynamic_cast<ForestBrush*>(*iter);

      if (!pElement)
         continue;

      if (pElement == inData)
         return true;
   }

   return false;
}

DefineEngineMethod(ForestBrushGroup, containsBrushData, bool, (const char* obj), , "( ForestBrush obj )")
{
   ForestBrush* data = NULL;
   if (!Sim::findObject(obj, data))
   {
      Con::warnf("ForestBrush::containsBrushData - invalid object passed");
      return false;
   }

   return object->containsBrushData(data);
}
