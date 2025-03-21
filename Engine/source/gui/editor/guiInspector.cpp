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

#include "console/engineAPI.h"
#include "gui/editor/guiInspector.h"
#include "gui/editor/inspector/field.h"
#include "gui/editor/inspector/group.h"
#include "gui/buttons/guiIconButtonCtrl.h"
#include "gui/editor/inspector/dynamicGroup.h"
#include "gui/containers/guiScrollCtrl.h"
#include "gui/editor/inspector/customField.h"
#include "console/typeValidators.h"

IMPLEMENT_CONOBJECT(GuiInspector);

ConsoleDocClass( GuiInspector,
   "@brief A control that allows to edit the properties of one or more SimObjects.\n\n"
   "Editor use only.\n\n"
   "@internal"
);


IMPLEMENT_CALLBACK(GuiInspector, onPreInspectObject, void, (SimObject* object), (object),
   "Called prior to inspecting a new object.\n");

IMPLEMENT_CALLBACK(GuiInspector, onPostInspectObject, void, (SimObject* object), (object),
   "Called after inspecting a new object.\n");

//#define DEBUG_SPEW


//-----------------------------------------------------------------------------

GuiInspector::GuiInspector()
 : mDividerPos( 0.35f ),
   mDividerMargin( 5 ),
   mOverDivider( false ),
   mMovingDivider( false ),
   mHLField( NULL ),
   mShowCustomFields( true ),
   mComponentGroupTargetId(-1),
   mForcedArrayIndex(-1)
{
   mPadding = 1;
   mSearchText = StringTable->EmptyString();
}

//-----------------------------------------------------------------------------

GuiInspector::~GuiInspector()
{
   clearGroups();
}

//-----------------------------------------------------------------------------

void GuiInspector::initPersistFields()
{
   docsURL;
   addGroup( "Inspector" );
   
      addFieldV( "dividerMargin", TypeRangedS32, Offset( mDividerMargin, GuiInspector ), &CommonValidators::PositiveInt);

      addField( "groupFilters", TypeRealString, Offset( mGroupFilters, GuiInspector ), 
         "Specify groups that should be shown or not. Specifying 'shown' implicitly does 'not show' all other groups. Example string: +name -otherName" );

      addField( "showCustomFields", TypeBool, Offset( mShowCustomFields, GuiInspector ),
         "If false the custom fields Name, Id, and Source Class will not be shown." );

      addFieldV("forcedArrayIndex", TypeRangedS32, Offset(mForcedArrayIndex, GuiInspector), &CommonValidators::NegDefaultInt);

      addField("searchText", TypeString, Offset(mSearchText, GuiInspector), "A string that, if not blank, is used to filter shown fields");
   endGroup( "Inspector" );

   Parent::initPersistFields();
}

//-----------------------------------------------------------------------------

void GuiInspector::onRemove()
{
   clearGroups();
   Parent::onRemove();
}

//-----------------------------------------------------------------------------

void GuiInspector::onDeleteNotify( SimObject *object )
{
   Parent::onDeleteNotify( object );
   
   if( isInspectingObject( object ) )
      removeInspectObject( object );
}

//-----------------------------------------------------------------------------

void GuiInspector::parentResized(const RectI &oldParentRect, const RectI &newParentRect)
{
   GuiControl *parent = getParent();
   if ( parent && dynamic_cast<GuiScrollCtrl*>(parent) != NULL )
   {
      GuiScrollCtrl *scroll = dynamic_cast<GuiScrollCtrl*>(parent);
      setWidth( ( newParentRect.extent.x - ( scroll->scrollBarThickness() + 4  ) ) );
   }
   else
      Parent::parentResized(oldParentRect,newParentRect);
}

//-----------------------------------------------------------------------------

bool GuiInspector::resize( const Point2I &newPosition, const Point2I &newExtent )
{
   //F32 dividerPerc = (F32)getWidth() / (F32)mDividerPos;

   bool result = Parent::resize( newPosition, newExtent );

   //mDividerPos = (F32)getWidth() * dividerPerc;

   updateDivider();

   return result;
}

//-----------------------------------------------------------------------------

GuiControl* GuiInspector::findHitControl( const Point2I &pt, S32 initialLayer )
{
   if ( mOverDivider || mMovingDivider )
      return this;

   return Parent::findHitControl( pt, initialLayer );
}

//-----------------------------------------------------------------------------

void GuiInspector::getCursor( GuiCursor *&cursor, bool &showCursor, const GuiEvent &lastGuiEvent )
{
   GuiCanvas *pRoot = getRoot();
   if( !pRoot )
      return;

   S32 desiredCursor = mOverDivider ? PlatformCursorController::curResizeVert : PlatformCursorController::curArrow;

   // Bail if we're already at the desired cursor
   if ( pRoot->mCursorChanged == desiredCursor )
      return;

   PlatformWindow *pWindow = static_cast<GuiCanvas*>(getRoot())->getPlatformWindow();
   AssertFatal(pWindow != NULL,"GuiControl without owning platform window!  This should not be possible.");
   PlatformCursorController *pController = pWindow->getCursorController();
   AssertFatal(pController != NULL,"PlatformWindow without an owned CursorController!");

   // Now change the cursor shape
   pController->popCursor();
   pController->pushCursor(desiredCursor);
   pRoot->mCursorChanged = desiredCursor;
}

//-----------------------------------------------------------------------------

void GuiInspector::onMouseMove(const GuiEvent &event)
{
   if ( collideDivider( globalToLocalCoord( event.mousePoint ) ) )
      mOverDivider = true;
   else
      mOverDivider = false;
}

//-----------------------------------------------------------------------------

void GuiInspector::onMouseDown(const GuiEvent &event)
{
   if ( mOverDivider )
   {
      mMovingDivider = true;
   }
}

//-----------------------------------------------------------------------------

void GuiInspector::onMouseUp(const GuiEvent &event)
{
   mMovingDivider = false;   
}

//-----------------------------------------------------------------------------

void GuiInspector::onMouseDragged(const GuiEvent &event)
{
   if ( !mMovingDivider )   
      return;

   Point2I localPnt = globalToLocalCoord( event.mousePoint );

   S32 inspectorWidth = getWidth();

   // Distance from mouse/divider position in local space
   // to the right edge of the inspector
   mDividerPos = inspectorWidth - localPnt.x;
   mDividerPos = mClamp( mDividerPos, 0, inspectorWidth );

   // Divide that by the inspectorWidth to get a percentage
   mDividerPos /= inspectorWidth;

   updateDivider();
}

//-----------------------------------------------------------------------------

GuiInspectorGroup* GuiInspector::findExistentGroup( StringTableEntry groupName )
{
   // If we have no groups, it couldn't possibly exist
   if( mGroups.empty() )
      return NULL;

   // Attempt to find it in the group list
   Vector<GuiInspectorGroup*>::iterator i = mGroups.begin();

   for( ; i != mGroups.end(); i++ )
   {
      if( dStricmp( (*i)->getGroupName(), groupName ) == 0 )
         return *i;
   }

   return NULL;
}

S32 GuiInspector::findExistentGroupIndex(StringTableEntry groupName)
{
   // If we have no groups, it couldn't possibly exist
   if (mGroups.empty())
      return -1;

   // Attempt to find it in the group list
   Vector<GuiInspectorGroup*>::iterator i = mGroups.begin();

   S32 index = 0;
   for (; i != mGroups.end(); i++)
   {
      if (dStricmp((*i)->getGroupName(), groupName) == 0)
         return index;

      index++;
   }

   return -1;
}

//-----------------------------------------------------------------------------

void GuiInspector::updateFieldValue( StringTableEntry fieldName, StringTableEntry arrayIdx )
{
   // We don't know which group contains the field of this name,
   // so ask each group in turn, and break when a group returns true
   // signifying it contained and updated that field.

   Vector<GuiInspectorGroup*>::iterator groupIter = mGroups.begin();

   for( ; groupIter != mGroups.end(); groupIter++ )
   {   
      if ( (*groupIter)->updateFieldValue( fieldName, arrayIdx ) )
         break;
   }
}

//-----------------------------------------------------------------------------

void GuiInspector::clearGroups()
{
   #ifdef DEBUG_SPEW
   Platform::outputDebugString( "[GuiInspector] Clearing %i (%s)", getId(), getName() );
   #endif
   
   // If we have no groups, there's nothing to clear!
   if( mGroups.empty() )
      return;

   mHLField = NULL;
   
   if( isMethod( "onClear" ) )
      Con::executef( this, "onClear" );

   Vector<GuiInspectorGroup*>::iterator i = mGroups.begin();

   freeze(true);

   // Delete Groups
   for( ; i != mGroups.end(); i++ )
   {
      if((*i) && (*i)->isProperlyAdded())
         (*i)->deleteObject();
   }   

   mGroups.clear();

   freeze(false);
   updatePanes();
}

//-----------------------------------------------------------------------------

bool GuiInspector::isInspectingObject( SimObject* object )
{
   const U32 numTargets = mTargets.size();
   for( U32 i = 0; i < numTargets; ++ i )
      if( mTargets[ i ] == object )
         return true;
         
   return false;
}

//-----------------------------------------------------------------------------

void GuiInspector::inspectObject( SimObject *object )
{
   onPreInspectObject_callback((mTargets.size() > 1)? mTargets[0] : NULL);

   if( mTargets.size() > 1 || !isInspectingObject( object ) )
      clearInspectObjects();
         
   addInspectObject( object );
   onPostInspectObject_callback(object);
}

//-----------------------------------------------------------------------------

void GuiInspector::clearInspectObjects()
{
   const U32 numTargets = mTargets.size();
   for( U32 i = 0; i < numTargets; ++ i )
      clearNotify( mTargets[ i ] );
      
   clearGroups();
   mTargets.clear();
}

//-----------------------------------------------------------------------------

void GuiInspector::addInspectObject( SimObject* object, bool autoSync )
{   
   // If we are already inspecting the object, just update the groups.

   onPreInspectObject_callback((mTargets.size() > 1) ? mTargets[0] : NULL);
   if( isInspectingObject( object ) )
   {
      #ifdef DEBUG_SPEW
      Platform::outputDebugString( "[GuiInspector] Refreshing view of %i:%s (%s)",
         object->getId(), object->getClassName(), object->getName() );
      #endif

      Vector<GuiInspectorGroup*>::iterator i = mGroups.begin();
      for ( ; i != mGroups.end(); i++ )
         (*i)->updateAllFields();

      return;
   }

   #ifdef DEBUG_SPEW
   Platform::outputDebugString( "[GuiInspector] Adding %i:%s (%s) to inspect set",
      object->getId(), object->getClassName(), object->getName() );
   #endif

   // Give users a chance to customize fields on this object
   if( object->isMethod("onDefineFieldTypes") )
      Con::executef( object, "onDefineFieldTypes" );

   // Set Target
   mTargets.push_back( object );
   deleteNotify( object );
   
	if( autoSync )
		refresh();
   onPostInspectObject_callback(object);
}

//-----------------------------------------------------------------------------

void GuiInspector::removeInspectObject( SimObject* object )
{
   const U32 numTargets = mTargets.size();
   for( U32 i = 0; i < numTargets; ++ i )
      if( mTargets[ i ] == object )
      {      
         // Delete all inspector data *before* removing the target so that apply calls
         // triggered by edit controls losing focus will not find the inspect object
         // gone.

         clearGroups();
         
         #ifdef DEBUG_SPEW
         Platform::outputDebugString( "[GuiInspector] Removing %i:%s (%s) from inspect set",
            object->getId(), object->getClassName(), object->getName() );
         #endif

         mTargets.erase( i );
         clearNotify( object );
         
         // Refresh the inspector except if the system is going down.

         if( !Sim::isShuttingDown() )
            refresh();
         
         return;
      }   
}

//-----------------------------------------------------------------------------

void GuiInspector::setName( StringTableEntry newName )
{
   if( mTargets.size() != 1 )
      return;

   StringTableEntry name = StringTable->insert(newName);

   // Only assign a new name if we provide one
   mTargets[ 0 ]->assignName(name);
}

//-----------------------------------------------------------------------------

bool GuiInspector::collideDivider( const Point2I &localPnt )
{   
   RectI divisorRect( getWidth() - getWidth() * mDividerPos - mDividerMargin, 0, mDividerMargin * 2, getHeight() ); 

   if ( divisorRect.pointInRect( localPnt ) )
      return true;

   return false;
}

//-----------------------------------------------------------------------------

void GuiInspector::updateDivider()
{
   for ( U32 i = 0; i < mGroups.size(); i++ )
      for ( U32 j = 0; j < mGroups[i]->mChildren.size(); j++ )
         mGroups[i]->mChildren[j]->updateRects(); 

   //setUpdate();
}

//-----------------------------------------------------------------------------

void GuiInspector::getDivider( S32 &pos, S32 &margin )
{   
   pos = (F32)getWidth() * mDividerPos;
   margin = mDividerMargin;   
}

//-----------------------------------------------------------------------------

void GuiInspector::setHighlightField( GuiInspectorField *field )
{
   if ( mHLField == field )
      return;

   if ( mHLField.isValid() )
      mHLField->setHLEnabled( false );
   mHLField = field;

   // We could have been passed a null field, meaning, set no field highlighted.
   if ( mHLField.isNull() )
      return;

   mHLField->setHLEnabled( true );
}

//-----------------------------------------------------------------------------

bool GuiInspector::isGroupFiltered( const char *groupName ) const
{
   // Internal and Ungrouped always filtered, we never show them.   
   if ( dStricmp( groupName, "Internal" ) == 0 ||
        dStricmp( groupName, "Ungrouped" ) == 0 ||
		  dStricmp( groupName, "AdvCoordManipulation" ) == 0)
      return true;

   // Normal case, determine if filtered by looking at the mGroupFilters string.
   String searchStr;

   // Is this group explicitly show? Does it immediately follow a + char.
   searchStr = String::ToString( "+%s", groupName );
   if ( mGroupFilters.find( searchStr ) != String::NPos )
      return false;   

   // Were there any other + characters, if so, we are implicitly hidden.   
   if ( mGroupFilters.find( "+" ) != String::NPos )
      return true;

   // Is this group explicitly hidden? Does it immediately follow a - char.
   searchStr = String::ToString( "-%s", groupName );
   if ( mGroupFilters.find( searchStr ) != String::NPos )
      return true;   

   return false;
}

//-----------------------------------------------------------------------------

bool GuiInspector::isGroupExplicitlyFiltered( const char *groupName ) const
{  
   String searchStr;

   searchStr = String::ToString( "-%s", groupName );

   if ( mGroupFilters.find( searchStr ) != String::NPos )
      return true;   

   return false;
}

//-----------------------------------------------------------------------------

void GuiInspector::setObjectField( const char *fieldName, const char *data )
{
   GuiInspectorField *field;

   for ( S32 i = 0; i < mGroups.size(); i++ )
   {
      field = mGroups[i]->findField( fieldName );

      if( field )
      {
         field->setData( data );
         return;
      }
   }
}

//-----------------------------------------------------------------------------

static SimObject *sgKeyObj = NULL;

bool findInspectors( GuiInspector *obj )
{   
   if ( obj->isAwake() && obj->isInspectingObject( sgKeyObj ) )
      return true;

   return false;
}

//-----------------------------------------------------------------------------

GuiInspector* GuiInspector::findByObject( SimObject *obj )
{
   sgKeyObj = obj;

   Vector< GuiInspector* > found;
   Sim::getGuiGroup()->findObjectByCallback( findInspectors, found );

   if ( found.empty() )
      return NULL;

   return found.first();
}

//-----------------------------------------------------------------------------

void GuiInspector::refresh()
{
   clearGroups();
   
   // Remove any inspect object that happened to have
   // already been killed.
   
   for( U32 i = 0; i < mTargets.size(); ++ i )
      if( !mTargets[ i ] )
      {
         mTargets.erase( i );
         -- i;
      }
      
   if( !mTargets.size() )
      return;
   
   // Special group for fields which should appear at the top of the
   // list outside of a rollout control.
   
   GuiInspectorGroup *ungroup = NULL;
   if( mTargets.size() == 1 )
   {
      ungroup = new GuiInspectorGroup( "Ungrouped", this );
      ungroup->setHeaderHidden( true );
      ungroup->setCanCollapse( false );

      ungroup->registerObject();
      mGroups.push_back( ungroup );
      addObject( ungroup );
   }

   // Put the 'transform' group first
   GuiInspectorGroup *transform = new GuiInspectorGroup( "Transform", this );

   transform->registerObject();
   mGroups.push_back(transform);
   addObject(transform);

   // Always create the 'general' group (for fields without a group)      
   GuiInspectorGroup *general = new GuiInspectorGroup( "General", this );

   general->registerObject();
   mGroups.push_back(general);
   addObject(general);

   // Create the inspector groups for static fields.

   for( TargetVector::iterator iter = mTargets.begin(); iter != mTargets.end(); ++ iter )
   {
      AbstractClassRep::FieldList &fieldList = ( *iter )->getModifiableFieldList();

      // Iterate through, identifying the groups and create necessary GuiInspectorGroups
      for( AbstractClassRep::FieldList::iterator itr = fieldList.begin(); itr != fieldList.end(); itr++ )
      {      
         if ( itr->type == AbstractClassRep::StartGroupFieldType )
         {
            GuiInspectorGroup* group = findExistentGroup( itr->pGroupname );
            
            if( !group && !isGroupFiltered( itr->pGroupname ) )
            {
               GuiInspectorGroup *newGroup = new GuiInspectorGroup( itr->pGroupname, this );
               newGroup->setForcedArrayIndex(mForcedArrayIndex);

			      newGroup->registerObject();
               if( !newGroup->getNumFields() )
               {
                  #ifdef DEBUG_SPEW
                  Platform::outputDebugString( "[GuiInspector] Removing empty group '%s'",
					  newGroup->getCaption().c_str() );
                  #endif
                     
                  // The group ended up having no fields.  Remove it.
				      newGroup->deleteObject();
               }
               else
               {
                  mGroups.push_back(newGroup);
                  addObject(newGroup);
               }
            }
            else if(group)
            {
               group->setForcedArrayIndex(mForcedArrayIndex);
            }
         }
      }
   }

   mTargets.first()->onInspect(this);

   // Deal with dynamic fields
   if ( !isGroupFiltered( "Dynamic Fields" ) )
   {
      GuiInspectorGroup *dynGroup = new GuiInspectorDynamicGroup( "Dynamic Fields", this);

      dynGroup->registerObject();
      mGroups.push_back( dynGroup );
      addObject( dynGroup );
   }

   if( mShowCustomFields && mTargets.size() == 1 )
   {
      SimObject* object = mTargets.first();
      
      // Add the SimObjectID field to the ungrouped group.
      
      GuiInspectorCustomField* field = new GuiInspectorCustomField();
      field->init( this, ungroup );

      if( field->registerObject() )
      {
         ungroup->mChildren.push_back( field );
         ungroup->mStack->addObject( field );
         
         static StringTableEntry sId = StringTable->insert( "id" );
         
         field->setCaption( sId );
         field->setData( object->getIdString() );
         field->setDoc( "SimObjectId of this object. [Read Only]" );
      }
      else
         delete field;

      // Add the Source Class field to the ungrouped group.
      
      field = new GuiInspectorCustomField();
      field->init( this, ungroup );

      if( field->registerObject() )
      {
         ungroup->mChildren.push_back( field );
         ungroup->mStack->addObject( field );
         
         StringTableEntry sSourceClass = StringTable->insert( "Source Class", true );
         field->setCaption( sSourceClass );
         field->setData( object->getClassName() );

         Namespace* ns = object->getClassRep()->getNameSpace();
         field->setToolTip( Con::getNamespaceList( ns ) );

         field->setDoc( "Native class of this object. [Read Only]" );
      }
      else
         delete field;
   }


   // If the general group is still empty at this point ( or filtered ), kill it.
   if ( isGroupFiltered( "General" ) || general->mStack->size() == 0 )
   {
      for(S32 i=0; i<mGroups.size(); i++)
      {
         if ( mGroups[i] == general )
         {
            mGroups.erase(i);
            general->deleteObject();
            updatePanes();

            break;
         }
      }
   }

   // If transform turns out to be empty or filtered, remove it
   if( isGroupFiltered( "Transform" ) || transform->mStack->size() == 0 )
   {
      for(S32 i=0; i<mGroups.size(); i++)
      {
         if ( mGroups[i] == transform )
         {
            mGroups.erase(i);
            transform->deleteObject();
            updatePanes();

            break;
         }
      }
   }

   // If ungrouped is empty or explicitly filtered, remove it.   
   if( ungroup && ( isGroupExplicitlyFiltered( "Ungrouped" ) || ungroup->getNumFields() == 0 ) )
   {
      for(S32 i=0; i<mGroups.size(); i++)
      {
         if ( mGroups[i] == ungroup )
         {
            mGroups.erase(i);
            ungroup->deleteObject();
            updatePanes();

            break;
         }
      }
   }
   
   // If the object cannot be renamed, deactivate the name field if we have it.
   
   if( ungroup && getNumInspectObjects() == 1 && !getInspectObject()->isNameChangeAllowed() )
   {
      GuiInspectorField* nameField = ungroup->findField( "name" );
      if( nameField )
         nameField->setActive( false );
   }
}

//-----------------------------------------------------------------------------

void GuiInspector::sendInspectPreApply()
{
   const U32 numObjects = getNumInspectObjects();
   for( U32 i = 0; i < numObjects; ++ i )
      getInspectObject( i )->inspectPreApply();
}

//-----------------------------------------------------------------------------

void GuiInspector::sendInspectPostApply()
{
   const U32 numObjects = getNumInspectObjects();
   for( U32 i = 0; i < numObjects; ++ i )
      getInspectObject( i )->inspectPostApply();
}

S32 GuiInspector::createInspectorGroup(StringTableEntry groupName, S32 index)
{
   GuiInspectorGroup* newGroup = nullptr;
   newGroup = findExistentGroup(groupName);
   if (newGroup)
      return newGroup->getId();  //if we already have a group under this name, just return it

   newGroup = new GuiInspectorGroup(groupName, this);
   newGroup->registerObject();

   if (index == -1)
   {
      //if index is -1, we just throw the group onto the stack at the end
      mGroups.push_back(newGroup);
   }
   else
   {
      //if we have an explicit index, insert it specifically
      mGroups.insert(index, newGroup);
   }

   addObject(newGroup);

   return newGroup->getId();
}

void GuiInspector::removeInspectorGroup(StringTableEntry groupName)
{
   GuiInspectorGroup* group = findExistentGroup(groupName);
   if (group == nullptr)
      return;

   mGroups.remove(group);
   removeObject(group);
}

void GuiInspector::setForcedArrayIndex(S32 arrayIndex)
{
   mForcedArrayIndex = arrayIndex;
   refresh();
}

void GuiInspector::setSearchText(StringTableEntry searchText)
{
   mSearchText = searchText;
   refresh();
}

//=============================================================================
//    Console Methods.
//=============================================================================
// MARK: ---- Console Methods ----

//-----------------------------------------------------------------------------
DefineEngineMethod( GuiInspector, inspect, void, (const char* simObject), (""),
   "Inspect the given object.\n"
   "@param simObject Object to inspect.")
{
   SimObject * target = Sim::findObject(simObject);
   if(!target)
   {
      if(dAtoi(simObject) > 0)
         Con::warnf("%s::inspect(): invalid object: %s", object->getClassName(), simObject);

      object->clearInspectObjects();
      return;
   }

   object->inspectObject(target);
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, addInspect, void, (const char* simObject, bool autoSync), (true),
   "Add the object to the list of objects being inspected.\n"
   "@param simObject Object to add to the inspection."
   "@param autoSync Auto sync the values when they change.")
{
   SimObject* obj;
   if( !Sim::findObject( simObject, obj ) )
   {
      Con::errorf( "%s::addInspect(): invalid object: %s", object->getClassName(), simObject );
      return;
   }

	object->addInspectObject( obj, autoSync );
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, removeInspect, void, (const char* simObject), ,
   "Remove the object from the list of objects being inspected.\n"
   "@param simObject Object to remove from the inspection.")
{
   SimObject* obj;
   if( !Sim::findObject( simObject, obj ) )
   {
      Con::errorf( "%s::removeInspect(): invalid object: %s", object->getClassName(), simObject );
      return;
   }

   object->removeInspectObject( obj );
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, refresh, void, (), ,
   "Re-inspect the currently selected object.\n")
{
   if ( object->getNumInspectObjects() == 0 )
      return;

   SimObject *target = object->getInspectObject();
   if ( target )
      object->inspectObject( target );
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, getInspectObject, const char*, (S32 index), (0),
   "Returns currently inspected object.\n"
   "@param index Index of object in inspection list you want to get."
   "@return object being inspected.")
{
      
   if( index < 0 || index >= object->getNumInspectObjects() )
   {
      Con::errorf( "GuiInspector::getInspectObject() - index out of range: %i", index );
      return "";
   }
   
   return object->getInspectObject( index )->getIdString();
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, getNumInspectObjects, S32, (), ,
   "Return the number of objects currently being inspected.\n"
   "@return number of objects currently being inspected.")
{
   return object->getNumInspectObjects();
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, setName, void, (const char* newObjectName), ,
	"Rename the object being inspected (first object in inspect list).\n"
	"@param newObjectName new name for object being inspected.")
{
   object->setName(newObjectName);
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, apply, void, (), ,
	"Force application of inspected object's attributes.\n")
{
   object->sendInspectPostApply();
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, setObjectField, void, (const char* fieldname, const char* data ), ,
	"Set a named fields value on the inspected object if it exists. This triggers all the usual callbacks that would occur if the field had been changed through the gui..\n"
	"@param fieldname Field name on object we are inspecting we want to change."
	"@param data New Value for the given field.")
{
   object->setObjectField( fieldname, data );
}

//-----------------------------------------------------------------------------

DefineEngineMethod( GuiInspector, findByObject, S32, (SimObject* obj), ,
	"Returns the id of an awake inspector that is inspecting the passed object if one exists\n"
	"@param object Object to find away inspector for."
	"@return id of an awake inspector that is inspecting the passed object if one exists, else NULL or 0.")
{
   if ( !obj)
      return 0;

   SimObject *inspector = GuiInspector::findByObject(obj);

   if ( !inspector )
      return 0;

   return inspector->getId();
}

DefineEngineMethod(GuiInspector, createGroup, S32, (const char* groupName, S32 index), (-1),
   "Creates a new GuiInspectorGroup for this inspector and returns it's Id. If one already exists, then the Id of the existing one is returned.\n"
   "@param groupName Name of the new GuiInspectorGroup to add to this Inspector."
   "@param index(Optional) The index where to add the new group to in this Inspector's group stack."
   "@return id of the named GuiInspectorGroup")
{
   return object->createInspectorGroup(StringTable->insert(groupName), index);
}

DefineEngineMethod(GuiInspector, findExistentGroup, S32, (const char* groupName), ,
   "Finds an existing GuiInspectorGroup if it exists and returns it's Id.\n"
   "@param groupName Name of the new GuiInspectorGroup to find in this Inspector."
   "@return id of the named GuiInspectorGroup")
{
   GuiInspectorGroup* group = object->findExistentGroup(StringTable->insert(groupName));
   return group ? group->getId() : 0;
}

DefineEngineMethod(GuiInspector, getInspectedGroupCount, S32, (), ,
   "How many inspected groups there are.\n"
   "@return how many inspected groups there are")
{
   return object->getGroups().size();
}

DefineEngineMethod(GuiInspector, getInspectedGroup, GuiInspectorGroup*, (S32 key), ,
   "Finds an existing GuiInspectorGroup if it exists and returns it's Id.\n"
   "@param key nth group out of the list of groups."
   "@return id of the GuiInspectorGroup")
{
   return object->getGroups()[key];
}

DefineEngineMethod(GuiInspector, removeGroup, void, (const char* groupName), ,
   "Finds an existing GuiInspectorGroup if it exists removes it.\n"
   "@param groupName Name of the new GuiInspectorGroup to find in this Inspector.")
{
   object->removeInspectorGroup(StringTable->insert(groupName));
}

DefineEngineMethod(GuiInspector, setForcedArrayIndex, void, (S32 arrayIndex), (-1),
   "Sets the ForcedArrayIndex for the inspector. Used to force presentation of arrayed fields to only show a specific field index inside groups."
   "@param arrayIndex The specific field index for arrayed fields to show. Use -1 or blank arg to go back to normal behavior.")
{
   object->setForcedArrayIndex(arrayIndex);
}

DefineEngineMethod(GuiInspector, setSearchText, void, (const char* searchText), (""),
   "Sets the searched text used to filter out displayed fields in the inspector."
   "@param searchText The text to be used as a filter for field names. Leave as blank to clear search")
{
   object->setSearchText(searchText);
}
