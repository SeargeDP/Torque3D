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
#include "gui/editor/guiInspectorTypes.h"

#include "gui/editor/inspector/group.h"
#include "gui/controls/guiTextEditSliderCtrl.h"
#include "gui/controls/guiTextEditSliderBitmapCtrl.h"
#include "gui/buttons/guiSwatchButtonCtrl.h"
#include "gui/containers/guiDynamicCtrlArrayCtrl.h"
#include "core/strings/stringUnit.h"
#include "materials/materialDefinition.h"
#include "materials/materialManager.h"
#include "materials/customMaterialDefinition.h"
#include "gfx/gfxDrawUtil.h"
#include "sfx/sfxTypes.h"
#include "sfx/sfxParameter.h"
#include "sfx/sfxState.h"
#include "sfx/sfxSource.h"
#include "gui/editor/editorFunctions.h"
#include "math/mEase.h"
#include "math/mathTypes.h"
#include "sim/actionMap.h"
#include "console/typeValidators.h"

//-----------------------------------------------------------------------------
// GuiInspectorTypeMenuBase
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeMenuBase);

ConsoleDocClass( GuiInspectorTypeMenuBase,
   "@brief Inspector field type for MenuBase\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiControl* GuiInspectorTypeMenuBase::constructEditControl()
{
   GuiControl* retCtrl = new GuiPopUpMenuCtrlEx();

   GuiPopUpMenuCtrlEx *menu = dynamic_cast<GuiPopUpMenuCtrlEx*>(retCtrl);

   // Let's make it look pretty.
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "ToolsGuiPopupMenuProfile" );
   _registerEditControl( retCtrl );

   // Configure it to update our value when the popup is closed
   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply( %d.getText() );", getId(), menu->getId() );
   menu->setField("Command", szBuffer );

   //now add the entries, allow derived classes to override this
   _populateMenu( menu );

   // Select the active item, or just set the text field if that fails
   S32 id = menu->findText(getData());
   if (id != -1)
      menu->setSelected(id, false);
   else
      menu->setField("text", getData());

   return retCtrl;
}

void GuiInspectorTypeMenuBase::setValue( StringTableEntry newValue )
{
   GuiPopUpMenuCtrl *ctrl = dynamic_cast<GuiPopUpMenuCtrl*>( mEdit );
   if ( ctrl != NULL )
      ctrl->setText( newValue );
}

void GuiInspectorTypeMenuBase::_populateMenu( GuiPopUpMenuCtrlEx *menu )
{
   // do nothing, child classes override this.
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeEnum 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeEnum);

ConsoleDocClass( GuiInspectorTypeEnum,
   "@brief Inspector field type for Enum\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeEnum::_populateMenu( GuiPopUpMenuCtrlEx *menu )
{
   const EngineEnumTable* table = mField->table;
   if( !table )
   {
      ConsoleBaseType* type = ConsoleBaseType::getType( mField->type );
      if( type && type->getEnumTable() )
         table = type->getEnumTable();
      else
         return;
   }
      
   const EngineEnumTable& t = *table;
   const U32 numEntries = t.getNumValues();
   
   for( U32 i = 0; i < numEntries; ++ i )
      menu->addEntry( t[ i ].getName(), t[ i ] );

   menu->sort();
}

void GuiInspectorTypeEnum::consoleInit()
{
   Parent::consoleInit();

   // Set this to be the inspector type for all enumeration console types.
   
   for( ConsoleBaseType* type = ConsoleBaseType::getListHead(); type != NULL; type = type->getListNext() )
      if( type->getTypeInfo() && type->getTypeInfo()->isEnum() )
         type->setInspectorFieldType( "GuiInspectorTypeEnum" );
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeCubemapName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeCubemapName);

ConsoleDocClass( GuiInspectorTypeCubemapName,
   "@brief Inspector field type for Cubemap\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeCubemapName::_populateMenu(GuiPopUpMenuCtrlEx *menu )
{
   PROFILE_SCOPE( GuiInspectorTypeCubemapName_populateMenu );

   // This could be expensive looping through the whole RootGroup
   // and performing string comparisons... Put a profile here
   // to keep an eye on it.

   SimGroup *root = Sim::getRootGroup();
   
   SimGroupIterator iter( root );
   for ( ; *iter; ++iter )
   {
      if ( dStricmp( (*iter)->getClassName(), "CubemapData" ) == 0 )
         menu->addEntry( (*iter)->getName(), 0 );
   }

   menu->sort();
}

void GuiInspectorTypeCubemapName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeCubemapName)->setInspectorFieldType("GuiInspectorTypeCubemapName");
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeMaterialName 
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(GuiInspectorTypeMaterialName);

ConsoleDocClass( GuiInspectorTypeMaterialName,
   "@brief Inspector field type for Material\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiInspectorTypeMaterialName::GuiInspectorTypeMaterialName()
 : mBrowseButton( NULL )
{
}

void GuiInspectorTypeMaterialName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeMaterialName)->setInspectorFieldType("GuiInspectorTypeMaterialName");
}

GuiControl* GuiInspectorTypeMaterialName::construct(const char* command)
{
   GuiControl* retCtrl = new GuiTextEditCtrl();

   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(), retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   //return retCtrl;
   mBrowseButton = new GuiBitmapButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );

   dSprintf( szBuffer, 512, command, getId());
	mBrowseButton->setField( "Command", szBuffer );

	//temporary static button name
	char bitmapName[512] = "ToolsModule:change_material_btn_n_image";
	mBrowseButton->setBitmap( StringTable->insert(bitmapName) );

   mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiButtonProfile" );
   mBrowseButton->registerObject();
   addObject( mBrowseButton );

   // Position
   mBrowseButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

GuiControl* GuiInspectorTypeMaterialName::constructEditControl()
{	
   return construct("materialSelector.showDialog(\"%d.apply\", \"name\");");
}

bool GuiInspectorTypeMaterialName::updateRects()
{
   Point2I fieldPos = getPosition();
   Point2I fieldExtent = getExtent();
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
	// Icon extent 17 x 17
   mBrowseRect.set( fieldExtent.x - 20, 2, 17, fieldExtent.y - 1 );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 29, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool browseResize = false;

   if ( mBrowseButton != NULL )
   {         
      browseResize = mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }

   return ( editResize || browseResize );
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeTerrainMaterialIndex 
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(GuiInspectorTypeTerrainMaterialIndex);

ConsoleDocClass( GuiInspectorTypeTerrainMaterialIndex,
   "@brief Inspector field type for TerrainMaterialIndex\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeTerrainMaterialIndex::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeTerrainMaterialIndex)->setInspectorFieldType("GuiInspectorTypeTerrainMaterialIndex");
}

GuiControl* GuiInspectorTypeTerrainMaterialIndex::constructEditControl()
{	
   return construct("materialSelector.showTerrainDialog(\"%d.apply\", \"index\");");
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeTerrainMaterialName 
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(GuiInspectorTypeTerrainMaterialName);

ConsoleDocClass( GuiInspectorTypeTerrainMaterialName,
   "@brief Inspector field type for TerrainMaterial\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeTerrainMaterialName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeTerrainMaterialName)->setInspectorFieldType("GuiInspectorTypeTerrainMaterialName");
}

GuiControl* GuiInspectorTypeTerrainMaterialName::construct(const char* command)
{
	GuiControl* retCtrl = new GuiTextEditCtrl();

   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(), retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   //return retCtrl;
   mBrowseButton = new GuiBitmapButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );

   dSprintf( szBuffer, 512, command, getId());
	mBrowseButton->setField( "Command", szBuffer );

	//temporary static button name
	char bitmapName[512] = "ToolsModule:tools/gui/images/layers_btn_n_image";
	mBrowseButton->setBitmap(StringTable->insert(bitmapName) );

   mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiButtonProfile" );
   mBrowseButton->registerObject();
   addObject( mBrowseButton );

   // Position
   mBrowseButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

GuiControl* GuiInspectorTypeTerrainMaterialName::constructEditControl()
{	
   return construct("materialSelector.showTerrainDialog(\"%d.apply\", \"name\");");
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeGuiProfile 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeGuiProfile);

ConsoleDocClass( GuiInspectorTypeGuiProfile,
   "@brief Inspector field type for GuiProfile\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeGuiProfile::_populateMenu(GuiPopUpMenuCtrlEx *menu )
{
   // Check whether we should show profiles from the editor category.
   
   const bool showEditorProfiles = Con::getBoolVariable( "$pref::GuiEditor::showEditorProfiles", false );
   
   // Add the control profiles to the menu.
   
   SimGroup *grp = Sim::getGuiDataGroup();
   SimSetIterator iter( grp );
   for ( ; *iter; ++iter )
   {
      GuiControlProfile *profile = dynamic_cast<GuiControlProfile*>(*iter);
      if( !profile )
         continue;
      
      if( !showEditorProfiles && profile->mCategory.compare( "Editor", 0, String::NoCase ) == 0 )
         continue;
         
      menu->addEntry( profile->getName(), profile->getId() );
   }
   
   menu->sort();
}

void GuiInspectorTypeGuiProfile::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TYPEID< GuiControlProfile >() )->setInspectorFieldType("GuiInspectorTypeGuiProfile");
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeActionMap 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeActionMap);

ConsoleDocClass(GuiInspectorTypeActionMap,
   "@brief Inspector field type for ActionMap\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeActionMap::_populateMenu(GuiPopUpMenuCtrlEx* menu)
{
   // Add the action maps to the menu.
   //First add a blank entry so you can clear the action map
   menu->addEntry("", 0);

   SimGroup* grp = Sim::getRootGroup();
   SimSetIterator iter(grp);
   for (; *iter; ++iter)
   {
      ActionMap* actionMap = dynamic_cast<ActionMap*>(*iter);
      if (!actionMap)
         continue;

      menu->addEntry(actionMap->getName(), actionMap->getId());
   }

   menu->sort();
}

void GuiInspectorTypeActionMap::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TYPEID< ActionMap >())->setInspectorFieldType("GuiInspectorTypeActionMap");
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeCheckBox 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeCheckBox);

ConsoleDocClass( GuiInspectorTypeCheckBox,
   "@brief Inspector field type for CheckBox\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiControl* GuiInspectorTypeCheckBox::constructEditControl()
{
   if (mField && mField->flag.test(AbstractClassRep::FIELD_CustomInspectors))
   {
      // This checkbox (bool field) is meant to be treated as a button.
      GuiControl* retCtrl = new GuiButtonCtrl();

      // If we couldn't construct the control, bail!
      if( retCtrl == NULL )
         return retCtrl;

      GuiButtonCtrl *button = dynamic_cast<GuiButtonCtrl*>(retCtrl);

      // Let's make it look pretty.
      retCtrl->setDataField( StringTable->insert("profile"), NULL, "InspectorTypeButtonProfile" );
      retCtrl->setField( "text", "Click Here" );

      retCtrl->setScriptValue( getData() );

      _registerEditControl( retCtrl );

      // Configure it to update our value when the popup is closed
      char szBuffer[512];
      dSprintf( szBuffer, 512, "%d.apply(%d.getValue());",getId(), button->getId() );
      button->setField("Command", szBuffer );

      return retCtrl;
   }
   else if (mField && mField->flag.test(AbstractClassRep::FieldFlags::FIELD_ComponentInspectors))
   {
      // This checkbox (bool field) is meant to be treated as a button.
      GuiControl* retCtrl = new GuiButtonCtrl();

      // If we couldn't construct the control, bail!
      if (retCtrl == NULL)
         return retCtrl;

      GuiButtonCtrl *button = dynamic_cast<GuiButtonCtrl*>(retCtrl);

      // Let's make it look pretty.
      retCtrl->setDataField(StringTable->insert("profile"), NULL, "InspectorTypeButtonProfile");
      retCtrl->setField("text", "Click Here");

      retCtrl->setScriptValue(getData());

      _registerEditControl(retCtrl);

      // Configure it to update our value when the popup is closed
      char szBuffer[512];
      dSprintf(szBuffer, 512, "%d.apply(%d.getValue());", getId(), button->getId());
      button->setField("Command", szBuffer);

      return retCtrl;
   }
   else 
   {
      GuiControl* retCtrl = new GuiCheckBoxCtrl();

      GuiCheckBoxCtrl *check = dynamic_cast<GuiCheckBoxCtrl*>(retCtrl);

      // Let's make it look pretty.
      retCtrl->setDataField(StringTable->insert("profile"), NULL, "InspectorTypeCheckboxProfile");
      retCtrl->setField("text", "");

      check->setIndent(4);

      retCtrl->setScriptValue(getData());

      _registerEditControl(retCtrl);

      // Configure it to update our value when the popup is closed
      char szBuffer[512];
      dSprintf(szBuffer, 512, "%d.apply(%d.getValue());", getId(), check->getId());
      check->setField("Command", szBuffer);

      return retCtrl;
   }
}


void GuiInspectorTypeCheckBox::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeBool)->setInspectorFieldType("GuiInspectorTypeCheckBox");
}

void GuiInspectorTypeCheckBox::setValue( StringTableEntry newValue )
{
   GuiButtonBaseCtrl *ctrl = dynamic_cast<GuiButtonBaseCtrl*>( mEdit );
   if ( ctrl != NULL )
      ctrl->setStateOn( dAtob(newValue) );
}

const char* GuiInspectorTypeCheckBox::getValue()
{
   GuiButtonBaseCtrl *ctrl = dynamic_cast<GuiButtonBaseCtrl*>( mEdit );
   if ( ctrl != NULL )
      return ctrl->getScriptValue();

   return NULL;
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeFileName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeFileName);

ConsoleDocClass( GuiInspectorTypeFileName,
   "@brief Inspector field type for FileName\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeFileName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeFilename)->setInspectorFieldType("GuiInspectorTypeFileName");
   ConsoleBaseType::getType(TypeStringFilename)->setInspectorFieldType("GuiInspectorTypeFileName");
}

GuiControl* GuiInspectorTypeFileName::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditCtrl();

   // Let's make it look pretty.
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditRightProfile" );
   retCtrl->setDataField( StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile" );
   retCtrl->setDataField( StringTable->insert("hovertime"), NULL, "1000" );

   // Don't forget to register ourselves
   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(),retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   mBrowseButton = new GuiButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );

   dSprintf( szBuffer, 512, "getLoadFilename(\"*.*|*.*\", \"%d.apply\", %d.getData());", getId(), getId() );
   mBrowseButton->setField( "Command", szBuffer );
   mBrowseButton->setField( "text", "..." );
   mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiInspectorButtonProfile" );
   mBrowseButton->registerObject();
   addObject( mBrowseButton );

   // Position
   mBrowseButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

bool GuiInspectorTypeFileName::resize( const Point2I &newPosition, const Point2I &newExtent )
{
   if ( !Parent::resize( newPosition, newExtent ) )
      return false;

   if ( mEdit != NULL )
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorTypeFileName::updateRects()
{   
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 32, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool browseResize = false;

   if ( mBrowseButton != NULL )
   {
      mBrowseRect.set( fieldExtent.x - 20, 2, 14, fieldExtent.y - 4 );
      browseResize = mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }

   return ( editResize || browseResize );
}

void GuiInspectorTypeFileName::updateValue()
{
   if ( mField )
   {
      Parent::updateValue();
      const char* data = getData();
      if(!data)
         data = "";
      mEdit->setDataField( StringTable->insert("tooltip"), NULL, data );
   }
}

DefineEngineMethod(GuiInspectorTypeFileName, apply, void, (String path), , "")
{
   if (path.isNotEmpty())
      path = Platform::makeRelativePathName(path, Platform::getMainDotCsDir());

   object->setData(path.c_str());
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeImageFileName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeImageFileName);

ConsoleDocClass( GuiInspectorTypeImageFileName,
   "@brief Inspector field type for FileName\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeImageFileName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeImageFilename)->setInspectorFieldType("GuiInspectorTypeImageFileName");
}

GuiControl* GuiInspectorTypeImageFileName::constructEditControl()
{
   GuiControl *retCtrl = Parent::constructEditControl();
   
   if ( retCtrl == NULL )
      return retCtrl;
   
   retCtrl->getRenderTooltipDelegate().bind( this, &GuiInspectorTypeImageFileName::renderTooltip );
   char szBuffer[512];

   String extList = GBitmap::sGetExtensionList();
   extList += "dds";
   U32 extCount = StringUnit::getUnitCount( extList, " " );

   String fileSpec;

   // building the fileSpec string 

   fileSpec += "All Image Files|";

   for ( U32 i = 0; i < extCount; i++ )
   {
      fileSpec += "*.";
      fileSpec += StringUnit::getUnit( extList, i, " " );

      if ( i < extCount - 1 )
         fileSpec += ";";
   }

   fileSpec += "|";

   for ( U32 i = 0; i < extCount; i++ )
   {
      String ext = StringUnit::getUnit( extList, i, " " );
      fileSpec += ext;
      fileSpec += "|*.";
      fileSpec += ext;

      if ( i != extCount - 1 )
         fileSpec += "|";
   }

   dSprintf( szBuffer, 512, "getLoadFilename(\"%s\", \"%d.apply\", %d.getData());", fileSpec.c_str(), getId(), getId() );
   mBrowseButton->setField( "Command", szBuffer );

   return retCtrl;
}

bool GuiInspectorTypeImageFileName::renderTooltip( const Point2I &hoverPos, const Point2I &cursorPos, const char *tipText )
{
   if ( !mAwake ) 
      return false;

   GuiCanvas *root = getRoot();
   if ( !root )
      return false;

   StringTableEntry filename = getData();
   if ( !filename || !filename[0] )
      return false;

   GFXTexHandle texture( filename, &GFXStaticTextureSRGBProfile, avar("%s() - tooltip texture (line %d)", __FUNCTION__, __LINE__) );
   if ( texture.isNull() )
      return false;

   // Render image at a reasonable screen size while 
   // keeping its aspect ratio...
   Point2I screensize = getRoot()->getWindowSize();
   Point2I offset = hoverPos; 
   Point2I tipBounds;

   U32 texWidth = texture.getWidth();
   U32 texHeight = texture.getHeight();
   F32 aspect = (F32)texHeight / (F32)texWidth;

   const F32 newWidth = 150.0f;
   F32 newHeight = aspect * newWidth;

   // Offset below cursor image
   offset.y += 20; // TODO: Attempt to fix?: root->getCursorExtent().y;
   tipBounds.x = newWidth;
   tipBounds.y = newHeight;

   // Make sure all of the tooltip will be rendered width the app window,
   // 5 is given as a buffer against the edge
   if ( screensize.x < offset.x + tipBounds.x + 5 )
      offset.x = screensize.x - tipBounds.x - 5;
   if ( screensize.y < offset.y + tipBounds.y + 5 )
      offset.y = hoverPos.y - tipBounds.y - 5;

   RectI oldClip = GFX->getClipRect();
   RectI rect( offset, tipBounds );
   GFX->setClipRect( rect );

   GFXDrawUtil *drawer = GFX->getDrawUtil();
   drawer->clearBitmapModulation();
   GFX->getDrawUtil()->drawBitmapStretch( texture, rect );

   GFX->setClipRect( oldClip );

   return true;
}


//-----------------------------------------------------------------------------
// GuiInspectorTypePrefabFilename 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypePrefabFilename);

ConsoleDocClass( GuiInspectorTypePrefabFilename,
   "@brief Inspector field type for PrefabFilename\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypePrefabFilename::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypePrefabFilename)->setInspectorFieldType("GuiInspectorTypePrefabFilename");
}

GuiControl* GuiInspectorTypePrefabFilename::constructEditControl()
{
   GuiControl *retCtrl = Parent::constructEditControl();

   if ( retCtrl == NULL )
      return retCtrl;
   
   const char *fileSpec = "Prefab Files (*.prefab)|*.prefab|All Files (*.*)|*.*|";

   char szBuffer[512];
   dSprintf( szBuffer, 512, "getLoadFilename(\"%s\", \"%d.apply\", %d.getData());", fileSpec, getId(), getId() );

   mBrowseButton->setField( "Command", szBuffer );

   return retCtrl;
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeShapeFileName 
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT(GuiInspectorTypeShapeFilename);

ConsoleDocClass( GuiInspectorTypeShapeFilename,
   "@brief Inspector field type for Shapes\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeShapeFilename::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeShapeFilename)->setInspectorFieldType("GuiInspectorTypeShapeFilename");
}

GuiControl* GuiInspectorTypeShapeFilename::constructEditControl()
{
   // Create base filename edit controls
   GuiControl *retCtrl = Parent::constructEditControl();
   if ( retCtrl == NULL )
      return retCtrl;

   // Change filespec
   char szBuffer[512];
   dSprintf( szBuffer, sizeof(szBuffer), "getLoadFormatFilename(\"%d.apply\", %d.getData());", getId(), getId() );
   mBrowseButton->setField( "Command", szBuffer );

   // Create "Open in ShapeEditor" button
   mShapeEdButton = new GuiBitmapButtonCtrl();

   dSprintf(szBuffer, sizeof(szBuffer), "ShapeEditorPlugin.open(%d.getText());", retCtrl->getId());
   mShapeEdButton->setField("Command", szBuffer);

   char bitmapName[512] = "ToolsModule:shape_editor_n_image";
   mShapeEdButton->setBitmap(StringTable->insert(bitmapName));

   mShapeEdButton->setDataField(StringTable->insert("Profile"), NULL, "GuiButtonProfile");
   mShapeEdButton->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   mShapeEdButton->setDataField(StringTable->insert("hovertime"), NULL, "1000");
   mShapeEdButton->setDataField(StringTable->insert("tooltip"), NULL, "Open this file in the Shape Editor");

   mShapeEdButton->registerObject();
   addObject(mShapeEdButton);

   return retCtrl;
}

bool GuiInspectorTypeShapeFilename::updateRects()
{
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 34, fieldExtent.y );

   bool resized = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   if ( mBrowseButton != NULL )
   {
      mBrowseRect.set( fieldExtent.x - 32, 2, 14, fieldExtent.y - 4 );
      resized |= mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }
   if ( mShapeEdButton != NULL )
   {
      RectI shapeEdRect( fieldExtent.x - 16, 2, 14, fieldExtent.y - 4 );
      resized |= mShapeEdButton->resize( shapeEdRect.point, shapeEdRect.extent );
   }

   return resized;
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeCommand
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeCommand);

ConsoleDocClass( GuiInspectorTypeCommand,
   "@brief Inspector field type for Command\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeCommand::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeCommand)->setInspectorFieldType("GuiInspectorTypeCommand");
}

GuiInspectorTypeCommand::GuiInspectorTypeCommand()
{
   mTextEditorCommand = StringTable->insert("TextPad");
}

GuiControl* GuiInspectorTypeCommand::constructEditControl()
{
   GuiButtonCtrl* retCtrl = new GuiButtonCtrl();

   // Let's make it look pretty.
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   // Don't forget to register ourselves
   _registerEditControl( retCtrl );

   _setCommand( retCtrl, getData() );

   return retCtrl;
}

void GuiInspectorTypeCommand::setValue( StringTableEntry newValue )
{
   GuiButtonCtrl *ctrl = dynamic_cast<GuiButtonCtrl*>( mEdit );
   _setCommand( ctrl, newValue );
}

void GuiInspectorTypeCommand::_setCommand( GuiButtonCtrl *ctrl, StringTableEntry command )
{
   if( ctrl != NULL )
   {
      ctrl->setField( "text", command );

      // expandEscape isn't length-limited, so while this _should_ work
      // in most circumstances, it may still fail if getData() has lots of
      // non-printable characters
      S32 len = 2 * dStrlen(command) + 512;

      FrameTemp<char> szBuffer(len);

	   S32 written = dSprintf( szBuffer, len, "%s(\"", mTextEditorCommand );
      expandEscape(szBuffer.address() + written, command);
      written = (S32)strlen(szBuffer);
      dSprintf( szBuffer.address() + written, len - written, "\", \"%d.apply\", %d.getRoot());", getId(), getId() );

	   ctrl->setField( "Command", szBuffer );
   }
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeRectUV
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT( GuiInspectorTypeRectUV );

ConsoleDocClass( GuiInspectorTypeRectUV,
   "@brief Inspector field type for TypeRectUV.\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiInspectorTypeRectUV::GuiInspectorTypeRectUV()
 : mBrowseButton( NULL )
{
}


void GuiInspectorTypeRectUV::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TypeRectUV )->setInspectorFieldType( "GuiInspectorTypeRectUV" );
}

GuiControl* GuiInspectorTypeRectUV::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditCtrl();

   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(), retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   //return retCtrl;
   mBrowseButton = new GuiBitmapButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );

   dSprintf( szBuffer, 512, "uvEditor.showDialog(\"%d.apply\", %d, %d.getText());", getId(), mInspector->getInspectObject()->getId(), retCtrl->getId());
	mBrowseButton->setField( "Command", szBuffer );

	//temporary static button name
	char bitmapName[512] = "ToolsModule:uv_editor_btn_n_image";
	mBrowseButton->setBitmap(StringTable->insert(bitmapName) );

   mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiButtonProfile" );
   mBrowseButton->registerObject();
   addObject( mBrowseButton );

   // Position
   mBrowseButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

bool GuiInspectorTypeRectUV::updateRects()
{
   Point2I fieldPos = getPosition();
   Point2I fieldExtent = getExtent();
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
	// Icon extent 17 x 17
   mBrowseRect.set( fieldExtent.x - 20, 2, 17, fieldExtent.y - 1 );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 29, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool browseResize = false;

   if ( mBrowseButton != NULL )
   {         
      browseResize = mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }

   return ( editResize || browseResize );
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeEaseF
//-----------------------------------------------------------------------------

IMPLEMENT_CONOBJECT( GuiInspectorTypeEaseF );

ConsoleDocClass( GuiInspectorTypeEaseF,
   "@brief Inspector field type for TypeEaseF.\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiInspectorTypeEaseF::GuiInspectorTypeEaseF()
   : mBrowseButton( NULL )
{
}

void GuiInspectorTypeEaseF::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TypeEaseF )->setInspectorFieldType( "GuiInspectorTypeEaseF" );
}

GuiControl* GuiInspectorTypeEaseF::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditCtrl();

   // Let's make it look pretty.
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   // Don't forget to register ourselves
   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(),retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   mBrowseButton = new GuiButtonCtrl();
   {
      RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );
      dSprintf( szBuffer, sizeof( szBuffer ), "GetEaseF(%d.getText(), \"%d.apply\", %d.getRoot());", retCtrl->getId(), getId(), getId() );
      mBrowseButton->setField( "Command", szBuffer );
      mBrowseButton->setField( "text", "E" );
      mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiInspectorButtonProfile" );
      mBrowseButton->registerObject();
      addObject( mBrowseButton );

      // Position
      mBrowseButton->resize( browseRect.point, browseRect.extent );
   }

   return retCtrl;
}

bool GuiInspectorTypeEaseF::resize( const Point2I &newPosition, const Point2I &newExtent )
{
   if ( !Parent::resize( newPosition, newExtent ) )
      return false;

   if ( mEdit != NULL )
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorTypeEaseF::updateRects()
{   
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 32, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool browseResize = false;

   if ( mBrowseButton != NULL )
   {
      mBrowseRect.set( fieldExtent.x - 20, 2, 14, fieldExtent.y - 4 );
      browseResize = mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }

   return ( editResize || browseResize );
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeColor (Base for ColorI/LinearColorF) 
//-----------------------------------------------------------------------------
GuiInspectorTypeColor::GuiInspectorTypeColor()
 : mColorFunction(NULL), mBrowseButton( NULL )
{
}

IMPLEMENT_CONOBJECT(GuiInspectorTypeColor);

ConsoleDocClass( GuiInspectorTypeColor,
   "@brief Inspector field type for TypeColor\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiControl* GuiInspectorTypeColor::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditCtrl();

   // Let's make it look pretty.
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   // Don't forget to register ourselves
   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(), retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );

   mBrowseButton = new GuiSwatchButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );
   mBrowseButton->setDataField( StringTable->insert("Profile"), NULL, "GuiInspectorSwatchButtonProfile" );
   mBrowseButton->registerObject();
   addObject( mBrowseButton );
		
   char szColor[2048];
   if( _getColorConversionFunction() )
      dSprintf( szColor, 512, "%s( %d.color )", _getColorConversionFunction(), mBrowseButton->getId() );
   else
      dSprintf( szColor, 512, "%d.color", mBrowseButton->getId() );
         
   // If the inspector supports the alternate undo recording path,
   // use this here.

   GuiInspector* inspector = getInspector();
   if( inspector->isMethod( "onInspectorPreFieldModification" ) )
   {
      dSprintf( szBuffer, sizeof( szBuffer ),
         "%d.onInspectorPreFieldModification(\"%s\",\"%s\"); %s(%s, \"%d.onInspectorPostFieldModification(); %d.applyWithoutUndo\", %d.getRoot(), \"%d.applyWithoutUndo\", \"%d.onInspectorDiscardFieldModification(); %%unused=\");",
         inspector->getId(), getRawFieldName(), getArrayIndex(),
         mColorFunction, szColor, inspector->getId(), getId(),
         getId(),
         getId(),
         inspector->getId()
      );
   }
   else
      dSprintf( szBuffer, sizeof( szBuffer ),
         "%s(%s, \"%d.apply\", %d.getRoot());",
         mColorFunction, szColor, getId(), getId() );
		
	mBrowseButton->setConsoleCommand( szBuffer );
   mBrowseButton->setUseMouseEvents( true ); // Allow drag&drop.

   // Position
   mBrowseButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

bool GuiInspectorTypeColor::resize( const Point2I &newPosition, const Point2I &newExtent )
{
   if( !Parent::resize( newPosition, newExtent ) )
      return false;

   return false;
}

bool GuiInspectorTypeColor::updateRects()
{
   Point2I fieldPos = getPosition();
   Point2I fieldExtent = getExtent();
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
   mBrowseRect.set( fieldExtent.x - 20, 2, 14, fieldExtent.y - 4 );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 29, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool browseResize = false;

   if ( mBrowseButton != NULL )
   {         
      browseResize = mBrowseButton->resize( mBrowseRect.point, mBrowseRect.extent );
   }

   return ( editResize || browseResize );
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeColorI
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeColorI);

ConsoleDocClass( GuiInspectorTypeColorI,
   "@brief Inspector field type for ColorI\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeColorI::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeColorI)->setInspectorFieldType("GuiInspectorTypeColorI");
}

GuiInspectorTypeColorI::GuiInspectorTypeColorI()
{
   mColorFunction = StringTable->insert("getColorI");
}

void GuiInspectorTypeColorI::setValue( StringTableEntry newValue )
{
   // Allow parent to set the edit-ctrl text to the new value.
   Parent::setValue( newValue );

   // Now we also set our color swatch button to the new color value.
   if ( mBrowseButton )
   {      
      ColorI color(255,0,255,255);
      S32 r,g,b,a;
      dSscanf( newValue, "%d %d %d %d", &r, &g, &b, &a );
      color.red = r;
      color.green = g;
      color.blue = b;
      color.alpha = a;
      mBrowseButton->setColor( color );
   }
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeColorF
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeColorF);

ConsoleDocClass( GuiInspectorTypeColorF,
   "@brief Inspector field type for LinearColorF\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeColorF::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeColorF)->setInspectorFieldType("GuiInspectorTypeColorF");
}

GuiInspectorTypeColorF::GuiInspectorTypeColorF()
{
   mColorFunction = StringTable->insert("getColorF");
}

void GuiInspectorTypeColorF::setValue( StringTableEntry newValue )
{
   // Allow parent to set the edit-ctrl text to the new value.
   Parent::setValue( newValue );

   // Now we also set our color swatch button to the new color value.
   if ( mBrowseButton )
   {      
      LinearColorF color(1,0,1,1);
      dSscanf( newValue, "%f %f %f %f", &color.red, &color.green, &color.blue, &color.alpha );
      mBrowseButton->setColor( color );
   }
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeS32
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeS32);

ConsoleDocClass( GuiInspectorTypeS32,
   "@brief Inspector field type for S32\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeS32::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeS32)->setInspectorFieldType("GuiInspectorTypeS32");
}

GuiControl* GuiInspectorTypeS32::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditSliderCtrl();

   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );

   // Don't forget to register ourselves
   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());",getId(), retCtrl->getId() );
   retCtrl->setField("AltCommand", szBuffer );
   retCtrl->setField("Validate", szBuffer );
   retCtrl->setField("increment","1");
   retCtrl->setField("format","%d");
   retCtrl->setField("range","-2147483648 2147483647");

   return retCtrl;
}

void GuiInspectorTypeS32::setValue( StringTableEntry newValue )
{
   GuiTextEditSliderCtrl *ctrl = dynamic_cast<GuiTextEditSliderCtrl*>( mEdit );
   if( ctrl != NULL )
      ctrl->setText( newValue );
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeRangedF32
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeRangedF32);

ConsoleDocClass(GuiInspectorTypeRangedF32,
   "@brief Inspector field type for range-clamped F32\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeRangedF32::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeRangedF32)->setInspectorFieldType("GuiInspectorTypeRangedF32");
}

GuiControl* GuiInspectorTypeRangedF32::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditSliderCtrl();

   retCtrl->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");

   // Don't forget to register ourselves
   _registerEditControl(retCtrl);

   char szBuffer[512];
   dSprintf(szBuffer, 512, "%d.apply(%d.getText());", getId(), retCtrl->getId());
   retCtrl->setField("AltCommand", szBuffer);
   FRangeValidator* validator = dynamic_cast<FRangeValidator*>(mField->validator);
   if (validator)
   {
      retCtrl->setField("format", "%g");
      retCtrl->setField("range", String::ToString("%g %g", validator->getMin(), validator->getMax()));
      if (validator->getFidelity()>0.0f)
         retCtrl->setField("increment", String::ToString("%g", (validator->getMax()-validator->getMin())/validator->getFidelity()));
      else
         retCtrl->setField("increment", String::ToString("%g", POINT_EPSILON));
   }
   return retCtrl;
}

void GuiInspectorTypeRangedF32::setValue(StringTableEntry newValue)
{
   GuiTextEditSliderCtrl* ctrl = dynamic_cast<GuiTextEditSliderCtrl*>(mEdit);
   if (ctrl != NULL)
      ctrl->setText(newValue);
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeRangedS32
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeRangedS32);

ConsoleDocClass(GuiInspectorTypeRangedS32,
   "@brief Inspector field type for range-clamped S32\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeRangedS32::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeRangedS32)->setInspectorFieldType("GuiInspectorTypeRangedS32");
}

GuiControl* GuiInspectorTypeRangedS32::constructEditControl()
{
   GuiControl* retCtrl = new GuiTextEditSliderCtrl();

   retCtrl->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");

   // Don't forget to register ourselves
   _registerEditControl(retCtrl);

   char szBuffer[512];
   dSprintf(szBuffer, 512, "%d.apply(%d.getText());", getId(), retCtrl->getId());
   retCtrl->setField("AltCommand", szBuffer);
   IRangeValidator* validator = dynamic_cast<IRangeValidator*>(mField->validator);

   retCtrl->setField("increment", "1");
   retCtrl->setField("format", "%d");
   retCtrl->setField("range", "-2147483648 2147483647");

   if (validator)
   {
      retCtrl->setField("range", String::ToString("%d %d", validator->getMin(), validator->getMax()));
      if (validator->getFidelity() > 1)
         retCtrl->setField("increment", String::ToString("%d", (validator->getMax() - validator->getMin()) / validator->getFidelity()));
   }
   else
   {
      IRangeValidatorScaled* scaledValidator = dynamic_cast<IRangeValidatorScaled*>(mField->validator);
      if (scaledValidator)
      {
         retCtrl->setField("range", String::ToString("%d %d", scaledValidator->getMin(), scaledValidator->getMax()));
         if (validator->getFidelity() > 1)
            retCtrl->setField("increment", String::ToString("%d", scaledValidator->getScaleFactor()));
      }
   }
   return retCtrl;
}

void GuiInspectorTypeRangedS32::setValue(StringTableEntry newValue)
{
   GuiTextEditSliderCtrl* ctrl = dynamic_cast<GuiTextEditSliderCtrl*>(mEdit);
   if (ctrl != NULL)
      ctrl->setText(newValue);
}
//-----------------------------------------------------------------------------
// GuiInspectorTypeS32Mask
//-----------------------------------------------------------------------------

GuiInspectorTypeBitMask32::GuiInspectorTypeBitMask32()
 : mHelper( NULL ),
   mRollout( NULL ),
   mArrayCtrl( NULL )
{
}

IMPLEMENT_CONOBJECT( GuiInspectorTypeBitMask32 );

ConsoleDocClass( GuiInspectorTypeBitMask32,
   "@brief Inspector field type for TypeBitMask32.\n\n"
   "Editor use only.\n\n"
   "@internal"
);

bool GuiInspectorTypeBitMask32::onAdd()
{
   // Skip our parent because we aren't using mEditCtrl
   // and according to our parent that would be cause to fail onAdd.
   if ( !Parent::Parent::onAdd() )
      return false;

   if ( !mInspector )
      return false;

   const EnumTable* table = mField->table;
   if( !table )
   {
      ConsoleBaseType* type = ConsoleBaseType::getType( mField->type );
      if( type && type->getEnumTable() )
         table = type->getEnumTable();
      else
         return false;
   }

   static StringTableEntry sProfile = StringTable->insert( "profile" );
   setDataField( sProfile, NULL, "GuiInspectorFieldProfile" );   
   setBounds(0,0,100,18);

   // Allocate our children controls...

   mRollout = new GuiRolloutCtrl();
   mRollout->setMargin( 14, 0, 0, 0 );
   mRollout->setCanCollapse( false );
   mRollout->registerObject();
   addObject( mRollout );
   
   mArrayCtrl = new GuiDynamicCtrlArrayControl();
   mArrayCtrl->setDataField( sProfile, NULL, "GuiInspectorBitMaskArrayProfile" );
   mArrayCtrl->setField( "autoCellSize", "true" );
   mArrayCtrl->setField( "fillRowFirst", "true" );
   mArrayCtrl->setField( "dynamicSize", "true" );
   mArrayCtrl->setField( "rowSpacing", "4" );
   mArrayCtrl->setField( "colSpacing", "1" );
   mArrayCtrl->setField( "frozen", "true" );
   mArrayCtrl->registerObject();
   
   mRollout->addObject( mArrayCtrl );

   GuiCheckBoxCtrl *pCheckBox = NULL;
   
   const EngineEnumTable& t = *table;
   const U32 numValues = t.getNumValues();

   for ( S32 i = 0; i < numValues; i++ )
   {   
      pCheckBox = new GuiCheckBoxCtrl();
      pCheckBox->setText( t[ i ].getName() );
      pCheckBox->registerObject();      
      mArrayCtrl->addObject( pCheckBox );

      pCheckBox->autoSize();

      // Override the normal script callbacks for GuiInspectorTypeCheckBox
      char szBuffer[512];
      dSprintf( szBuffer, 512, "%d.applyBit();", getId() );
      pCheckBox->setField( "Command", szBuffer );   
   }      

   mArrayCtrl->setField( "frozen", "false" );
   mArrayCtrl->refresh(); 

   mHelper = new GuiInspectorTypeBitMask32Helper();
   mHelper->init( mInspector, mParent );
   mHelper->mParentRollout = mRollout;
   mHelper->mParentField = this;
   mHelper->setInspectorField( mField, mCaption, mFieldArrayIndex );
   mHelper->registerObject();
   mHelper->setExtent( pCheckBox->getExtent() );
   mHelper->setPosition( 0, 0 );
   mRollout->addObject( mHelper );

   mRollout->sizeToContents();
   mRollout->instantCollapse();  

   updateValue();

   return true;
}

void GuiInspectorTypeBitMask32::consoleInit()
{
   Parent::consoleInit();

   // Set this to be the inspector type for all bitfield console types.

   for( ConsoleBaseType* type = ConsoleBaseType::getListHead(); type != NULL; type = type->getListNext() )
      if( type->getTypeInfo() && type->getTypeInfo()->isBitfield() )
         type->setInspectorFieldType( "GuiInspectorTypeBitMask32" );
}

void GuiInspectorTypeBitMask32::childResized( GuiControl *child )
{
   setExtent( mRollout->getExtent() );
}

bool GuiInspectorTypeBitMask32::resize(const Point2I &newPosition, const Point2I &newExtent)
{
   if ( !Parent::resize( newPosition, newExtent ) )
      return false;
   
   // Hack... height of 18 is hardcoded
   return mHelper->resize( Point2I(0,0), Point2I( newExtent.x, 18 ) );
}

bool GuiInspectorTypeBitMask32::updateRects()
{
   if ( !mRollout )
      return false;

   bool result = mRollout->setExtent( getExtent() );   
   
   for ( U32 i = 0; i < mArrayCtrl->size(); i++ )
   {
      GuiInspectorField *pField = dynamic_cast<GuiInspectorField*>( mArrayCtrl->at(i) );
      if ( pField )
         if ( pField->updateRects() )
            result = true;
   }

   if ( mHelper && mHelper->updateRects() )
      result = true;

   return result;   
}

StringTableEntry GuiInspectorTypeBitMask32::getValue()
{
   if ( !mRollout )
      return StringTable->insert( "" );

   S32 mask = 0;

   for ( U32 i = 0; i < mArrayCtrl->size(); i++ )
   {
      GuiCheckBoxCtrl *pCheckBox = dynamic_cast<GuiCheckBoxCtrl*>( mArrayCtrl->at(i) );

      bool bit = pCheckBox->getStateOn();
      mask |= bit << i;
   }

   return StringTable->insert( String::ToString(mask).c_str() );
}

void GuiInspectorTypeBitMask32::setValue( StringTableEntry value )
{
   U32 mask = dAtoui( value );

   if (mask == 0 && mask != -1) //zero we need to double check. -1 we know is all on
   {
      BitSet32 bitMask;
      const EngineEnumTable* table = mField->table;
      if (!table)
      {
         ConsoleBaseType* type = ConsoleBaseType::getType(mField->type);
         if (type && type->getEnumTable())
            table = type->getEnumTable();
      }

      if (table)
      {
         const EngineEnumTable& t = *table;
         const U32 numEntries = t.getNumValues();
         String inString(value);

         for (U32 i = 0; i < numEntries; i++)
         {
            if (inString.find(t[i].getName()) != String::NPos)
               bitMask.set(t[i].getInt());
         }
         mask = bitMask.getMask();
      }
   }

   for ( U32 i = 0; i < mArrayCtrl->size(); i++ )
   {
      GuiCheckBoxCtrl *pCheckBox = dynamic_cast<GuiCheckBoxCtrl*>( mArrayCtrl->at(i) );

      bool bit = mask & ( 1 << i );
      pCheckBox->setStateOn( bit );
   }

   mHelper->setValue( value );
}

void GuiInspectorTypeBitMask32::updateData()
{
   StringTableEntry data = getValue();
   setData( data );   
}

DefineEngineMethod( GuiInspectorTypeBitMask32, applyBit, void, (),, "" )
{
   object->updateData();
}

//------------------------------------------------------------------------------
// GuiInspectorTypeS32MaskHelper
//------------------------------------------------------------------------------

GuiInspectorTypeBitMask32Helper::GuiInspectorTypeBitMask32Helper()
: mButton( NULL ),
  mParentRollout( NULL ),
  mParentField( NULL )
{
}

IMPLEMENT_CONOBJECT( GuiInspectorTypeBitMask32Helper );

ConsoleDocClass( GuiInspectorTypeBitMask32Helper,
   "@brief Inspector field type support for TypeBitMask32.\n\n"
   "Editor use only.\n\n"
   "@internal"
);

GuiControl* GuiInspectorTypeBitMask32Helper::constructEditControl()
{
   GuiControl *retCtrl = new GuiTextEditCtrl();
   retCtrl->setDataField( StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile" );
   retCtrl->setField( "hexDisplay", "true" );

   _registerEditControl( retCtrl );

   char szBuffer[512];
   dSprintf( szBuffer, 512, "%d.apply(%d.getText());", mParentField->getId(), retCtrl->getId() );
   retCtrl->setField( "AltCommand", szBuffer );
   retCtrl->setField( "Validate", szBuffer );

   mButton = new GuiBitmapButtonCtrl();

   RectI browseRect( Point2I( ( getLeft() + getWidth()) - 26, getTop() + 2), Point2I(20, getHeight() - 4) );
   dSprintf( szBuffer, 512, "%d.toggleExpanded(false);", mParentRollout->getId() );
   mButton->setField( "Command", szBuffer );
   mButton->setField( "buttonType", "ToggleButton" );
   mButton->setDataField( StringTable->insert("Profile"), NULL, "GuiInspectorButtonProfile" );
   mButton->setBitmap(StringTable->insert("ToolsModule:arrowBtn_n_image") );
   mButton->setStateOn( true );
   mButton->setExtent( 16, 16 );
   mButton->registerObject();
   addObject( mButton );

   mButton->resize( browseRect.point, browseRect.extent );

   return retCtrl;
}

bool GuiInspectorTypeBitMask32Helper::resize(const Point2I &newPosition, const Point2I &newExtent)
{
   if ( !Parent::resize( newPosition, newExtent ) )
      return false;

   if ( mEdit != NULL )
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorTypeBitMask32Helper::updateRects()
{
   S32 dividerPos, dividerMargin;
   mInspector->getDivider( dividerPos, dividerMargin );
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mCaptionRect.set( 0, 0, fieldExtent.x - dividerPos - dividerMargin, fieldExtent.y );
   mEditCtrlRect.set( fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 32, fieldExtent.y );

   bool editResize = mEdit->resize( mEditCtrlRect.point, mEditCtrlRect.extent );
   bool buttonResize = false;

   if ( mButton != NULL )
   {
      mButtonRect.set( fieldExtent.x - 26, 2, 16, 16 );
      buttonResize = mButton->resize( mButtonRect.point, mButtonRect.extent );
   }

   return ( editResize || buttonResize );
}

void GuiInspectorTypeBitMask32Helper::setValue( StringTableEntry newValue )
{
   GuiTextEditCtrl *edit = dynamic_cast<GuiTextEditCtrl*>(mEdit);
   edit->setText( newValue );
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeName);

ConsoleDocClass( GuiInspectorTypeName,
   "@brief Inspector field type for Name\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeName)->setInspectorFieldType("GuiInspectorTypeName");
}

bool GuiInspectorTypeName::verifyData( StringTableEntry data )
{   
   return validateObjectName( data, mInspector->getInspectObject() );   
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeSFXParameterName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeSFXParameterName);

ConsoleDocClass( GuiInspectorTypeSFXParameterName,
   "@brief Inspector field type for SFXParameter\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeSFXParameterName::_populateMenu(GuiPopUpMenuCtrlEx *menu )
{
   SimSet* set = Sim::getSFXParameterGroup();
   for( SimSet::iterator iter = set->begin(); iter != set->end(); ++ iter )
   {
      SFXParameter* parameter = dynamic_cast< SFXParameter* >( *iter );
      if( parameter )
         menu->addEntry( parameter->getInternalName(), parameter->getId() );
   }
   
   menu->sort();
}

void GuiInspectorTypeSFXParameterName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TypeSFXParameterName )->setInspectorFieldType( "GuiInspectorTypeSFXParameterName" );
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeSFXStateName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeSFXStateName);

ConsoleDocClass( GuiInspectorTypeSFXStateName,
   "@brief Inspector field type for SFXState\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeSFXStateName::_populateMenu(GuiPopUpMenuCtrlEx *menu )
{
   menu->addEntry( "", 0 );

   SimSet* set = Sim::getSFXStateSet();
   for( SimSet::iterator iter = set->begin(); iter != set->end(); ++ iter )
   {
      SFXState* state = dynamic_cast< SFXState* >( *iter );
      if( state )
         menu->addEntry( state->getName(), state->getId() );
   }
   
   menu->sort();
}

void GuiInspectorTypeSFXStateName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TypeSFXStateName )->setInspectorFieldType( "GuiInspectorTypeSFXStateName" );
}


//-----------------------------------------------------------------------------
// GuiInspectorTypeSFXSourceName 
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeSFXSourceName);

ConsoleDocClass( GuiInspectorTypeSFXSourceName,
   "@brief Inspector field type for SFXSource\n\n"
   "Editor use only.\n\n"
   "@internal"
);

void GuiInspectorTypeSFXSourceName::_populateMenu(GuiPopUpMenuCtrlEx *menu )
{
   menu->addEntry( "", 0 );

   SimSet* set = Sim::getSFXSourceSet();
   for( SimSet::iterator iter = set->begin(); iter != set->end(); ++ iter )
   {
      SFXSource* source = dynamic_cast< SFXSource* >( *iter );
      if( source && source->getName() )
         menu->addEntry( source->getName(), source->getId() );
   }
   
   menu->sort();
}

void GuiInspectorTypeSFXSourceName::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType( TypeSFXSourceName )->setInspectorFieldType( "GuiInspectorTypeSFXSourceName" );
}

//-----------------------------------------------------------------------------
// Two Dimensional Field base GuiInspectorField Class
//-----------------------------------------------------------------------------

void GuiInspectorType2DValue::constructEditControlChildren(GuiControl* retCtrl, S32 width)
{
   mCtrlX = new GuiTextEditCtrl();
   _registerEditControl(mCtrlX, "x");
   mLabelX = new GuiControl();
   _registerEditControl(mLabelX, "lx");

   mCtrlY = new GuiTextEditCtrl();
   _registerEditControl(mCtrlY, "y");
   mLabelY = new GuiControl();
   _registerEditControl(mLabelY, "ly");

   mScriptValue = new GuiTextEditCtrl();

   mCopyButton = new GuiButtonCtrl();
   mCopyButton->setExtent(Point2I(45, 15));
   mCopyButton->registerObject();
   mCopyButton->setDataField(StringTable->insert("text"), NULL, "Copy");
   mCopyButton->setDataField(StringTable->insert("Profile"), NULL, "GuiInspectorButtonProfile");
   mCopyButton->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   mCopyButton->setDataField(StringTable->insert("hovertime"), NULL, "1000");
   mCopyButton->setDataField(StringTable->insert("tooltip"), NULL, "Copy all values for script.");

   mPasteButton = new GuiButtonCtrl();
   mPasteButton->setExtent(Point2I(45, 15));
   mPasteButton->registerObject();
   mPasteButton->setDataField(StringTable->insert("text"), NULL, "Paste");
   mPasteButton->setDataField(StringTable->insert("Profile"), NULL, "GuiInspectorButtonProfile");
   mPasteButton->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   mPasteButton->setDataField(StringTable->insert("hovertime"), NULL, "1000");
   mPasteButton->setDataField(StringTable->insert("tooltip"), NULL, "Copy all values for script.");

   mCtrlX->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mCtrlX->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");

   mCtrlY->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mCtrlY->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");

   mLabelX->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiXDimensionText");
   mLabelY->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiYDimensionText");

   mScriptValue->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mScriptValue->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");

   S32 labelWidth = 3;
   mLabelX->setExtent(Point2I(labelWidth, 18));
   mLabelY->setExtent(Point2I(labelWidth, 18));

   mCtrlX->setExtent(Point2I(width - labelWidth, 18));
   mCtrlY->setExtent(Point2I(width - labelWidth, 18));
   mScriptValue->setExtent(Point2I(width, 18));

   mCtrlX->setPosition(Point2I(labelWidth, 0));
   mCtrlY->setPosition(Point2I(labelWidth, 0));

   char szXCBuffer[512];
   dSprintf(szXCBuffer, 512, "%d.applyWord(0, %d.getText());", getId(), mCtrlX->getId());

   char szYCBuffer[512];
   dSprintf(szYCBuffer, 512, "%d.applyWord(1, %d.getText());", getId(), mCtrlY->getId());

   mCtrlX->setField("AltCommand", szXCBuffer);
   mCtrlY->setField("AltCommand", szYCBuffer);

   mCtrlX->setField("Validate", szXCBuffer);
   mCtrlY->setField("Validate", szYCBuffer);

   mContainerX = new GuiControl();
   mContainerX->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mContainerX->setExtent(Point2I(width, 18));
   mContainerX->addObject(mLabelX);
   mContainerX->addObject(mCtrlX);
   _registerEditControl(mContainerX, "cx");

   mContainerY = new GuiControl();
   mContainerY->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mContainerY->setExtent(Point2I(width, 18));
   mContainerY->addObject(mLabelY);
   mContainerY->addObject(mCtrlY);
   _registerEditControl(mContainerY, "cy");

   retCtrl->addObject(mContainerX);
   retCtrl->addObject(mContainerY);
   //retCtrl->addObject(mScriptValue);
}

void GuiInspectorType2DValue::updateValue()
{
   if (mField)
   {
      Parent::updateValue();
      const char* data = getData();
      if (!data)
         data = "";
      U32 elementCount = StringUnit::getUnitCount(data, " ");

      if (elementCount > 0)
      {
         F32 value = dAtof(StringUnit::getUnit(data, 0, " \t\n"));
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%.4f", value);
         mCtrlX->setText(szBuffer);
      }

      if (elementCount > 1)
      {
         F32 value = dAtof(StringUnit::getUnit(data, 1, " \t\n"));
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%.4f", value);
         mCtrlY->setText(szBuffer);
      }

      mScriptValue->setText(data);

      mEdit->setDataField(StringTable->insert("tooltip"), NULL, data);
   }
}

bool GuiInspectorType2DValue::resize(const Point2I& newPosition, const Point2I& newExtent)
{
   if (!Parent::resize(newPosition, newExtent))
      return false;

   if (mEdit != NULL)
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorType2DValue::updateRects()
{
   S32 rowSize = 18;
   S32 dividerPos, dividerMargin;
   mInspector->getDivider(dividerPos, dividerMargin);
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mEditCtrlRect.set(fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 29, fieldExtent.y);
   S32 cellWidth = mCeil((dividerPos - dividerMargin - 29));

   mCtrlX->setExtent(Point2I(cellWidth - 3, 18));
   mCtrlY->setExtent(Point2I(cellWidth - 3, 18));

   S32 dimX = 10;

   mCaptionLabel->resize(Point2I(mProfile->mTextOffset.x, 0), Point2I(fieldExtent.x, rowSize));
   mDimensionLabelX->resize(Point2I(fieldExtent.x - dividerPos - dimX, 0), Point2I(dimX, rowSize));
   mDimensionLabelY->resize(Point2I(fieldExtent.x - dividerPos - dimX, rowSize + 3), Point2I(dimX, rowSize));

   mCopyButton->resize(Point2I(mProfile->mTextOffset.x, rowSize + 3), Point2I(45, 15));
   mPasteButton->resize(Point2I(mProfile->mTextOffset.x, rowSize + rowSize + 6), Point2I(45, 15));

   mEdit->resize(mEditCtrlRect.point, mEditCtrlRect.extent);

   return true;
}

//-----------------------------------------------------------------------------
// Three Dimensional Field base GuiInspectorField Class
//-----------------------------------------------------------------------------

void GuiInspectorType3DValue::constructEditControlChildren(GuiControl* retCtrl, S32 width)
{
   Parent::constructEditControlChildren(retCtrl, width);

   mCtrlZ = new GuiTextEditCtrl();
   _registerEditControl(mCtrlZ, "z");
   mLabelZ = new GuiControl();
   _registerEditControl(mLabelZ, "lz");

   mCtrlZ->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mCtrlZ->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");

   mLabelZ->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiZDimensionText");

   S32 labelWidth = 3;
   mLabelZ->setExtent(Point2I(labelWidth, 18));

   mCtrlZ->setExtent(Point2I(width - labelWidth, 18));

   mCtrlZ->setPosition(Point2I(labelWidth, 0));

   char szXCBuffer[512];
   dSprintf(szXCBuffer, 512, "%d.applyWord(0, %d.getText());", getId(), mCtrlX->getId());

   char szYCBuffer[512];
   dSprintf(szYCBuffer, 512, "%d.applyWord(1, %d.getText());", getId(), mCtrlY->getId());

   char szZCBuffer[512];
   dSprintf(szZCBuffer, 512, "%d.applyWord(2, %d.getText());", getId(), mCtrlZ->getId());

   mCtrlX->setField("AltCommand", szXCBuffer);
   mCtrlY->setField("AltCommand", szYCBuffer);
   mCtrlZ->setField("AltCommand", szZCBuffer);

   mCtrlX->setField("Validate", szXCBuffer);
   mCtrlY->setField("Validate", szYCBuffer);
   mCtrlZ->setField("Validate", szZCBuffer);

   mContainerZ = new GuiControl();
   mContainerZ->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mContainerZ->setExtent(Point2I(width, 18));
   mContainerZ->addObject(mLabelZ);
   mContainerZ->addObject(mCtrlZ);
   _registerEditControl(mContainerZ, "cz");

   retCtrl->addObject(mContainerZ);
}

void GuiInspectorType3DValue::updateValue()
{
   if (mField)
   {
      Parent::updateValue();
      const char* data = getData();
      if (!data)
         data = "";

      U32 elementCount = StringUnit::getUnitCount(data, " ");

      if (elementCount > 2)
      {
         F32 value = dAtof(StringUnit::getUnit(data, 2, " \t\n"));
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%.4f", value);
         mCtrlZ->setText(szBuffer);
      }
   }
}

bool GuiInspectorType3DValue::resize(const Point2I& newPosition, const Point2I& newExtent)
{
   if (!Parent::resize(newPosition, newExtent))
      return false;

   if (mEdit != NULL)
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorType3DValue::updateRects()
{
   if (!Parent::updateRects())
      return false;

   S32 rowSize = 18;
   S32 dividerPos, dividerMargin;
   mInspector->getDivider(dividerPos, dividerMargin);
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   S32 cellWidth = mCeil((dividerPos - dividerMargin - 29));

   mCtrlZ->setExtent(Point2I(cellWidth - 3, 18));

   S32 dimX = 10;

   mDimensionLabelZ->resize(Point2I(fieldExtent.x - dividerPos - dimX, rowSize + rowSize + 6), Point2I(dimX, rowSize));

   return true;
}

//-----------------------------------------------------------------------------
// Four Dimensional Field base GuiInspectorField Class
//-----------------------------------------------------------------------------

void GuiInspectorType4DValue::constructEditControlChildren(GuiControl* retCtrl, S32 width)
{
   Parent::constructEditControlChildren(retCtrl, width);

   mCtrlW = new GuiTextEditCtrl();
   GuiControl* mLabelW = new GuiControl();

   _registerEditControl(mCtrlW);

   mCtrlW->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mCtrlW->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");

   mLabelW->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiZDimensionText");

   S32 labelWidth = 3;
   mLabelW->setExtent(Point2I(labelWidth, 18));

   mCtrlW->setExtent(Point2I(width - labelWidth, 18));

   mScriptValue->setExtent(Point2I(width, 18));

   mCtrlW->setPosition(Point2I(labelWidth, 0));

   char szXCBuffer[512];
   dSprintf(szXCBuffer, 512, "%d.applyWord(0, %d.getText());", getId(), mCtrlX->getId());

   char szYCBuffer[512];
   dSprintf(szYCBuffer, 512, "%d.applyWord(1, %d.getText());", getId(), mCtrlY->getId());

   char szZCBuffer[512];
   dSprintf(szZCBuffer, 512, "%d.applyWord(2, %d.getText());", getId(), mCtrlZ->getId());

   char szWCBuffer[512];
   dSprintf(szZCBuffer, 512, "%d.applyWord(3, %d.getText());", getId(), mCtrlW->getId());

   mCtrlX->setField("AltCommand", szXCBuffer);
   mCtrlY->setField("AltCommand", szYCBuffer);
   mCtrlZ->setField("AltCommand", szZCBuffer);
   mCtrlW->setField("AltCommand", szWCBuffer);

   mCtrlX->setField("Validate", szXCBuffer);
   mCtrlY->setField("Validate", szYCBuffer);
   mCtrlZ->setField("Validate", szZCBuffer);
   mCtrlW->setField("Validate", szWCBuffer);

   GuiControl* mContainerW = new GuiControl();
   mContainerW->setDataField(StringTable->insert("profile"), NULL, "GuiInspectorTextEditProfile");
   mContainerW->setExtent(Point2I(width, 18));
   mContainerW->addObject(mLabelW);
   mContainerW->addObject(mCtrlW);
   _registerEditControl(mContainerW);

   retCtrl->addObject(mContainerW);
}

void GuiInspectorType4DValue::updateValue()
{
   if (mField)
   {
      Parent::updateValue();
      const char* data = getData();
      if (!data)
         data = "";
      U32 elementCount = StringUnit::getUnitCount(data, " ");

      if (elementCount > 3)
      {
         mCtrlW->setText(StringUnit::getUnit(data, 3, " \t\n"));
      }
   }
}

bool GuiInspectorType4DValue::resize(const Point2I& newPosition, const Point2I& newExtent)
{
   if (!Parent::resize(newPosition, newExtent))
      return false;

   if (mEdit != NULL)
   {
      return updateRects();
   }

   return false;
}

bool GuiInspectorType4DValue::updateRects()
{
   if (!Parent::updateRects())
      return false;

   S32 rowSize = 18;
   S32 dividerPos, dividerMargin;
   mInspector->getDivider(dividerPos, dividerMargin);
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   S32 cellWidth = mCeil((dividerPos - dividerMargin - 29));

   mCtrlW->setExtent(Point2I(cellWidth - 3, 18));

   S32 dimX = 10;

   mDimensionLabelW->resize(Point2I(fieldExtent.x - dividerPos - dimX, rowSize + rowSize + 6), Point2I(dimX, rowSize));

   return true;
}

//-----------------------------------------------------------------------------
// TypePoint2F GuiInspectorField Class
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypePoint2F);

ConsoleDocClass(GuiInspectorTypePoint2F,
   "@brief Inspector field type for Point2F\n\n"
   "Editor use only.\n\n"
   "@internal"
);
void GuiInspectorTypePoint2F::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypePoint2F)->setInspectorFieldType("GuiInspectorTypePoint2F");
}

GuiControl* GuiInspectorTypePoint2F::constructEditControl()
{
   GuiStackControl* retCtrl = new GuiStackControl();

   if (retCtrl == NULL)
      return retCtrl;

   mCaptionLabel = new GuiTextCtrl();
   mCaptionLabel->registerObject();
   mCaptionLabel->setControlProfile(mProfile);
   mCaptionLabel->setText(mCaption);
   addObject(mCaptionLabel);

   mDimensionLabelX = new GuiTextCtrl();
   mDimensionLabelX->registerObject();
   mDimensionLabelX->setControlProfile(mProfile);
   mDimensionLabelX->setText("X");
   addObject(mDimensionLabelX);

   mDimensionLabelY = new GuiTextCtrl();
   mDimensionLabelY->registerObject();
   mDimensionLabelY->setControlProfile(mProfile);
   mDimensionLabelY->setText("Y");
   addObject(mDimensionLabelY);

   retCtrl->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiDefaultProfile");
   retCtrl->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   retCtrl->setDataField(StringTable->insert("stackingType"), NULL, "Vertical");
   retCtrl->setDataField(StringTable->insert("dynamicSize"), NULL, "1");
   retCtrl->setDataField(StringTable->insert("padding"), NULL, "3");

   _registerEditControl(retCtrl);

   constructEditControlChildren(retCtrl, getWidth());

   char szBuffer[512];
   dSprintf(szBuffer, 512, "setClipboard(%d.getText() SPC %d.getText());", mCtrlX->getId(), mCtrlY->getId());
   mCopyButton->setField("Command", szBuffer);
   addObject(mCopyButton);

   dSprintf(szBuffer, 512, "%d.apply(getWords(getClipboard(), 0, 1));", getId());
   mPasteButton->setField("Command", szBuffer);
   addObject(mPasteButton);

   mUseHeightOverride = true;
   mHeightOverride = retCtrl->getHeight() + 16 + 6;

   return retCtrl;
}

//-----------------------------------------------------------------------------
// TypePoint2I GuiInspectorField Class
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypePoint2I);

ConsoleDocClass(GuiInspectorTypePoint2I,
   "@brief Inspector field type for Point2I\n\n"
   "Editor use only.\n\n"
   "@internal"
);
void GuiInspectorTypePoint2I::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypePoint2I)->setInspectorFieldType("GuiInspectorTypePoint2I");
}

GuiControl* GuiInspectorTypePoint2I::constructEditControl()
{
   GuiControl* retCtrl = Parent::constructEditControl();

   mCtrlX->setDataField(StringTable->insert("format"), NULL, "%d");
   mCtrlY->setDataField(StringTable->insert("format"), NULL, "%d");

   return retCtrl;
}

//-----------------------------------------------------------------------------
// TypePoint3F GuiInspectorField Class
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypePoint3F);

ConsoleDocClass(GuiInspectorTypePoint3F,
   "@brief Inspector field type for Point3F\n\n"
   "Editor use only.\n\n"
   "@internal"
);
void GuiInspectorTypePoint3F::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeMatrixPosition)->setInspectorFieldType("GuiInspectorTypePoint3F");
   ConsoleBaseType::getType(TypePoint3F)->setInspectorFieldType("GuiInspectorTypePoint3F");
}

GuiControl* GuiInspectorTypePoint3F::constructEditControl()
{
   GuiStackControl* retCtrl = new GuiStackControl();

   if (retCtrl == NULL)
      return retCtrl;

   mCaptionLabel = new GuiTextCtrl();
   mCaptionLabel->registerObject();
   mCaptionLabel->setControlProfile(mProfile);
   mCaptionLabel->setText(mCaption);
   addObject(mCaptionLabel);

   mDimensionLabelX = new GuiTextCtrl();
   mDimensionLabelX->registerObject();
   mDimensionLabelX->setControlProfile(mProfile);
   mDimensionLabelX->setText("X");
   addObject(mDimensionLabelX);

   mDimensionLabelY = new GuiTextCtrl();
   mDimensionLabelY->registerObject();
   mDimensionLabelY->setControlProfile(mProfile);
   mDimensionLabelY->setText("Y");
   addObject(mDimensionLabelY);

   mDimensionLabelZ = new GuiTextCtrl();
   mDimensionLabelZ->registerObject();
   mDimensionLabelZ->setControlProfile(mProfile);
   mDimensionLabelZ->setText("Z");
   addObject(mDimensionLabelZ);

   retCtrl->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiDefaultProfile");
   retCtrl->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   retCtrl->setDataField(StringTable->insert("stackingType"), NULL, "Vertical");
   retCtrl->setDataField(StringTable->insert("dynamicSize"), NULL, "1");
   retCtrl->setDataField(StringTable->insert("padding"), NULL, "3");

   _registerEditControl(retCtrl);

   constructEditControlChildren(retCtrl, getWidth());

   char szBuffer[512];
   dSprintf(szBuffer, 512, "setClipboard(%d.getText() SPC %d.getText() SPC %d.getText());", mCtrlX->getId(), mCtrlY->getId(), mCtrlZ->getId());
   mCopyButton->setField("Command", szBuffer);
   addObject(mCopyButton);

   dSprintf(szBuffer, 512, "%d.apply(getWords(getClipboard(), 0, 2));", getId());
   mPasteButton->setField("Command", szBuffer);
   addObject(mPasteButton);

   mUseHeightOverride = true;
   mHeightOverride = retCtrl->getHeight() + 6;

   return retCtrl;
}

//-----------------------------------------------------------------------------
// GuiInspectorTypeMatrixRotation GuiInspectorField Class
//-----------------------------------------------------------------------------
IMPLEMENT_CONOBJECT(GuiInspectorTypeMatrixRotation);

ConsoleDocClass(GuiInspectorTypeMatrixRotation,
   "@brief Inspector field type for rotation\n\n"
   "Editor use only.\n\n"
   "@internal"
);
void GuiInspectorTypeMatrixRotation::consoleInit()
{
   Parent::consoleInit();

   ConsoleBaseType::getType(TypeMatrixRotation)->setInspectorFieldType("GuiInspectorTypeMatrixRotation");
}

GuiControl* GuiInspectorTypeMatrixRotation::constructEditControl()
{
   GuiStackControl* retCtrl = new GuiStackControl();

   if (retCtrl == NULL)
      return retCtrl;

   mCaptionLabel = new GuiTextCtrl();
   mCaptionLabel->registerObject();
   mCaptionLabel->setControlProfile(mProfile);
   mCaptionLabel->setText(mCaption);
   addObject(mCaptionLabel);

   mDimensionLabelX = new GuiTextCtrl();
   mDimensionLabelX->registerObject();
   mDimensionLabelX->setControlProfile(mProfile);
   mDimensionLabelX->setText("Pitch");
   addObject(mDimensionLabelX);

   mDimensionLabelY = new GuiTextCtrl();
   mDimensionLabelY->registerObject();
   mDimensionLabelY->setControlProfile(mProfile);
   mDimensionLabelY->setText("Roll");
   addObject(mDimensionLabelY);

   mDimensionLabelZ = new GuiTextCtrl();
   mDimensionLabelZ->registerObject();
   mDimensionLabelZ->setControlProfile(mProfile);
   mDimensionLabelZ->setText("Yaw");
   addObject(mDimensionLabelZ);

   retCtrl->setDataField(StringTable->insert("profile"), NULL, "ToolsGuiDefaultProfile");
   retCtrl->setDataField(StringTable->insert("tooltipprofile"), NULL, "GuiToolTipProfile");
   retCtrl->setDataField(StringTable->insert("stackingType"), NULL, "Vertical");
   retCtrl->setDataField(StringTable->insert("dynamicSize"), NULL, "1");
   retCtrl->setDataField(StringTable->insert("padding"), NULL, "3");

   _registerEditControl(retCtrl);

   constructEditControlChildren(retCtrl, getWidth());

   //retCtrl->addObject(mScriptValue);

   char szBuffer[512];
   dSprintf(szBuffer, 512, "setClipboard(%d.getText());", mScriptValue->getId());
   mCopyButton->setField("Command", szBuffer);
   addObject(mCopyButton);

   dSprintf(szBuffer, 512, "%d.apply(getClipboard());", getId());
   mPasteButton->setField("Command", szBuffer);
   addObject(mPasteButton);

   mUseHeightOverride = true;
   mHeightOverride = retCtrl->getHeight() + 6;

   return retCtrl;
}

void GuiInspectorTypeMatrixRotation::constructEditControlChildren(GuiControl* retCtrl, S32 width)
{
   Parent::constructEditControlChildren(retCtrl, width);

   // Don't forget to register ourselves
   _registerEditControl(mScriptValue, "value");
   retCtrl->addObject(mScriptValue);

   // enable script value
   String angleInput = String::ToString("%d.apply(%d.getText());", getId(), mScriptValue->getId());
   mScriptValue->setField("AltCommand", angleInput.c_str());
   mScriptValue->setField("Validate", angleInput.c_str());

   // change command for pitch roll yaw input.
   angleInput = String::ToString("%d.applyRotation(mEulDegToAng(%d.getText() SPC %d.getText() SPC %d.getText()));", getId(), mCtrlX->getId(), mCtrlY->getId(), mCtrlZ->getId());
   

   mCtrlX->setField("AltCommand", angleInput.c_str());
   mCtrlX->setField("Validate", angleInput.c_str());
   mCtrlX->setDataField(StringTable->insert("format"), NULL, "%g");

   mCtrlY->setField("AltCommand", angleInput.c_str());
   mCtrlY->setField("Validate", angleInput.c_str());
   mCtrlY->setDataField(StringTable->insert("format"), NULL, "%g");

   mCtrlZ->setField("AltCommand", angleInput.c_str());
   mCtrlZ->setField("Validate", angleInput.c_str());
   mCtrlZ->setDataField(StringTable->insert("format"), NULL, "%g");
}

void GuiInspectorTypeMatrixRotation::updateValue()
{
   if (mField)
   {
      Update::updateValue();
      const char* data = getData();

      angAx.set(Point3F(dAtof(StringUnit::getUnit(data, 0, " \t\n")),
         dAtof(StringUnit::getUnit(data, 1, " \t\n")),
         dAtof(StringUnit::getUnit(data, 2, " \t\n"))),
         mDegToRad(dAtof(StringUnit::getUnit(data, 3, " \t\n"))));

      eulAng = mAngToEul(angAx);

      U32 elementCount = StringUnit::getUnitCount(data, " ");

      if (elementCount > 0)
      {
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%g", eulAng.x);
         mCtrlX->setText(szBuffer);
      }
      if (elementCount > 1)
      {
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%g", eulAng.y);
         mCtrlY->setText(szBuffer);
      }
      if (elementCount > 2)
      {
         char szBuffer[64];
         dSprintf(szBuffer, 64, "%g", eulAng.z);
         mCtrlZ->setText(szBuffer);
      }

      mScriptValue->setText(data);

      mEdit->setDataField(StringTable->insert("tooltip"), NULL, data);
   }

}

bool GuiInspectorTypeMatrixRotation::resize(const Point2I& newPosition, const Point2I& newExtent)
{
   if (!Parent::resize(newPosition, newExtent))
      return false;

   if (mEdit != NULL)
   {
      return updateRects();
   }
   return false;
}

bool GuiInspectorTypeMatrixRotation::updateRects()
{
   S32 rowSize = 18;
   S32 dividerPos, dividerMargin;
   mInspector->getDivider(dividerPos, dividerMargin);
   Point2I fieldExtent = getExtent();
   Point2I fieldPos = getPosition();

   mEditCtrlRect.set(fieldExtent.x - dividerPos + dividerMargin, 1, dividerPos - dividerMargin - 29, fieldExtent.y);
   S32 cellWidth = mCeil((dividerPos - dividerMargin - 29));

   mCtrlX->setExtent(Point2I(cellWidth - 3, 18));
   mCtrlY->setExtent(Point2I(cellWidth - 3, 18));
   mCtrlZ->setExtent(Point2I(cellWidth - 3, 18));

   mCaptionLabel->resize(Point2I(mProfile->mTextOffset.x, 0), Point2I(fieldExtent.x, rowSize));
   mDimensionLabelX->resize(Point2I(fieldExtent.x - dividerPos - 30, 0), Point2I(30, rowSize));
   mDimensionLabelY->resize(Point2I(fieldExtent.x - dividerPos - 30, rowSize + 3), Point2I(50, rowSize));
   mDimensionLabelZ->resize(Point2I(fieldExtent.x - dividerPos - 30, rowSize + rowSize + 6), Point2I(40, rowSize));

   mEdit->resize(mEditCtrlRect.point, mEditCtrlRect.extent);

   mCopyButton->resize(Point2I(mProfile->mTextOffset.x, rowSize + 3), Point2I(45, 15));
   mPasteButton->resize(Point2I(mProfile->mTextOffset.x, rowSize + rowSize + 6), Point2I(45, 15));

   return true;
}

void GuiInspectorTypeMatrixRotation::updateAng(AngAxisF newAngAx)
{
   angAx.axis = newAngAx.axis;
   angAx.angle = mRadToDeg(newAngAx.angle);
}

void GuiInspectorTypeMatrixRotation::updateEul(EulerF newEul)
{
   eulAng = newEul;
}

void GuiInspectorTypeMatrixRotation::updateData()
{
   StringTableEntry data = getValue();
   setData(data);
}

StringTableEntry GuiInspectorTypeMatrixRotation::getValue()
{
   String angBuffer = String::ToString("%g %g %g %g", angAx.axis.x, angAx.axis.y, angAx.axis.z, angAx.angle);
   return StringTable->insert(angBuffer.c_str());
}

DefineEngineMethod(GuiInspectorTypeMatrixRotation, applyRotation, void, (AngAxisF angAx), , "")
{
   object->updateAng(angAx);
   object->updateEul(mAngToEul(angAx));
   object->updateData();
}

