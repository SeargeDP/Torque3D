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

#include "gui/editor/guiInspector.h"
#include "gui/editor/inspector/group.h"

#include "console/script.h"
#include "gui/editor/inspector/dynamicField.h"
#include "gui/editor/inspector/datablockField.h"
#include "gui/buttons/guiIconButtonCtrl.h"
#include "T3D/assets/MaterialAsset.h"
#include "T3D/assets/ShapeAsset.h"
#include "T3D/assets/ImageAsset.h"
#include "T3D/assets/SoundAsset.h"

IMPLEMENT_CONOBJECT(GuiInspectorGroup);

ConsoleDocClass( GuiInspectorGroup,
   "@brief The GuiInspectorGroup control is a helper control that the inspector "
   "makes use of which houses a collapsible pane type control for separating "
   "inspected objects fields into groups.\n\n"
   "Editor use only.\n\n"
   "@internal"
);

//#define DEBUG_SPEW


//-----------------------------------------------------------------------------

GuiInspectorGroup::GuiInspectorGroup() 
 : mParent( NULL ), 
   mStack(NULL)
{
   setBounds(0,0,200,20);

   mChildren.clear();

   setCanSave( false );

   mForcedArrayIndex = -1;

   // Make sure we receive our ticks.
   setProcessTicks();
   mMargin.set(0,0,5,0);
}

//-----------------------------------------------------------------------------

GuiInspectorGroup::GuiInspectorGroup( const String& groupName, 
                                      SimObjectPtr<GuiInspector> parent ) 
 : mParent( parent ), 
   mStack(NULL)
{

   setBounds(0,0,200,20);

   mCaption = groupName;
   setCanSave( false );

   mChildren.clear();
   mMargin.set(0,0,4,0);
}

//-----------------------------------------------------------------------------

GuiInspectorGroup::~GuiInspectorGroup()
{  
}

//-----------------------------------------------------------------------------

bool GuiInspectorGroup::onAdd()
{
   setDataField( StringTable->insert("profile"), NULL, "GuiInspectorGroupProfile" );

   if( !Parent::onAdd() )
      return false;

   // Create our inner controls. Allow subclasses to provide other content.
   if(!createContent())
      return false;

   inspectGroup();

   return true;
}

//-----------------------------------------------------------------------------

bool GuiInspectorGroup::createContent()
{
   // Create our field stack control
   mStack = new GuiStackControl();

   // Prefer GuiTransperantProfile for the stack.
   mStack->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorStackProfile" );
   mStack->setInternalName(StringTable->insert("stack"));
   if( !mStack->registerObject() )
   {
      SAFE_DELETE( mStack );
      return false;
   }

   addObject( mStack );
   mStack->setField( "padding", "0" );
   return true;
}

//-----------------------------------------------------------------------------

void GuiInspectorGroup::animateToContents()
{
   calculateHeights();
   if(size() > 0)
      animateTo( mExpanded.extent.y );
   else
      animateTo( mHeader.extent.y );
}

//-----------------------------------------------------------------------------

GuiInspectorField* GuiInspectorGroup::constructField( S32 fieldType )
{
   // See if we can construct a field of this type
   ConsoleBaseType *cbt = ConsoleBaseType::getType(fieldType);
   if( !cbt )
      return NULL;

   // Alright, is it a datablock?
   if(cbt->isDatablock())
   {
      // Default to GameBaseData
      StringTableEntry typeClassName = cbt->getTypeClassName();

      if( mParent->getNumInspectObjects() == 1 && !dStricmp(typeClassName, "GameBaseData") )
      {
         // Try and setup the classname based on the object type
         char className[256];
         dSprintf(className,256,"%sData", mParent->getInspectObject( 0 )->getClassName());
         // Walk the ACR list and find a matching class if any.
         AbstractClassRep *walk = AbstractClassRep::getClassList();
         while(walk)
         {
            if(!dStricmp(walk->getClassName(), className))
               break;

            walk = walk->getNextClass();
         }

         // We found a valid class
         if (walk)
            typeClassName = walk->getClassName();

      }


      GuiInspectorDatablockField *dbFieldClass = new GuiInspectorDatablockField( typeClassName );

      // return our new datablock field with correct datablock type enumeration info
      return dbFieldClass;
   }

   // Nope, not a datablock. So maybe it has a valid inspector field override we can use?
   if(!cbt->getInspectorFieldType())
      // Nothing, so bail.
      return NULL;

   // Otherwise try to make it!
   ConsoleObject *co = create(cbt->getInspectorFieldType());
   GuiInspectorField *gif = dynamic_cast<GuiInspectorField*>(co);

   if(!gif)
   {
      // Wasn't appropriate type, bail.
      delete co;
      return NULL;
   }

   return gif;
}

//-----------------------------------------------------------------------------

GuiInspectorField *GuiInspectorGroup::findField( const char *fieldName )
{
   // If we don't have any field children we can't very well find one then can we?
   if( mChildren.empty() )
      return NULL;

   Vector<GuiInspectorField*>::iterator i = mChildren.begin();

   for( ; i != mChildren.end(); i++ )
   {
      if( (*i)->getFieldName() != NULL && dStricmp( (*i)->getFieldName(), fieldName ) == 0 )
         return (*i);
   }

   return NULL;
}

//-----------------------------------------------------------------------------

void GuiInspectorGroup::clearFields()
{
   // Deallocates all field related controls.
   mStack->clear();

   // Then just cleanup our vectors which also point to children
   // that we keep for our own convenience.
   mArrayCtrls.clear();
   mChildren.clear();
}

//-----------------------------------------------------------------------------

bool GuiInspectorGroup::inspectGroup()
{
   // We can't inspect a group without a target!
   if( !mParent || !mParent->getNumInspectObjects() )
      return false;

   // to prevent crazy resizing, we'll just freeze our stack for a sec..
   mStack->freeze(true);

   bool bNoGroup = false;

   // Un-grouped fields are all sorted into the 'general' group
   if ( dStricmp( mCaption, "General" ) == 0 )
      bNoGroup = true;
      
   // Just delete all fields and recreate them (like the dynamicGroup)
   // because that makes creating controls for array fields a lot easier
   clearFields();
   
   bool bNewItems = false;
   bool bMakingArray = false;
   GuiStackControl *pArrayStack = NULL;
   GuiRolloutCtrl *pArrayRollout = NULL;
   bool bGrabItems = false;

   AbstractClassRep* commonAncestorClass = findCommonAncestorClass();
   AbstractClassRep::FieldList& fieldList = commonAncestorClass->mFieldList;
   for (AbstractClassRep::FieldList::iterator itr = fieldList.begin();
      itr != fieldList.end(); ++itr)
   {
      AbstractClassRep::Field* field = &(*itr);
      if (field->type == AbstractClassRep::StartGroupFieldType)
      {
         // If we're dealing with general fields, always set grabItems to true (to skip them)
         if (bNoGroup == true)
            bGrabItems = true;
         else if (dStricmp(field->pGroupname, mCaption) == 0)
            bGrabItems = true;
         continue;
      }
      else if (field->type == AbstractClassRep::EndGroupFieldType)
      {
         // If we're dealing with general fields, always set grabItems to false (to grab them)
         if (bNoGroup == true)
            bGrabItems = false;
         else if (dStricmp(field->pGroupname, mCaption) == 0)
            bGrabItems = false;
         continue;
      }

      // Skip field if it has the HideInInspectors flag set.

      if (field->flag.test(AbstractClassRep::FIELD_HideInInspectors))
         continue;

      String searchText = mParent->getSearchText();
      if (searchText != String::EmptyString) {
         if (String(field->pFieldname).find(searchText, 0, String::NoCase | String::Left) == String::NPos)
            continue;
      }

      if ((bGrabItems == true || (bNoGroup == true && bGrabItems == false)) && itr->type != AbstractClassRep::DeprecatedFieldType)
      {
         if (bNoGroup == true && bGrabItems == true)
            continue;

         if ((field->type == AbstractClassRep::StartArrayFieldType || field->type == AbstractClassRep::EndArrayFieldType) && mForcedArrayIndex != -1)
         {
            continue;
         }
         else
         {
            if (field->type == AbstractClassRep::StartArrayFieldType)
            {
#ifdef DEBUG_SPEW
               Platform::outputDebugString("[GuiInspectorGroup] Beginning array '%s'",
                  field->pFieldname );
#endif

               // Starting an array...
               // Create a rollout for the Array, give it the array's name.
               GuiRolloutCtrl* arrayRollout = new GuiRolloutCtrl();
               GuiControlProfile* arrayRolloutProfile = dynamic_cast<GuiControlProfile*>(Sim::findObject("GuiInspectorRolloutProfile0"));

               arrayRollout->setControlProfile(arrayRolloutProfile);
               //arrayRollout->mCaption = StringTable->insert( String::ToString( "%s (%i)", field->pGroupname, field->elementCount ) );
               arrayRollout->setCaption(field->pGroupname);
               //arrayRollout->setMargin( 14, 0, 0, 0 );
               arrayRollout->registerObject();

               GuiStackControl* arrayStack = new GuiStackControl();
               arrayStack->registerObject();
               arrayStack->freeze(true);
               arrayRollout->addObject(arrayStack);

               // Allocate a rollout for each element-count in the array
               // Give it the element count name.
               for (U32 i = 0; i < field->elementCount; i++)
               {
                  GuiRolloutCtrl* elementRollout = new GuiRolloutCtrl();
                  GuiControlProfile* elementRolloutProfile = dynamic_cast<GuiControlProfile*>(Sim::findObject("GuiInspectorRolloutProfile0"));

                  char buf[256];
                  dSprintf(buf, 256, "  [%i]", i);

                  elementRollout->setControlProfile(elementRolloutProfile);
                  elementRollout->setCaption(buf);
                  //elementRollout->setMargin( 14, 0, 0, 0 );
                  elementRollout->registerObject();

                  GuiStackControl* elementStack = new GuiStackControl();
                  elementStack->registerObject();
                  elementRollout->addObject(elementStack);
                  elementRollout->instantCollapse();

                  arrayStack->addObject(elementRollout);
               }

               pArrayRollout = arrayRollout;
               pArrayStack = arrayStack;
               arrayStack->freeze(false);
               pArrayRollout->instantCollapse();
               mStack->addObject(arrayRollout);

               bMakingArray = true;
               continue;
            }
            else if (field->type == AbstractClassRep::EndArrayFieldType)
            {
#ifdef DEBUG_SPEW
               Platform::outputDebugString("[GuiInspectorGroup] Ending array '%s'",
                  field->pFieldname );
#endif

               bMakingArray = false;
               continue;
            }
         }
         if ( bMakingArray )
         {
            // Add a GuiInspectorField for this field, 
            // for every element in the array...
            for ( U32 i = 0; i < pArrayStack->size(); i++ )
            {
               FrameTemp<char> intToStr( 64 );
               dSprintf( intToStr, 64, "%d", i );
               
               // The array stack should have a rollout for each element
               // as children...
               GuiRolloutCtrl *pRollout = dynamic_cast<GuiRolloutCtrl*>(pArrayStack->at(i));
               // And the each of those rollouts should have a stack for 
               // fields...
               GuiStackControl *pStack = dynamic_cast<GuiStackControl*>(pRollout->at(0));
               
               // And we add a new GuiInspectorField to each of those stacks...            
               GuiInspectorField *fieldGui = constructField( field->type );
               if ( fieldGui == NULL )                
                  fieldGui = new GuiInspectorField();
               
               fieldGui->init( mParent, this );
               StringTableEntry caption = field->pFieldname;
               fieldGui->setInspectorField( field, caption, intToStr );
               
               if( fieldGui->registerObject() )
               {
                  #ifdef DEBUG_SPEW
                  Platform::outputDebugString( "[GuiInspectorGroup] Adding array element '%s[%i]'",
                     field->pFieldname, i );
                  #endif

                  mChildren.push_back( fieldGui );
                  pStack->addObject( fieldGui );
               }
               else
                  delete fieldGui;
            }
            
            continue;
         }
         
         // This is weird, but it should work for now. - JDD
         // We are going to check to see if this item is an array
         // if so, we're going to construct a field for each array element
         if( field->elementCount > 1)
         {
            if (mForcedArrayIndex == -1)
            {
               // Make a rollout control for this array
               //
               GuiRolloutCtrl* rollout = new GuiRolloutCtrl();
               rollout->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorRolloutProfile0");
               rollout->setCaption(String::ToString("%s (%i)", field->pFieldname, field->elementCount));
               rollout->setMargin(14, 0, 0, 0);
               rollout->registerObject();
               mArrayCtrls.push_back(rollout);

               // Put a stack control within the rollout
               //
               GuiStackControl* stack = new GuiStackControl();
               stack->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorStackProfile");
               stack->registerObject();
               stack->freeze(true);
               rollout->addObject(stack);

               mStack->addObject(rollout);

               // Create each field and add it to the stack.
               //
               for (S32 nI = 0; nI < field->elementCount; nI++)
               {
                  FrameTemp<char> intToStr(64);
                  dSprintf(intToStr, 64, "%d", nI);

                  // Construct proper ValueName[nI] format which is "ValueName0" for index 0, etc.

                  String fieldName = String::ToString("%s%d", field->pFieldname, nI);

                  // If the field already exists, just update it
                  GuiInspectorField* fieldGui = findField(fieldName);
                  if (fieldGui != NULL)
                  {
                     fieldGui->updateValue();
                     continue;
                  }

                  bNewItems = true;

                  fieldGui = constructField(field->type);
                  if (fieldGui == NULL)
                     fieldGui = new GuiInspectorField();

                  fieldGui->init(mParent, this);
                  StringTableEntry caption = StringTable->insert(String::ToString("   [%i]", nI));
                  fieldGui->setInspectorField(field, caption, intToStr);

                  if (fieldGui->registerObject())
                  {
                     mChildren.push_back(fieldGui);
                     stack->addObject(fieldGui);
                  }
                  else
                     delete fieldGui;
               }

               stack->freeze(false);
               stack->updatePanes();
               rollout->instantCollapse();
            }
            else
            {
               FrameTemp<char> intToStr(64);
               dSprintf(intToStr, 64, "%d", mForcedArrayIndex);

               // Construct proper ValueName[nI] format which is "ValueName0" for index 0, etc.

               String fieldName = String::ToString("%s%d", field->pFieldname, mForcedArrayIndex);

               // If the field already exists, just update it
               GuiInspectorField* fieldGui = findField(fieldName);
               if (fieldGui != NULL)
               {
                  fieldGui->updateValue();
                  continue;
               }

               bNewItems = true;

               fieldGui = constructField(field->type);
               if (fieldGui == NULL)
                  fieldGui = new GuiInspectorField();

               fieldGui->init(mParent, this);
               fieldGui->setInspectorField(field, field->pFieldname, intToStr);

               if (fieldGui->registerObject())
               {
                  mChildren.push_back(fieldGui);
                  mStack->addObject(fieldGui);
               }
               else
                  delete fieldGui;
            }
         }
         else
         {
            // If the field already exists, just update it
            GuiInspectorField* fieldGui = findField(field->pFieldname);
            if (fieldGui != NULL)
            {
               fieldGui->updateValue();
               continue;
            }

            bNewItems = true;

            fieldGui = constructField(field->type);
            if (fieldGui == NULL)
               fieldGui = new GuiInspectorField();

            fieldGui->init(mParent, this);
            fieldGui->setInspectorField(field);

            if (fieldGui->registerObject())
            {
#ifdef DEBUG_SPEW
               Platform::outputDebugString("[GuiInspectorGroup] Adding field '%s'",
                  field->pFieldname);
#endif

               mChildren.push_back(fieldGui);
               mStack->addObject(fieldGui);
            }
            else
            {
               SAFE_DELETE(fieldGui);
            }
         }
      }
   }
   mStack->freeze(false);
   mStack->updatePanes();

   // If we've no new items, there's no need to resize anything!
   if( bNewItems == false && !mChildren.empty() )
      return true;

   sizeToContents();

   setUpdate();

   return true;
}

//-----------------------------------------------------------------------------

bool GuiInspectorGroup::updateFieldValue( StringTableEntry fieldName, StringTableEntry arrayIdx )
{
   // Check if we contain a field of this name,
   // if so update its value and return true.
   Vector<GuiInspectorField*>::iterator iter = mChildren.begin();
   
   if( arrayIdx == StringTable->EmptyString() )
      arrayIdx = NULL;

   for( ; iter != mChildren.end(); iter++ )
   {   
      GuiInspectorField *field = (*iter);
      if ( field->mField &&
           field->mField->pFieldname == fieldName &&
           field->mFieldArrayIndex == arrayIdx )
      {
         field->updateValue();
         return true;
      }
   }

   return false;
}

//-----------------------------------------------------------------------------

void GuiInspectorGroup::updateAllFields()
{   
   Vector<GuiInspectorField*>::iterator iter = mChildren.begin();
   for( ; iter != mChildren.end(); iter++ )
      (*iter)->updateValue();
}

//-----------------------------------------------------------------------------

AbstractClassRep* GuiInspectorGroup::findCommonAncestorClass()
{
   AbstractClassRep* classRep = getInspector()->getInspectObject( 0 )->getClassRep();
   const U32 numInspectObjects = getInspector()->getNumInspectObjects();
   
   for( U32 i = 1; i < numInspectObjects; ++ i )
   {
      SimObject* object = getInspector()->getInspectObject( i );
      while( !object->getClassRep()->isClass( classRep ) )
      {
         classRep = classRep->getParentClass();
         AssertFatal( classRep, "GuiInspectorGroup::findcommonAncestorClass - Walked above ConsoleObject!" );
      }
   }
      
   return classRep;
}

GuiInspectorField* GuiInspectorGroup::createInspectorField()
{
   GuiInspectorField* newField = new GuiInspectorField();

   newField->init(mParent, this);

   newField->setSpecialEditField(true);

   if (newField->registerObject())
   {
      return newField;
   }

   return NULL;
}

void GuiInspectorGroup::addInspectorField(StringTableEntry name, StringTableEntry typeName, const char* description, const char* callbackName)
{
   S32 fieldType = -1;

   String typeNameTyped = typeName;
   if (!typeNameTyped.startsWith("Type"))
      typeNameTyped = String("Type") + typeNameTyped;

   ConsoleBaseType* typeRef = AbstractClassRep::getTypeByName(typeNameTyped.c_str());
   if(typeRef)
   {
      fieldType = typeRef->getTypeID();
   }
   else
   {
      if (typeName == StringTable->insert("int"))
         fieldType = TypeS32;
      else if (typeName == StringTable->insert("float"))
         fieldType = TypeF32;
      else if (typeName == StringTable->insert("vector"))
         fieldType = TypePoint3F;
      else if (typeName == StringTable->insert("vector2"))
         fieldType = TypePoint2F;
      else if (typeName == StringTable->insert("material"))
         fieldType = TypeMaterialAssetId;
      else if (typeName == StringTable->insert("image"))
         fieldType = TypeImageAssetId;
      else if (typeName == StringTable->insert("shape"))
         fieldType = TypeShapeAssetId;
      else if (typeName == StringTable->insert("sound"))
         fieldType = TypeSoundAssetId;
      else if (typeName == StringTable->insert("bool"))
         fieldType = TypeBool;
      else if (typeName == StringTable->insert("object"))
         fieldType = TypeSimObjectPtr;
      else if (typeName == StringTable->insert("string"))
         fieldType = TypeString;
      else if (typeName == StringTable->insert("colorI"))
         fieldType = TypeColorI;
      else if (typeName == StringTable->insert("colorF"))
         fieldType = TypeColorF;
      else if (typeName == StringTable->insert("ease"))
         fieldType = TypeEaseF;
      else if (typeName == StringTable->insert("command"))
         fieldType = TypeCommand;
      else if (typeName == StringTable->insert("filename"))
         fieldType = TypeStringFilename;
   }

   GuiInspectorField* fieldGui;

   //Currently the default GuiInspectorField IS the string type, so we'll control
   //for that type here. If it's not TypeString, we allow the normal creation process
   //to continue
   if (fieldType == TypeString)
      fieldGui = new GuiInspectorField();
   else
      fieldGui = constructField(fieldType);

   if (fieldGui == nullptr)
   {
      //call down into script and see if there's special handling for that type of field
      //this allows us to have completely special-case field types implemented entirely in script
      if (isMethod("onConstructField"))
      {
         //ensure our stack variable is bound if we need it
         Con::evaluatef("%d.stack = %d;", this->getId(), mStack->getId());

         Con::executef(this, "onConstructField", name, name, typeName, description, StringTable->EmptyString(), StringTable->EmptyString(), callbackName, mParent->getInspectObject(0)->getId());
      }
   }
   else
   {
      fieldGui->init(mParent, this);

      fieldGui->setSpecialEditField(true);
      fieldGui->setTargetObject(mParent->getInspectObject(0));

      StringTableEntry fieldName = StringTable->insert(name);

      fieldGui->setSpecialEditVariableName(fieldName);
      fieldGui->setSpecialEditVariableType(typeName);
      fieldGui->setSpecialEditCallbackName(StringTable->insert(callbackName));

      fieldGui->setInspectorField(NULL, fieldName);
      fieldGui->setDocs(description);

      if (fieldGui->registerObject())
      {
         fieldGui->setValue(mParent->getInspectObject(0)->getDataField(fieldName, NULL));

         mStack->addObject(fieldGui);
      }
      else
      {
         SAFE_DELETE(fieldGui);
      }
   }
}

void GuiInspectorGroup::addInspectorField(GuiInspectorField* field)
{
   mStack->addObject(field);
   mChildren.push_back(field);
   mStack->updatePanes();
}

void GuiInspectorGroup::removeInspectorField(StringTableEntry name)
{
   for (U32 i = 0; i < mStack->size(); i++)
   {
      GuiInspectorField* field = dynamic_cast<GuiInspectorField*>(mStack->getObject(i));

      if (field == nullptr)
         continue;

      if (field->getFieldName() == name || field->getSpecialEditVariableName() == name)
      {
         mStack->removeObject(field);
         return;
      }
   }
}

void GuiInspectorGroup::hideInspectorField(StringTableEntry fieldName, bool setHidden)
{
   SimObject* inspectObj = mParent->getInspectObject();
   if (inspectObj == nullptr)
      return;

   AbstractClassRep::Field* field = const_cast<AbstractClassRep::Field*>(inspectObj->getClassRep()->findField(fieldName));

   if (field == NULL)
   {
      Con::errorf("fieldName not found: %s.%s", inspectObj->getName(), fieldName);
      return;
   }

   if (setHidden)
      field->flag.set(AbstractClassRep::FIELD_HideInInspectors);
   else
      field->flag.clear(AbstractClassRep::FIELD_HideInInspectors);
}

DefineEngineMethod(GuiInspectorGroup, createInspectorField, GuiInspectorField*, (), , "createInspectorField()")
{
   return object->createInspectorField();
}

DefineEngineMethod(GuiInspectorGroup, addField, void, (const char* fieldName, const char* fieldTypeName, const char* description, const char* callbackName),
   ("", "", "", ""),
   "Adds a new Inspector field to this group.\n"
   "@param fieldName The name of the field to add. The field will associate to a variable of the same name on the inspected object for editing purposes."
   "@param fieldTypeName The name of the type of field it is. If it's an understood existing type, it will create it as normal. If it's an unknown type, it will attempt to call into script to create it."
   "@param description (Optional) Description of the field."
   "@param callbackName (Optional) Sets a special callback function to be called when this field is edited.")
{
   if (dStrEqual(fieldName, "") || dStrEqual(fieldTypeName, ""))
      return;

   object->addInspectorField(StringTable->insert(fieldName), StringTable->insert(fieldTypeName), description, callbackName);
}


DefineEngineMethod(GuiInspectorGroup, addInspectorField, void, (GuiInspectorField* field), (nullAsType<GuiInspectorField*>()), "addInspectorField( GuiInspectorFieldObject )")
{
   if(field)
      object->addInspectorField(field);
}

DefineEngineMethod(GuiInspectorGroup, removeField, void, (const char* fieldName),
   (""),
   "Removes a Inspector field to this group of a given name.\n"
   "@param fieldName The name of the field to be removed.")
{
   if (dStrEqual(fieldName, ""))
      return;

   object->removeInspectorField(StringTable->insert(fieldName));
}

DefineEngineMethod(GuiInspectorGroup, hideField, void, (const char* fieldName, bool setHidden), (true),
   "Removes a Inspector field to this group of a given name.\n"
   "@param fieldName The name of the field to be removed.")
{
   if (dStrEqual(fieldName, ""))
      return;

   object->hideInspectorField(StringTable->insert(fieldName), setHidden);
}

DefineEngineMethod(GuiInspectorGroup, setForcedArrayIndex, void, (S32 arrayIndex), (-1),
   "Sets the ForcedArrayIndex for the group. Used to force presentation of arrayed fields to only show a specific field index."
   "@param arrayIndex The specific field index for arrayed fields to show. Use -1 or blank arg to go back to normal behavior.")
{
   object->setForcedArrayIndex(arrayIndex);
}
