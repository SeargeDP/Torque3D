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
#include "environment/editors/guiMeshRoadEditorCtrl.h"

#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "environment/meshRoad.h"
#include "renderInstance/renderPassManager.h"
#include "collision/collision.h"
#include "math/util/frustum.h"
#include "math/mathUtils.h"
#include "gfx/gfxPrimitiveBuffer.h"
#include "gfx/gfxTextureHandle.h"
#include "gfx/gfxTransformSaver.h"
#include "gfx/primBuilder.h"
#include "gfx/gfxDrawUtil.h"
#include "scene/sceneRenderState.h"
#include "scene/sceneManager.h"
#include "gui/core/guiCanvas.h"
#include "gui/buttons/guiButtonCtrl.h"
#include "gui/worldEditor/undoActions.h"
#include "T3D/gameBase/gameConnection.h"
#include "gfx/sim/debugDraw.h"
#include "materials/materialDefinition.h"
#include "T3D/prefab.h"

#include "T3D/Scene.h"

IMPLEMENT_CONOBJECT(GuiMeshRoadEditorCtrl);

ConsoleDocClass( GuiMeshRoadEditorCtrl,
   "@brief GUI tool that makes up the Mesh Road Editor\n\n"
   "Editor use only.\n\n"
   "@internal"
);

S32 _NodeIndexCmp( U32 const *a, U32 const *b )
{
   S32 a2 = (*a);
   S32 b2 = (*b);
   S32 diff = a2 - b2;
   return diff < 0 ? 1 : diff > 0 ? -1 : 0;
}

GuiMeshRoadEditorCtrl::GuiMeshRoadEditorCtrl()
 : 
	// Each of the mode names directly correlates with the Mesh Road Editor's
	// tool palette
	mSelectMeshRoadMode("MeshRoadEditorSelectMode"),
	mAddMeshRoadMode("MeshRoadEditorAddRoadMode"),
	mAddNodeMode("MeshRoadEditorAddNodeMode"),
	mInsertPointMode("MeshRoadEditorInsertPointMode"),
	mRemovePointMode("MeshRoadEditorRemovePointMode"),
	mMovePointMode("MeshRoadEditorMoveMode"),
    mScalePointMode("MeshRoadEditorScaleMode"),
	mRotatePointMode("MeshRoadEditorRotateMode"),
    mSavedDrag(false),
    mIsDirty( false ),
    mSavedProfileDrag( false ),
    mDeselectProfileNode( false ),
    mProfileNode( -1 ),
    mProfileColor( 255,255,0 ),
    mRoadSet( NULL ),
    mSelNode( -1 ),
    mHoverNode( -1 ),
    mAddNodeIdx( 0 ),
    mSelRoad( NULL ),
    mHoverRoad( NULL ),
    mMode(mSelectMeshRoadMode),
    mDefaultWidth( 10.0f ),
    mDefaultDepth( 5.0f ),
    mDefaultNormal( 0,0,1 ),
    mNodeHalfSize( 4,4 ),
    mHoverSplineColor( 255,0,0,255 ),
    mSelectedSplineColor( 0,255,0,255 ),
    mHoverNodeColor( 255,255,255,255 ),
	 mHasCopied( false )
{
   INIT_ASSET(TopMaterial);
   INIT_ASSET(BottomMaterial);
   INIT_ASSET(SideMaterial);

   mTopMaterialAssetId = Con::getVariable("$MeshRoadEditor::defaultTopMaterialAsset");
   mBottomMaterialAssetId = Con::getVariable("$MeshRoadEditor::defaultBottomMaterialAsset");
   mSideMaterialAssetId = Con::getVariable("$MeshRoadEditor::defaultSideMaterialAsset");
}

GuiMeshRoadEditorCtrl::~GuiMeshRoadEditorCtrl()
{
   // nothing to do
}

void GuiMeshRoadEditorUndoAction::undo()
{
   MeshRoad *object = NULL;
   if ( !Sim::findObject( mObjId, object ) )
      return;

   // Temporarily save the Roads current data.
   //F32 metersPerSeg = object->mMetersPerSegment;
   Vector<MeshRoadNode> nodes;   
   nodes.merge( object->mNodes );

   // Restore the River properties saved in the UndoAction
   //object->mMetersPerSegment = mMetersPerSegment;

   // Restore the Nodes saved in the UndoAction
   object->mNodes.clear();
   for ( U32 i = 0; i < mNodes.size(); i++ )
   {
      object->_addNode( mNodes[i].point, mNodes[i].width, mNodes[i].depth, mNodes[i].normal );      
   }

   // Temporarily save the Roads current profile data.
   Vector<MeshRoadProfileNode> profNodes;
   Vector<U8> profMtrls;
   profNodes.merge( object->mSideProfile.mNodes );
   profMtrls.merge( object->mSideProfile.mSegMtrls );

   // Restore the Profile Nodes saved in the UndoAction
   Point3F pos;
   object->mSideProfile.mNodes.clear();
   object->mSideProfile.mSegMtrls.clear();
   for ( U32 i = 0; i < mProfileNodes.size(); i++ )
   {
      MeshRoadProfileNode newNode;

      pos = mProfileNodes[i].getPosition();
      newNode.setSmoothing( mProfileNodes[i].isSmooth() );

      object->mSideProfile.mNodes.push_back( newNode );
      object->mSideProfile.mNodes.last().setPosition( pos.x, pos.y );

      if(i)
         object->mSideProfile.mSegMtrls.push_back(mProfileMtrls[i-1]);
   }

   // Set the first node position to trigger packet update to client
   pos.set(0.0f, 0.0f, 0.0f);
   object->mSideProfile.setNodePosition(0,pos);

   // Regenerate the Road
   object->mSideProfile.generateNormals();

   // If applicable set the selected Road and node
   mEditor->mProfileNode = -1;

   // Now save the previous Road data in this UndoAction
   // since an undo action must become a redo action and vice-versa
   //mMetersPerSegment = metersPerSeg;
   mProfileNodes.clear();
   mProfileNodes.merge( profNodes );
   mProfileMtrls.clear();
   mProfileMtrls.merge( profMtrls );

   // Regenerate the Road
   object->regenerate();

   // If applicable set the selected Road and node
   mEditor->mSelRoad = object;
   mEditor->mSelNode = -1;

   // Now save the previous Road data in this UndoAction
   // since an undo action must become a redo action and vice-versa
   //mMetersPerSegment = metersPerSeg;
   mNodes.clear();
   mNodes.merge( nodes );
}

bool GuiMeshRoadEditorCtrl::onAdd()
{
   if( !Parent::onAdd() )
      return false;

   mRoadSet = MeshRoad::getServerSet();   

   GFXStateBlockDesc desc;
   desc.fillMode = GFXFillSolid;      
   desc.blendDefined = true;
   desc.blendEnable = false;      
   desc.zDefined = true;
   desc.zEnable = false;
   desc.cullDefined = true;
   desc.cullMode = GFXCullNone;

   mZDisableSB = GFX->createStateBlock(desc);

   desc.zEnable = true;
   mZEnableSB = GFX->createStateBlock(desc);

   return true;
}

void GuiMeshRoadEditorCtrl::initPersistFields()
{
   docsURL;
   addFieldV( "DefaultWidth",        TypeRangedF32,    Offset( mDefaultWidth, GuiMeshRoadEditorCtrl ), &CommonValidators::PositiveFloat);
	addFieldV( "DefaultDepth", TypeRangedF32,    Offset( mDefaultDepth, GuiMeshRoadEditorCtrl ), &CommonValidators::PositiveFloat);
	addField( "DefaultNormal",       TypePoint3F,Offset( mDefaultNormal, GuiMeshRoadEditorCtrl ) );
   addField( "HoverSplineColor",    TypeColorI, Offset( mHoverSplineColor, GuiMeshRoadEditorCtrl ) );
   addField( "SelectedSplineColor", TypeColorI, Offset( mSelectedSplineColor, GuiMeshRoadEditorCtrl ) );
   addField( "HoverNodeColor",      TypeColorI, Offset( mHoverNodeColor, GuiMeshRoadEditorCtrl ) );
   addField( "isDirty",             TypeBool,   Offset( mIsDirty, GuiMeshRoadEditorCtrl ) );

   INITPERSISTFIELD_MATERIALASSET(TopMaterial, GuiMeshRoadEditorCtrl, "Default Material used by the Mesh Road Editor on upper surface road creation.");
   INITPERSISTFIELD_MATERIALASSET(BottomMaterial, GuiMeshRoadEditorCtrl, "Default Material used by the Mesh Road Editor on bottom surface road creation.");
   INITPERSISTFIELD_MATERIALASSET(SideMaterial, GuiMeshRoadEditorCtrl, "Default Material used by the Mesh Road Editor on side surface road creation.");

   //addField( "MoveNodeCursor", TYPEID< SimObject >(), Offset( mMoveNodeCursor, GuiMeshRoadEditorCtrl) );
   //addField( "AddNodeCursor", TYPEID< SimObject >(), Offset( mAddNodeCursor, GuiMeshRoadEditorCtrl) );
   //addField( "InsertNodeCursor", TYPEID< SimObject >(), Offset( mInsertNodeCursor, GuiMeshRoadEditorCtrl) );
   //addField( "ResizeNodeCursor", TYPEID< SimObject >(), Offset( mResizeNodeCursor, GuiMeshRoadEditorCtrl) );

   Parent::initPersistFields();
}

void GuiMeshRoadEditorCtrl::onSleep()
{
   Parent::onSleep();

   mMode = mSelectMeshRoadMode;  
   mHoverNode = -1;
   mHoverRoad = NULL;
   setSelectedNode(-1);   
}

void GuiMeshRoadEditorCtrl::get3DCursor( GuiCursor *&cursor, 
                                       bool &visible, 
                                       const Gui3DMouseEvent &event_ )
{
   //cursor = mAddNodeCursor;
   //visible = false;
   
   cursor = NULL;
   visible = false;

   GuiCanvas *root = getRoot();
   if ( !root )
      return;

   S32 currCursor = PlatformCursorController::curArrow;

   if ( root->mCursorChanged == currCursor )
      return;

   PlatformWindow *window = root->getPlatformWindow();
   PlatformCursorController *controller = window->getCursorController();
   
   // We've already changed the cursor, 
   // so set it back before we change it again.
   if( root->mCursorChanged != -1)
      controller->popCursor();

   // Now change the cursor shape
   controller->pushCursor(currCursor);
   root->mCursorChanged = currCursor;   
}

void GuiMeshRoadEditorCtrl::on3DMouseDown(const Gui3DMouseEvent & event)
{
   mHasCopied = false;

   mGizmo->on3DMouseDown( event );

   if ( !isFirstResponder() )
      setFirstResponder();
	
   if( MeshRoad::smShowRoadProfile && mSelRoad )
   {
      // Ctrl-Click = Add Node
      if(event.modifier & SI_CTRL)
      {
         S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );

         if(clickedNode != -1)
         {
            // If clicked node is already in list, remove it, else add it to list
            if(!mSelProfNodeList.remove(clickedNode) && clickedNode > 0)
               mSelProfNodeList.push_back(clickedNode);

            return;
         }

         Point3F pos;

         PlaneF xy( mSelRoad->mSlices[0].p2, -mSelRoad->mSlices[0].fvec );

         xy.intersect(event.pos, event.vec, &pos);

         mSelRoad->mSideProfile.worldToObj(pos);

         U32 node = mSelRoad->mSideProfile.clickOnLine(pos);

         if(node != -1)
         {
            submitUndo( "Add Profile Node" );
            mSelRoad->mSideProfile.addPoint(node, pos);
            mProfileNode = node;
            mSelProfNodeList.clear();
            mSelProfNodeList.push_back(node);
            mIsDirty = true;
         }

         return;
      }

      // Alt-Click = Delete Node
      if(event.modifier & SI_ALT)
      {
         S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );


         if(mSelProfNodeList.find_next(clickedNode) != -1)
         {
            submitUndo( "Delete Profile Node" );

            mSelProfNodeList.sort( _NodeIndexCmp );
            for(U32 i=0; i < mSelProfNodeList.size(); i++)
               mSelRoad->mSideProfile.removePoint( mSelProfNodeList[i] );

            mProfileNode = -1;
            mSelProfNodeList.clear();
            mIsDirty = true;
         }
         else if(clickedNode > 0 && clickedNode < mSelRoad->mSideProfile.mNodes.size()-1)
         {
            submitUndo( "Delete Profile Node" );
            mSelRoad->mSideProfile.removePoint( clickedNode );
            mProfileNode = -1;
            mSelProfNodeList.clear();
            mIsDirty = true;
         }

         return;
      }

      // Shift-Click = Toggle Node Smoothing
      if(event.modifier & SI_SHIFT)
      {
         S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );

         if(clickedNode != -1)
         {
            submitUndo( "Smooth Profile Node" );

            if(mSelProfNodeList.find_next(clickedNode) != -1)
            {
               for(U32 i=0; i < mSelProfNodeList.size(); i++)
                  mSelRoad->mSideProfile.toggleSmoothing(mSelProfNodeList[i]);
            }
            else
            {
               mSelRoad->mSideProfile.toggleSmoothing(clickedNode);

               if(clickedNode != 0)
               {
                  mProfileNode = clickedNode;
                  mSelProfNodeList.clear();
                  mSelProfNodeList.push_back(clickedNode);
               }
            }

            mIsDirty = true;
            return;
         }

         Point3F pos;
         PlaneF xy( mSelRoad->mSlices[0].p2, -mSelRoad->mSlices[0].fvec );
         xy.intersect(event.pos, event.vec, &pos);
         mSelRoad->mSideProfile.worldToObj(pos);
         U32 node = mSelRoad->mSideProfile.clickOnLine(pos);

         if(node > 0)
         {
            submitUndo( "Profile Material" );
            mSelRoad->mSideProfile.toggleSegMtrl(node-1);
            mIsDirty = true;
         }

         return;
      }

      // Click to select/deselect nodes
      S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );

      if(clickedNode != -1)
      {
         if(mSelProfNodeList.find_next(clickedNode) != -1)
         {
            mProfileNode = clickedNode;
            mDeselectProfileNode = true;
         }
         else if(clickedNode != 0)
         {
            mProfileNode = clickedNode;
            mSelProfNodeList.clear();
            mSelProfNodeList.push_back(clickedNode);
         }
         else
         {
            mProfileNode = -1;
            mSelProfNodeList.clear();

            // Reset profile if Node 0 is double-clicked
            if( event.mouseClickCount > 1 )
            {
               submitUndo( "Reset Profile" );
               mSelRoad->mSideProfile.resetProfile(mSelRoad->mSlices[0].depth);
               mSelRoad->regenerate();
            }
         }

         return;
      }

      mProfileNode = -1;
      mSelProfNodeList.clear();
   }

	// Get the raycast collision position
   Point3F tPos;
   if ( !getStaticPos( event, tPos ) )
		return;  

   mouseLock();

   // Construct a LineSegment from the camera position to 1000 meters away in
   // the direction clicked.
   // If that segment hits the terrain, truncate the ray to only be that length.

   // We will use a LineSegment/Sphere intersection test to determine if a MeshRoadNode
   // was clicked.   

   MeshRoad *pRoad = NULL;
   MeshRoad *pClickedRoad = NULL;
   U32 insertNodeIdx = -1;
   Point3F collisionPnt;

   Point3F startPnt = event.pos;
   Point3F endPnt = event.pos + event.vec * 2000.0f;
   RayInfo ri;   

   if ( gServerContainer.castRay(startPnt, endPnt, StaticShapeObjectType, &ri) )
      endPnt = ri.point;

   DebugDrawer *ddraw = DebugDrawer::get();
   if ( false && ddraw )
   {
      ddraw->drawLine( startPnt, endPnt, ColorI(255,0,0,255) );
      ddraw->setLastTTL(DebugDrawer::DD_INFINITE);
   }

   // Check for collision with nodes of the currently selected or highlighted MeshRoad

   // Did we click on a MeshRoad? check currently selected road first
   if ( mSelRoad != NULL && mSelRoad->collideRay( event.pos, event.vec, &insertNodeIdx, &collisionPnt ) )
   {
      pClickedRoad = mSelRoad;
   }
   else
   {
      for ( SimSetIterator iter(mRoadSet); *iter; ++iter )
		{
			pRoad = static_cast<MeshRoad*>( *iter );

         // Do not select or edit a MeshRoad within a Prefab.
         if ( Prefab::getPrefabByChild(pRoad) )
            continue;

			if ( pRoad->collideRay( event.pos, event.vec, &insertNodeIdx, &collisionPnt ) )
			{
				pClickedRoad = pRoad;
				break;
			}
		}
   }

   /*
   else if ( gServerContainer.castRay(startPnt, endPnt, StaticShapeObjectType, &ri) )
   {
      MeshRoad *pRoad = NULL;
      pRoad = dynamic_cast<MeshRoad*>(ri.object);

      if ( pRoad && pRoad->collideRay( event.pos, event.vec, &insertNodeIdx, &collisionPnt ) )
         pClickedRoad = pRoad;
   }
   */

   // Did we click on a node?
   bool nodeClicked = false;
   S32 clickedNodeIdx = -1;

   // If we clicked on the currently selected road, only scan its nodes
   if ( mSelRoad != NULL && pClickedRoad == mSelRoad )
   {
      clickedNodeIdx = _getNodeAtScreenPos( mSelRoad, event.mousePoint );
      nodeClicked = clickedNodeIdx != -1;
   }
   else
   {
      for ( SimSetIterator iter(mRoadSet); *iter; ++iter )
      {
         pRoad = static_cast<MeshRoad*>( *iter );

         // Do not select or edit a MeshRoad within a Prefab.
         if ( Prefab::getPrefabByChild(pRoad) )
            continue;
         
         clickedNodeIdx = _getNodeAtScreenPos( pRoad, event.mousePoint );

         if ( clickedNodeIdx != -1 )
         {
            nodeClicked = true;
            pClickedRoad = pRoad;
            break;
         }
      }
   }
	
	// shortcuts
	bool dblClick = ( event.mouseClickCount > 1 );
   if( dblClick )
   { 
		if( mMode == mSelectMeshRoadMode )
		{
			setMode( mAddMeshRoadMode, true );
			return;
		}
		if( mMode == mAddNodeMode )
		{
			// Delete the node attached to the cursor.
			deleteSelectedNode();
			mMode = mAddMeshRoadMode;
			return;
		}
	}
	
	//this check is here in order to bounce back from deleting a whole road with ctrl+z
	//this check places the editor back into addrivermode
	if ( mMode == mAddNodeMode )
	{
      if ( !mSelRoad )
         mMode = mAddMeshRoadMode;
	}

   if ( mMode == mSelectMeshRoadMode )
	{
      // Did not click on a MeshRoad or a node.
      if ( !pClickedRoad  )
      {
         setSelectedRoad( NULL );
         setSelectedNode( -1 );
         
         return;
      }

      // Clicked on a MeshRoad that wasn't the currently selected River.
      if ( pClickedRoad != mSelRoad )
      {
         setSelectedRoad( pClickedRoad );
         setSelectedNode( clickedNodeIdx );
         return;
      }

     // Clicked on a node in the currently selected River that wasn't
      // the currently selected node.
      if ( nodeClicked )
      {
         setSelectedNode( clickedNodeIdx );
         return;
      }
	}
   else if ( mMode == mAddMeshRoadMode )
   {
		if ( nodeClicked )
      {
			// A double-click on a node in Normal mode means set AddNode mode.  
         if ( clickedNodeIdx == 0 )
         {
				setSelectedRoad( pClickedRoad  );
				setSelectedNode( clickedNodeIdx );

				mAddNodeIdx = clickedNodeIdx;
            mMode = mAddNodeMode; 

            mSelNode = mSelRoad->insertNode( tPos, mDefaultWidth, mDefaultDepth, mDefaultNormal, mAddNodeIdx );
            mIsDirty = true;

				return;
         }
			else if ( clickedNodeIdx == pClickedRoad->mNodes.size() - 1 )
         {
				setSelectedRoad( pClickedRoad  );
				setSelectedNode( clickedNodeIdx );

            mAddNodeIdx = U32_MAX;
				mMode = mAddNodeMode;

            mSelNode = mSelRoad->addNode( tPos, mDefaultWidth, mDefaultDepth, mDefaultNormal);
            mIsDirty = true;
				setSelectedNode( mSelNode );

				return;
         } 
		}

		MeshRoad *newRoad = new MeshRoad;  

      if(mTopMaterialAsset.notNull())
		   newRoad->_setTopMaterial(mTopMaterialAssetId);
      if (mBottomMaterialAsset.notNull())
		   newRoad->_setBottomMaterial(mBottomMaterialAssetId);
      if (mSideMaterialAsset.notNull())
		   newRoad->_setSideMaterial(mSideMaterialAssetId);
			
      newRoad->registerObject();

      // Add to scene                              
      Scene *scene;

      scene = Scene::getRootScene();
      if ( !scene)
         Con::errorf( "GuiMeshRoadEditorCtrl - could not find Scene to add new MeshRoad" );
      else
         scene->addObject( newRoad );

      Point3F pos( endPnt );
      pos.z += mDefaultDepth * 0.5f;

      newRoad->insertNode( pos, mDefaultWidth, mDefaultDepth, mDefaultNormal, 0 );
      U32 newNode = newRoad->insertNode( pos, mDefaultWidth, mDefaultDepth, mDefaultNormal, 1 );

      // Always add to the end of the road, the first node is the start.
      mAddNodeIdx = U32_MAX;

      setSelectedRoad( newRoad );      
      setSelectedNode( newNode );

      mMode = mAddNodeMode;

      // Disable the hover node while in addNodeMode, we
      // don't want some random node enlarged.
      mHoverNode = -1;

      // Grab the mission editor undo manager.
      UndoManager *undoMan = NULL;
      if ( !Sim::findObject( "EUndoManager", undoMan ) )
      {
         Con::errorf( "GuiMeshRoadEditorCtrl::on3DMouseDown() - EUndoManager not found!" );
         return;           
      }

      // Create the UndoAction.
      MECreateUndoAction *action = new MECreateUndoAction("Create MeshRoad");
      action->addObject( newRoad );
      
      // Submit it.               
      undoMan->addAction( action );

      //send a callback to script after were done here if one exists
      if (isMethod("onRoadCreation"))
         Con::executef(this, "onRoadCreation");

		return;
   }
	else if ( mMode == mAddNodeMode )
	{
		// Oops the road got deleted, maybe from an undo action?
      // Back to NormalMode.
      if ( mSelRoad )
      {
			// A double-click on a node in Normal mode means set AddNode mode.  
         if ( clickedNodeIdx == 0 )
         {
				submitUndo( "Add Node" );
				mAddNodeIdx = clickedNodeIdx;
            mMode = mAddNodeMode;
            mSelNode = mSelRoad->insertNode( tPos, mDefaultWidth, mDefaultDepth, mDefaultNormal, mAddNodeIdx );
            mIsDirty = true;
				setSelectedNode( mSelNode );

				return;
         }
			else
         {
				if( pClickedRoad && clickedNodeIdx == pClickedRoad->mNodes.size() - 1 )
				{
					submitUndo( "Add Node" );
					mAddNodeIdx = U32_MAX;
					mMode = mAddNodeMode;
					U32 newNode = mSelRoad->addNode( tPos, mDefaultWidth, mDefaultDepth, mDefaultNormal);
               mIsDirty = true;
					setSelectedNode( newNode );
					return;
				}
				else
				{
					submitUndo( "Insert Node" );
					// A single-click on empty space while in
					// AddNode mode means insert / add a node.
					//submitUndo( "Add Node" );
					U32 newNode = mSelRoad->insertNode( tPos, mDefaultWidth, mDefaultDepth, mDefaultNormal, mAddNodeIdx);
               mIsDirty = true;
					setSelectedNode( newNode );
					return;
				}
			}
		}

	}
	else if ( mMode == mInsertPointMode && mSelRoad != NULL )
	{
		if ( pClickedRoad == mSelRoad && insertNodeIdx != -1 )
      {
			// NOTE: I guess we have to determine the if the clicked ray intersects a road but not a specific node...
         // in order to handle inserting nodes in the same way as for fxRoad

         U32 prevNodeIdx = insertNodeIdx;
         U32 nextNodeIdx = ( prevNodeIdx + 1 > mSelRoad->mNodes.size() - 1 ) ? prevNodeIdx : prevNodeIdx + 1;

         const MeshRoadNode &prevNode = mSelRoad->mNodes[prevNodeIdx];
         const MeshRoadNode &nextNode = mSelRoad->mNodes[nextNodeIdx];

         F32 width = ( prevNode.width + nextNode.width ) * 0.5f;
         F32 depth = ( prevNode.depth + nextNode.depth ) * 0.5f;
         Point3F normal = ( prevNode.normal + nextNode.normal ) * 0.5f;
         normal.normalize();

         submitUndo( "Insert Node" );
         U32 newNode = mSelRoad->insertNode( collisionPnt, width, depth, normal, insertNodeIdx + 1 );
         mIsDirty = true;
         setSelectedNode( newNode );
			return;
       }
	}
	else if ( mMode == mRemovePointMode && mSelRoad != NULL )
	{
		if ( nodeClicked && pClickedRoad == mSelRoad )
      {
			setSelectedNode( clickedNodeIdx );
         deleteSelectedNode();
         return;
      }
	}
	else if ( mMode == mMovePointMode )
	{
		if ( nodeClicked && pClickedRoad == mSelRoad )
      {
			setSelectedNode( clickedNodeIdx );
         return;
      }
	}
	else if ( mMode == mScalePointMode )
	{
		if ( nodeClicked && pClickedRoad == mSelRoad )
      {
			setSelectedNode( clickedNodeIdx );
         return;
      }
	}
	else if ( mMode == mRotatePointMode )
	{
		if ( nodeClicked && pClickedRoad == mSelRoad )
      {
			setSelectedNode( clickedNodeIdx );
         return;
      }
	}   
}

void GuiMeshRoadEditorCtrl::on3DRightMouseDown(const Gui3DMouseEvent & event)
{
	//mIsPanning = true;
}

void GuiMeshRoadEditorCtrl::on3DRightMouseUp(const Gui3DMouseEvent & event)
{
   //mIsPanning = false;
}

void GuiMeshRoadEditorCtrl::on3DMouseUp(const Gui3DMouseEvent & event)
{
   mGizmo->on3DMouseUp(event);

   mSavedDrag = false;

   mSavedProfileDrag = false;

   if( MeshRoad::smShowRoadProfile && mSelRoad )
   {
      // If we need to deselect node... this means we clicked on a selected node without dragging
      if( mDeselectProfileNode )
      {
         S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );

         if(clickedNode == mProfileNode)
         {
            mProfileNode = -1;
            mSelProfNodeList.clear();
         }

         mDeselectProfileNode = false;
      }
      // Else if we dragged a node, update the road
      else
      {
         S32 clickedNode = _getProfileNodeAtScreenPos( &mSelRoad->mSideProfile, event.mousePoint );

         if(clickedNode == mProfileNode)
            mSelRoad->regenerate();       // This regens the road for collision purposes on the server
      }
   }

   mouseUnlock();
}

void GuiMeshRoadEditorCtrl::on3DMouseMove(const Gui3DMouseEvent & event)
{
   if ( mSelRoad != NULL && mMode == mAddNodeMode )
   {
      Point3F pos;

		mSelRoad->disableCollision();
      if ( getStaticPos( event, pos ) )         
      {
         pos.z += mSelRoad->getNodeDepth( mSelNode ) * 0.5f;
         mSelRoad->setNodePosition( mSelNode, pos );
         mIsDirty = true;
      }
		mSelRoad->enableCollision();

      return;
   }
   
   if ( mSelRoad != NULL && mSelNode != -1 )
   {
      mGizmo->on3DMouseMove( event );
      //mGizmo.collideAxisGizmo( event );
      //Con::printf( "SelectedAxis: %i", mGizmo.getSelectedAxis() );
   }

   // Is cursor hovering over a river?
   if ( mMode == mSelectMeshRoadMode )
   {
      mHoverRoad = NULL;

      Point3F startPnt = event.pos;
      Point3F endPnt = event.pos + event.vec * 1000.0f;

      RayInfo ri;   

      if ( gServerContainer.castRay(startPnt, endPnt, StaticShapeObjectType, &ri) )
      {         
         MeshRoad *pRoad = NULL;
         pRoad = dynamic_cast<MeshRoad*>(ri.object);         

         // Do not select or edit a MeshRoad within a Prefab.         
         if ( pRoad && !Prefab::getPrefabByChild(pRoad) )
            mHoverRoad = pRoad;
      }
   }

   // Is cursor over a node?
   if ( mHoverRoad )
   {
      MeshRoad *pRoad = NULL;
      S32 nodeIdx = -1;
      for ( SimSetIterator iter(mRoadSet); *iter; ++iter )
      {
         pRoad = static_cast<MeshRoad*>( *iter );

         nodeIdx = _getNodeAtScreenPos( pRoad, event.mousePoint );

         if ( nodeIdx != -1 )
         {
            mHoverRoad = pRoad;
            break;
         }
      }

      mHoverNode = nodeIdx;
   }
}

void GuiMeshRoadEditorCtrl::on3DMouseDragged(const Gui3DMouseEvent & event)
{   
   if( MeshRoad::smShowRoadProfile && mProfileNode > 0 && mSelRoad)
   {
      // If we haven't already saved,
      // save an undo action to get back to this state,
      // before we make any modifications to the selected node.
      if ( !mSavedProfileDrag )
      {
         submitUndo( "Modify Profile Node" );
         mSavedProfileDrag = true;
         mIsDirty = true;
      }

      U32 idx;
      Point3F pos, diff;

      PlaneF xy( mSelRoad->mSlices[0].p2, -mSelRoad->mSlices[0].fvec );
      xy.intersect(event.pos, event.vec, &pos);

      mSelRoad->mSideProfile.worldToObj(pos);
      diff = pos - mSelRoad->mSideProfile.mNodes[mProfileNode].getPosition();

      for(U32 i=0; i < mSelProfNodeList.size(); i++)
      {
         idx = mSelProfNodeList[i];
         pos = mSelRoad->mSideProfile.mNodes[idx].getPosition();
         pos += diff;

         if(pos.x < -mSelRoad->mSlices[0].width/2.0f)
            pos.x = -mSelRoad->mSlices[0].width/2.0f + 1e-6;

         mSelRoad->mSideProfile.setNodePosition( idx, pos );
      }

      mDeselectProfileNode = false;
      return;
   }

   // Drags are only used to transform nodes
   if ( !mSelRoad || mSelNode == -1 ||
      ( mMode != mMovePointMode && mMode != mScalePointMode && mMode != mRotatePointMode ) )
      return;

   // If we haven't already saved,
   // save an undo action to get back to this state,
   // before we make any modifications to the selected node.
   if ( !mSavedDrag )
   {
      submitUndo( "Modify Node" );
      mSavedDrag = true;
   }

   // If shift is held and we haven't already copied the node,
   // make a copy of the selected node and select it.
   if ( event.modifier & SI_SHIFT && !mHasCopied && mSelRoad->isEndNode( mSelNode ) )
   {
      const MeshRoadNode &data = mSelRoad->getNode( mSelNode );
      
      U32 insertIdx = ( mSelNode == 0 ) ? 0 : U32_MAX;      
      U32 newNodeIdx = mSelRoad->insertNode( data.point, data.width, data.depth, data.normal, insertIdx );
      mIsDirty = true;
         
      mSelNode = -1;
      setSelectedNode( newNodeIdx );

      mHasCopied = true;
   }

   // Let the Gizmo handle the drag, eg, modify its transforms
   mGizmo->on3DMouseDragged( event );
   if ( mGizmo->isDirty() )
   {
      Point3F pos = mGizmo->getPosition();
      Point3F scale = mGizmo->getScale();      
      const MatrixF &mat = mGizmo->getTransform();
      VectorF normal;
      mat.getColumn( 2, &normal );

      mSelRoad->setNode( pos, scale.x, scale.z, normal, mSelNode );
      mIsDirty = true;
      mGizmo->markClean();
   }

   Con::executef( this, "onNodeModified", Con::getIntArg(mSelNode) );
}

void GuiMeshRoadEditorCtrl::on3DMouseEnter(const Gui3DMouseEvent & event)
{
   // nothing to do
}

void GuiMeshRoadEditorCtrl::on3DMouseLeave(const Gui3DMouseEvent & event)
{
   // nothing to do
}

bool GuiMeshRoadEditorCtrl::onKeyDown(const GuiEvent& event)
{
	if( event.keyCode == KEY_RETURN && mMode == mAddNodeMode )
   {
		// Delete the node attached to the cursor.
		deleteSelectedNode();
		mMode = mAddMeshRoadMode;
		return true;
	}

	return false;
}

void GuiMeshRoadEditorCtrl::updateGuiInfo()
{
   // nothing to do
}
      
void GuiMeshRoadEditorCtrl::renderScene(const RectI & updateRect)
{
   //GFXDrawUtil *drawer = GFX->getDrawUtil();            

   GFX->setStateBlock( mZDisableSB );

   // get the projected size...
   GameConnection* connection = GameConnection::getConnectionToServer();
   if(!connection)
      return;

   // Grab the camera's transform
   MatrixF mat;
   connection->getControlCameraTransform(0, &mat);

   // Get the camera position
   Point3F camPos;
   mat.getColumn(3,&camPos);

   // Set up transform
   if( mSelRoad )
   {
      MatrixF profileMat(true);

      profileMat.setRow(0, mSelRoad->mSlices[0].rvec);
      profileMat.setRow(1, mSelRoad->mSlices[0].uvec);
      profileMat.setRow(2, -mSelRoad->mSlices[0].fvec);

      mSelRoad->mSideProfile.setTransform(profileMat, mSelRoad->mSlices[0].p2);
   }

   if ( mHoverRoad && mHoverRoad != mSelRoad )
   {
      _drawSpline( mHoverRoad, mHoverSplineColor );
   }

   if ( mSelRoad )
   {
      _drawSpline( mSelRoad, mSelectedSplineColor );            

      // Render Gizmo for selected node if were in either of the three transform modes
      if ( mSelNode != -1 && ( mMode == mMovePointMode || mMode == mScalePointMode || mMode == mRotatePointMode ) )
      {
			if( mMode == mMovePointMode )
			{
				mGizmo->getProfile()->mode = MoveMode;
			}
			else if( mMode == mScalePointMode )
			{
				mGizmo->getProfile()->mode = ScaleMode;
			}
			else if( mMode == mRotatePointMode )
			{
				mGizmo->getProfile()->mode = RotateMode;
			}

         const MeshRoadNode &node = mSelRoad->mNodes[mSelNode];

         MatrixF objMat = mSelRoad->getNodeTransform(mSelNode);      
         Point3F objScale( node.width, 1.0f, node.depth );
         Point3F worldPos = node.point;

         mGizmo->set( objMat, worldPos, objScale );

         mGizmo->renderGizmo( mLastCameraQuery.cameraMatrix, mLastCameraQuery.fov );
			
			// Render Gizmo text
			mGizmo->renderText( mSaveViewport, mSaveModelview, mSaveProjection );     
      }    
   }

   DebugDrawer::get()->render();

   // Now draw all the 2d stuff!
   GFX->setClipRect(updateRect); 
   
   // Draw Control nodes for selecting and highlighted rivers
   if ( mHoverRoad )
      _drawControlNodes( mHoverRoad, mHoverSplineColor );
   if ( mSelRoad )
      _drawControlNodes( mSelRoad, mSelectedSplineColor );

   if(MeshRoad::smShowRoadProfile)
   {
      char buf[64];
      Point2I posi;

      posi.x = 10;
      posi.y = updateRect.len_y() - 80;

      GFX->getDrawUtil()->setBitmapModulation(ColorI(128, 128, 128));
      dStrcpy(buf, "Reset Profile: Double-click Start Node", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Move Node: Click and Drag Node", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Select Multiple Nodes: Ctrl-click Nodes", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Toggle Material: Shift-click Spline Segment", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Toggle Smoothing: Shift-click Node", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Delete Node: Alt-click Node", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
      posi.y -= mProfile->mFont->getCharHeight((U8)buf[0]) + 4;
      dStrcpy(buf, "Add Node: Ctrl-click Spline", 64);
      GFX->getDrawUtil()->drawTextN(mProfile->mFont, posi, buf, dStrlen(buf));
   }
} 

S32 GuiMeshRoadEditorCtrl::_getNodeAtScreenPos( const MeshRoad *pRoad, const Point2I &posi )
{
   for ( U32 i = 0; i < pRoad->mNodes.size(); i++ )
   {
      const Point3F &nodePos = pRoad->mNodes[i].point;

      Point3F screenPos;
      project( nodePos, &screenPos );

      if ( screenPos.z < 0.0f )
         continue;

      Point2I screenPosI( (S32)screenPos.x, (S32)screenPos.y );

      RectI nodeScreenRect( screenPosI - mNodeHalfSize, mNodeHalfSize * 2 );

      if ( nodeScreenRect.pointInRect(posi) )
      {
         // we found a hit!         
         return i;         
      }           
   }      

   return -1;
}

S32 GuiMeshRoadEditorCtrl::_getProfileNodeAtScreenPos( MeshRoadProfile *pProfile, const Point2I &posi)
{
   for ( U32 i = 0; i < pProfile->mNodes.size(); i++ )
   {
      Point3F nodePos;
      pProfile->getNodeWorldPos(i, nodePos);

      Point3F screenPos;
      project( nodePos, &screenPos );

      if ( screenPos.z < 0.0f )
         continue;

      Point2I screenPosI( (S32)screenPos.x, (S32)screenPos.y );

      RectI nodeScreenRect( screenPosI - mNodeHalfSize, mNodeHalfSize * 2 );

      if ( nodeScreenRect.pointInRect(posi) )
      {
         // we found a hit!
         return i;
      }
   }

   return -1;
}

void GuiMeshRoadEditorCtrl::_drawSpline( MeshRoad *river, const ColorI &color )
{
   if ( river->mSlices.size() <= 1 )
      return;

	if ( MeshRoad::smShowSpline )
	{
		// Render the River center-line
		if( MeshRoad::smShowRoadProfile )
			PrimBuild::color( ColorI(100,100,100) );
		else
			PrimBuild::color( color );

		PrimBuild::begin( GFXLineStrip, river->mSlices.size() );
		for ( U32 i = 0; i < river->mSlices.size(); i++ )
		{            		      
			PrimBuild::vertex3fv( river->mSlices[i].p1 );		      
		}
		PrimBuild::end();
	}
	
	if ( MeshRoad::smWireframe )
	{
		// Left-side line
		PrimBuild::color3i( 100, 100, 100 );
		PrimBuild::begin( GFXLineStrip, river->mSlices.size() );            
		for ( U32 i = 0; i < river->mSlices.size(); i++ )
		{            		      
			PrimBuild::vertex3fv( river->mSlices[i].p0 );		      
		}
		PrimBuild::end();

		// Right-side line
		PrimBuild::begin( GFXLineStrip, river->mSlices.size() );            
		for ( U32 i = 0; i < river->mSlices.size(); i++ )
		{            		      
			PrimBuild::vertex3fv( river->mSlices[i].p2 );		      
		}
		PrimBuild::end();

		// Cross-sections
		PrimBuild::begin( GFXLineList, river->mSlices.size() * 2 );            
		for ( U32 i = 0; i < river->mSlices.size(); i++ )
		{            		      
			PrimBuild::vertex3fv( river->mSlices[i].p0 );
			PrimBuild::vertex3fv( river->mSlices[i].p2 );		      
		}
		PrimBuild::end();
	}

   // If we are in Profile Edit Mode, draw the profile spline and node normals
   if ( MeshRoad::smShowRoadProfile )
   {
      Point3F nodePos;
      Point3F normEndPos;
      U32 numSide, numTop, numBottom;

      numSide = numTop = numBottom = 0;

      for ( U32 i = 0; i < river->mSideProfile.mSegMtrls.size(); i++ )
      {
         switch(river->mSideProfile.mSegMtrls[i])
         {
         case MeshRoad::Side:		numSide++;		break;
         case MeshRoad::Top:		numTop++;		break;
         case MeshRoad::Bottom:	numBottom++;	break;
         }
      }

      // Render the profile spline
      // Side
      if(numSide)
      {
         PrimBuild::color( mProfileColor );
         PrimBuild::begin( GFXLineList, 2*numSide );
         for ( U32 i = 0; i < river->mSideProfile.mSegMtrls.size(); i++ )
         {
            if(river->mSideProfile.mSegMtrls[i] == MeshRoad::Side)
            {
               river->mSideProfile.getNodeWorldPos(i, nodePos);
               PrimBuild::vertex3fv( nodePos );

               river->mSideProfile.getNodeWorldPos(i+1, nodePos);
               PrimBuild::vertex3fv( nodePos );
            }
         }
         PrimBuild::end();
      }

      // Top
      if(numTop)
      {
         PrimBuild::color( ColorI(0,255,0) );
         PrimBuild::begin( GFXLineList, 2*numTop );
         for ( U32 i = 0; i < river->mSideProfile.mSegMtrls.size(); i++ )
         {
            if(river->mSideProfile.mSegMtrls[i] == MeshRoad::Top)
            {
               river->mSideProfile.getNodeWorldPos(i, nodePos);
               PrimBuild::vertex3fv( nodePos );

               river->mSideProfile.getNodeWorldPos(i+1, nodePos);
               PrimBuild::vertex3fv( nodePos );
            }
         }
         PrimBuild::end();
      }

      // Bottom
      if(numBottom)
      {
         PrimBuild::color( ColorI(255,0,255) );
         PrimBuild::begin( GFXLineList, 2*numBottom );
         for ( U32 i = 0; i < river->mSideProfile.mSegMtrls.size(); i++ )
         {
            if(river->mSideProfile.mSegMtrls[i] == MeshRoad::Bottom)
            {
               river->mSideProfile.getNodeWorldPos(i, nodePos);
               PrimBuild::vertex3fv( nodePos );

               river->mSideProfile.getNodeWorldPos(i+1, nodePos);
               PrimBuild::vertex3fv( nodePos );
            }
         }
         PrimBuild::end();
      }

      // Render node normals
      PrimBuild::color( ColorI(255,0,0) );
      PrimBuild::begin( GFXLineList, 4*river->mSideProfile.mNodes.size() - 4 );
      for ( U32 i = 0; i < river->mSideProfile.mNodes.size()-1; i++ )
      {
         for( U32 j = 0; j < 2; j++)
         {
            river->mSideProfile.getNodeWorldPos(i+j, nodePos);
            PrimBuild::vertex3fv( nodePos );

            river->mSideProfile.getNormWorldPos(2*i+j, normEndPos);
            PrimBuild::vertex3fv( normEndPos );
         }
      }
      PrimBuild::end();
   }

   // Segment 
}

void GuiMeshRoadEditorCtrl::_drawControlNodes( MeshRoad *river, const ColorI &color )
{
   if ( !MeshRoad::smShowSpline )
      return;

   RectI bounds = getBounds();

   GFXDrawUtil *drawer = GFX->getDrawUtil();

   bool isSelected = ( river == mSelRoad );
   bool isHighlighted = ( river == mHoverRoad );
   
   for ( U32 i = 0; i < river->mNodes.size(); i++ )
   {
      if ( false && isSelected && mSelNode == i  )
         continue;

      const Point3F &wpos = river->mNodes[i].point;

      Point3F spos;
      project( wpos, &spos );                  

      if ( spos.z > 1.0f )
         continue;

      Point2I posi;
      posi.x = spos.x;
      posi.y = spos.y;

      if ( !bounds.pointInRect( posi ) )
         continue;

      ColorI theColor = color;
      Point2I nodeHalfSize = mNodeHalfSize;
            
      if ( isHighlighted && mHoverNode == i )
      {
         //theColor = mHoverNodeColor;
         nodeHalfSize += Point2I(2,2);
      }

      if ( isSelected )
      {   
         if ( mSelNode == i )
         {
            theColor.set(0,0,255);
         }
         else if ( i == 0 )
         {
            theColor.set(0,255,0);
         }
         else if ( i == river->mNodes.size() - 1 )
         {
            theColor.set(255,0,0);
         }         
      }

      if( MeshRoad::smShowRoadProfile && isSelected )
         theColor.set(100,100,100);

      drawer->drawRectFill( posi - nodeHalfSize, posi + nodeHalfSize, theColor );
   }

   // Draw profile control nodes
   if( MeshRoad::smShowRoadProfile && isSelected )
   {
      Point3F wpos;
      Point3F spos;
      Point2I posi;
      ColorI theColor;

      for( U32 i = 0; i < river->mSideProfile.mNodes.size(); i++)
      {
         river->mSideProfile.getNodeWorldPos(i, wpos);

         project( wpos, &spos );

         if ( spos.z > 1.0f )
            continue;

         posi.x = spos.x;
         posi.y = spos.y;

         if ( !bounds.pointInRect( posi ) )
            continue;

         if(i == 0)
            theColor.set(mProfileColor.red/3, mProfileColor.green/3, mProfileColor.blue/3,255);
         else
            theColor.set(mProfileColor,255);

         if( mSelProfNodeList.find_next(i) != -1 )
            theColor.set(0,0,255);

         drawer->drawRectFill( posi - mNodeHalfSize, posi + mNodeHalfSize, theColor );
      }
   }
}

bool GuiMeshRoadEditorCtrl::getStaticPos( const Gui3DMouseEvent & event, Point3F &tpos )
{     
   // Find clicked point on the terrain

   Point3F startPnt = event.pos;
   Point3F endPnt = event.pos + event.vec * 1000.0f;

   RayInfo ri;
   bool hit;         
         
   hit = gServerContainer.castRay(startPnt, endPnt, StaticShapeObjectType, &ri);    
   tpos = ri.point;
   
   return hit;
}

void GuiMeshRoadEditorCtrl::deleteSelectedNode()
{    
   if ( !mSelRoad || mSelNode == -1 )
      return;

   // If the Road has only two nodes remaining,
   // delete the whole Road.
   if ( mSelRoad->mNodes.size() <= 2 )
   {      
      deleteSelectedRoad( mMode != mAddNodeMode );
   }
   else
   {
      if ( mMode != mAddNodeMode )
         submitUndo( "Delete Node" );

      // Delete the SelectedNode of the SelectedRoad
      mSelRoad->deleteNode( mSelNode );

      // We deleted the Node but not the Road (it has nodes left)
      // so decrement the currently selected node.
      if ( mSelRoad->mNodes.size() <= mSelNode )
         setSelectedNode( mSelNode - 1 );
      else
      {
         // force gizmo to update to the selected nodes position
         // the index didn't change but the node it refers to did.
         U32 i = mSelNode;
         mSelNode = -1;
         setSelectedNode( i );
      }
   }

   // If you were in addNodeMode, 
   // deleting a node should ends it.
   //mMode = smNormalMode;
}

void GuiMeshRoadEditorCtrl::deleteSelectedRoad( bool undoAble )
{
   AssertFatal( mSelRoad != NULL, "GuiMeshRoadEditorCtrl::deleteSelectedRoad() - No Road is selected" );

   // Not undoAble? Just delete it.
   if ( !undoAble )
   {
      mSelRoad->deleteObject();
      mIsDirty = true;
      Con::executef( this, "onRoadSelected" );
      mSelNode = -1;

      return;
   }

   // Grab the mission editor undo manager.
   UndoManager *undoMan = NULL;
   if ( !Sim::findObject( "EUndoManager", undoMan ) )
   {
      // Couldn't find it? Well just delete the Road.
      Con::errorf( "GuiMeshRoadEditorCtrl::on3DMouseDown() - EUndoManager not found!" );    
      return;
   }
   else
   {
      // Create the UndoAction.
      MEDeleteUndoAction *action = new MEDeleteUndoAction("Deleted Road");
      action->deleteObject( mSelRoad );
      mIsDirty = true;

      // Submit it.               
      undoMan->addAction( action );
   }

   // ScriptCallback with 'NULL' parameter for no Road currently selected.
   Con::executef( this, "onRoadSelected" );

   // Clear the SelectedNode (it has been deleted along with the River).  
	setSelectedNode( -1 );
   mSelNode = -1;
}

void GuiMeshRoadEditorCtrl::setMode( String mode, bool sourceShortcut = false )
{
   mMode = mode;

	if( sourceShortcut )
		Con::executef( this, "paletteSync", mode );
}

void GuiMeshRoadEditorCtrl::setSelectedRoad( MeshRoad *road )
{
   mSelRoad = road;

   if ( mSelRoad != NULL )
      Con::executef( this, "onRoadSelected", road->getIdString() );
   else
      Con::executef( this, "onRoadSelected" );

	if ( mSelRoad != road )
      setSelectedNode(-1);
}

void GuiMeshRoadEditorCtrl::setNodeWidth( F32 width )
{
   if ( mSelRoad && mSelNode != -1 )
   {
      mSelRoad->setNodeWidth( mSelNode, width );
      mIsDirty = true;
   }
}

F32 GuiMeshRoadEditorCtrl::getNodeWidth()
{
   if ( mSelRoad && mSelNode != -1 )
      return mSelRoad->getNodeWidth( mSelNode );

   return 0.0f;   
}

void GuiMeshRoadEditorCtrl::setNodeDepth(F32 depth)
{
   if ( mSelRoad && mSelNode != -1 )
   {
      mSelRoad->setNodeDepth( mSelNode, depth );
      mIsDirty = true;
   }
}

F32 GuiMeshRoadEditorCtrl::getNodeDepth()
{
   if ( mSelRoad && mSelNode != -1 )
      return mSelRoad->getNodeDepth( mSelNode );

   return 0.0f;
}

void GuiMeshRoadEditorCtrl::setNodePosition(const Point3F& pos)
{
   if ( mSelRoad && mSelNode != -1 )
   {
      mSelRoad->setNodePosition( mSelNode, pos );
      mIsDirty = true;
   }
}

Point3F GuiMeshRoadEditorCtrl::getNodePosition()
{
   if ( mSelRoad && mSelNode != -1 )
      return mSelRoad->getNodePosition( mSelNode );

   return Point3F( 0, 0, 0 );   
}

void GuiMeshRoadEditorCtrl::setNodeNormal( const VectorF &normal )
{
   if ( mSelRoad && mSelNode != -1 )
   {
      mSelRoad->setNodeNormal( mSelNode, normal );
      mIsDirty = true;
   }
}

VectorF GuiMeshRoadEditorCtrl::getNodeNormal()
{
   if ( mSelRoad && mSelNode != -1 )
      return mSelRoad->getNodeNormal( mSelNode );

   return VectorF::Zero;
}

void GuiMeshRoadEditorCtrl::setSelectedNode( S32 node )
{
   if ( mSelNode == node )
      return;

   mSelNode = node;
   if ( mSelNode != -1 )
   {
      const MeshRoadNode &curNode = mSelRoad->mNodes[mSelNode];

      MatrixF objMat = mSelRoad->getNodeTransform(mSelNode);      
      Point3F objScale(curNode.width, 1.0f, curNode.depth );
      Point3F worldPos = curNode.point;
      
      mGizmo->set( objMat, worldPos, objScale );
   }
   
   if ( mSelNode != -1 )
      Con::executef( this, "onNodeSelected", Con::getIntArg(mSelNode) );
   else
      Con::executef( this, "onNodeSelected", Con::getIntArg(-1) );
}

void GuiMeshRoadEditorCtrl::submitUndo( const UTF8 *name )
{
   // Grab the mission editor undo manager.
   UndoManager *undoMan = NULL;
   if ( !Sim::findObject( "EUndoManager", undoMan ) )
   {
      Con::errorf( "GuiMeshRoadEditorCtrl::submitUndo() - EUndoManager not found!" );
      return;           
   }

   // Setup the action.
   GuiMeshRoadEditorUndoAction *action = new GuiMeshRoadEditorUndoAction( name );

   action->mObjId = mSelRoad->getId();
   //action->mMetersPerSegment = mSelRoad->mMetersPerSegment;
   action->mEditor = this;

   for( U32 i = 0; i < mSelRoad->mNodes.size(); i++ )
   {
      action->mNodes.push_back( mSelRoad->mNodes[i] );      
   }
      
   // Save profile nodes and materials
   for( U32 i = 0; i < mSelRoad->mSideProfile.mNodes.size(); i++)
   {
      action->mProfileNodes.push_back( mSelRoad->mSideProfile.mNodes[i] );

      if(i)
         action->mProfileMtrls.push_back( mSelRoad->mSideProfile.mSegMtrls[i-1] );
   }

   undoMan->addAction( action );
}

void GuiMeshRoadEditorCtrl::matchTerrainToRoad()
{
   if ( !mSelRoad )
      return;

   // Not implemented, but a potentially useful idea.
   // Move manipulate the terrain so that the MeshRoad appears to be sitting 
   // on top of it.

   // The opposite could also be useful, manipulate the MeshRoad to line up
   // with the terrain underneath it.
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, deleteNode, void, (), , "deleteNode()" )
{
   object->deleteSelectedNode();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getMode, const char*, (), , "" )
{
   return object->getMode();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setMode, void, (const char * mode), , "setMode( String mode )" )
{
   String newMode = ( mode );
   object->setMode( newMode );
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getNodeWidth, F32, (), , "" )
{
   return object->getNodeWidth();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setNodeWidth, void, ( F32 width ), , "" )
{
   object->setNodeWidth( width );
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getNodeDepth, F32, (), , "" )
{
   return object->getNodeDepth();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setNodeDepth, void, ( F32 depth ), , "" )
{
   object->setNodeDepth( depth );
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getNodePosition, Point3F, (), , "" )
{

	return object->getNodePosition();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setNodePosition, void, (Point3F pos), , "" )
{

   object->setNodePosition( pos );
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getNodeNormal, Point3F, (), , "" )
{

	return object->getNodeNormal();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setNodeNormal, void, (Point3F normal), , "" )
{

   object->setNodeNormal( normal );
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, setSelectedRoad, void, (const char * objName), (""), "" )
{
   if ( String::isEmpty(objName) )
      object->setSelectedRoad(NULL);
   else
   {
      MeshRoad *road = NULL;
      if ( Sim::findObject( objName, road ) )
         object->setSelectedRoad(road);
   }
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, getSelectedRoad, S32, (), , "" )
{
   MeshRoad *road = object->getSelectedRoad();
   if ( !road )
      return 0;

   return road->getId();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, regenerate, void, (), , "" )
{
   MeshRoad *road = object->getSelectedRoad();
   if ( road )
      road->regenerate();
}

DefineEngineMethod( GuiMeshRoadEditorCtrl, matchTerrainToRoad, void, (), , "" )
{
   object->matchTerrainToRoad();
}
