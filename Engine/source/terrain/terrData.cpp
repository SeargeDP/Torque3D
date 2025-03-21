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

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#include "platform/platform.h"
#include "terrain/terrData.h"

#include "terrain/terrCollision.h"
#include "terrain/terrCell.h"
#include "terrain/terrRender.h"
#include "terrain/terrMaterial.h"
#include "terrain/terrCellMaterial.h"
#include "gui/worldEditor/terrainEditor.h"
#include "math/mathIO.h"
#include "core/stream/fileStream.h"
#include "core/stream/bitStream.h"
#include "console/consoleTypes.h"
#include "sim/netConnection.h"
#include "core/util/safeDelete.h"
#include "T3D/objectTypes.h"
#include "renderInstance/renderPassManager.h"
#include "scene/sceneRenderState.h"
#include "materials/materialManager.h"
#include "materials/baseMatInstance.h"
#include "gfx/gfxTextureManager.h"
#include "gfx/gfxCardProfile.h"
#include "gfx/gfxAPI.h"
#include "core/resourceManager.h"
#include "T3D/physics/physicsPlugin.h"
#include "T3D/physics/physicsBody.h"
#include "T3D/physics/physicsCollision.h"
#include "console/engineAPI.h"
#include "core/util/safeRelease.h"

#include "T3D/assets/TerrainMaterialAsset.h"
using namespace Torque;

IMPLEMENT_CO_NETOBJECT_V1(TerrainBlock);

ConsoleDocClass( TerrainBlock,
   "@brief Represent a terrain object in a Torque 3D level\n\n"

  "@tsexample\n"
	"new TerrainBlock(theTerrain)\n"
	"{\n"
	"   terrainFile = \"art/terrains/Deathball Desert_0.ter\";\n"
	"   squareSize = \"2\";\n"
	"   tile = \"0\";\n"
	"   baseTexSize = \"1024\";\n"
	"   screenError = \"16\";\n"
	"   position = \"-1024 -1024 179.978\";\n"
	"   rotation = \"1 0 0 0\";\n"
	"   scale = \"1 1 1\";\n"
	"   isRenderEnabled = \"true\";\n"
	"   canSaveDynamicFields = \"1\";\n"
	"};\n"
   "@endtsexample\n\n"

   "@see TerrainMaterial\n\n"

   "@ingroup Terrain\n"
);


Signal<void(U32,TerrainBlock*,const Point2I& ,const Point2I&)> TerrainBlock::smUpdateSignal;

F32 TerrainBlock::smLODScale = 1.0f;
F32 TerrainBlock::smDetailScale = 1.0f;


//RBP - Global function declared in Terrdata.h
TerrainBlock* getTerrainUnderWorldPoint(const Point3F & wPos)
{
	// Cast a ray straight down from the world position and see which
	// Terrain is the closest to our starting point
	Point3F startPnt = wPos;
	Point3F endPnt = wPos + Point3F(0.0f, 0.0f, -10000.0f);

	S32 blockIndex = -1;
	F32 nearT = 1.0f;

	SimpleQueryList queryList;
	gServerContainer.findObjects( TerrainObjectType, SimpleQueryList::insertionCallback, &queryList);

	for (U32 i = 0; i < queryList.mList.size(); i++)
	{
		Point3F tStartPnt, tEndPnt;
		TerrainBlock* terrBlock = dynamic_cast<TerrainBlock*>(queryList.mList[i]);
		terrBlock->getWorldTransform().mulP(startPnt, &tStartPnt);
		terrBlock->getWorldTransform().mulP(endPnt, &tEndPnt);

		RayInfo ri;
		if (terrBlock->castRayI(tStartPnt, tEndPnt, &ri, true))
		{
			if (ri.t < nearT)
			{
				blockIndex = i;
				nearT = ri.t;
			}
		}
	}

	if (blockIndex > -1)
		return (TerrainBlock*)(queryList.mList[blockIndex]);

	return NULL;
}


ConsoleDocFragment _getTerrainUnderWorldPoint1(
   "@brief Gets the terrain block that is located under the given world point\n\n"
   "@param position The world space coordinate you wish to query at. Formatted as (\"x y z\")\n\n"
   "@return Returns the ID of the requested terrain block (0 if not found).\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainUnderWorldPoint( Point3F position );"
);
ConsoleDocFragment _getTerrainUnderWorldPoint2(
   "@brief Takes a world point and find the \"highest\" terrain underneath it\n\n"
   "@param x The X coordinate in world space\n"
   "@param y The Y coordinate in world space\n\n"
   "@param z The Z coordinate in world space\n\n"
   "@return Returns the ID of the requested terrain block (0 if not found).\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainUnderWorldPoint( F32 x, F32 y, F32 z);"
);

DefineEngineFunction( getTerrainUnderWorldPoint, S32, (const char* ptOrX, const char* y, const char* z), ("", ""),
                                                      "(Point3F x/y/z) Gets the terrain block that is located under the given world point.\n"
                                                      "@param x/y/z The world coordinates (floating point values) you wish to query at. " 
                                                      "These can be formatted as either a string (\"x y z\") or separately as (x, y, z)\n"
                                                      "@return Returns the ID of the requested terrain block (0 if not found).\n\n"
                                                      "@hide")
{
   Point3F pos;
   if(!String::isEmpty(ptOrX) && String::isEmpty(y) && String::isEmpty(z))
      dSscanf(ptOrX, "%f %f %f", &pos.x, &pos.y, &pos.z);
   else if(!String::isEmpty(ptOrX) && !String::isEmpty(y) && !String::isEmpty(z))
   {
      pos.x = dAtof(ptOrX);
      pos.y = dAtof(y);
      pos.z = dAtof(z);
   }
   TerrainBlock* terrain = getTerrainUnderWorldPoint(pos);
   if(terrain != NULL)
   {
      return terrain->getId();
   }
   return 0;
}


typedef TerrainBlock::BaseTexFormat baseTexFormat;
DefineEnumType(baseTexFormat);

ImplementEnumType(baseTexFormat,
   "Description\n"
   "@ingroup ?\n\n")
{ TerrainBlock::NONE, "NONE", "No cached terrain.\n" },
{ TerrainBlock::DDS, "DDS", "Cache the terrain in a DDS format.\n" },
{ TerrainBlock::PNG, "PNG", "Cache the terrain in a PNG format.\n" },
EndImplementEnumType;

TerrainBlock::TerrainBlock()
 : mLightMap( NULL ),
   mLightMapSize( 256 ),
   mCRC( 0 ),
   mMaxDetailDistance( 0.0f ),
   mBaseTexScaleConst( NULL ),
   mBaseTexIdConst( NULL ),
   mBaseLayerSizeConst(NULL),
   mDetailsDirty( false ),
   mLayerTexDirty( false ),
   mBaseTexSize( 1024 ),
   mBaseTexFormat( TerrainBlock::DDS ),
   mCell( NULL ),
   mBaseMaterial( NULL ),
   mDefaultMatInst( NULL ),
   mSquareSize( 1.0f ),
   mPhysicsRep( NULL ),
   mScreenError( 16 ),
   mCastShadows( true ),
   mZoningDirty( false ),
   mUpdateBasetex ( true ),
   mDetailTextureArray( NULL ),
   mMacroTextureArray( NULL ),
   mOrmTextureArray( NULL ),
   mNormalTextureArray( NULL )
{
   mTypeMask = TerrainObjectType | StaticObjectType | StaticShapeObjectType;
   mNetFlags.set(Ghostable | ScopeAlways);
   mIgnoreZodiacs = false;
   zode_primBuffer = 0;

   mTerrainAsset = StringTable->EmptyString();
   mTerrainAssetId = StringTable->EmptyString();
}

TerrainBlock::~TerrainBlock()
{
   // Kill collision
   mTerrainConvexList.nukeList();

   SAFE_DELETE(mLightMap);
   mLightMapTex = NULL;

#ifdef TORQUE_TOOLS
   TerrainEditor* editor = dynamic_cast<TerrainEditor*>(Sim::findObject("ETerrainEditor"));
   if (editor)
      editor->detachTerrain(this);
#endif
   deleteZodiacPrimitiveBuffer();

   SAFE_RELEASE(mDetailTextureArray);
   SAFE_RELEASE(mMacroTextureArray);
   SAFE_RELEASE(mNormalTextureArray);
   SAFE_RELEASE(mOrmTextureArray);
}

void TerrainBlock::_onTextureEvent( GFXTexCallbackCode code )
{
   if ( code == GFXZombify )
   {
      if (  mBaseTex.isValid() &&
            mBaseTex->isRenderTarget() )
         mBaseTex = NULL;

      mLayerTex = NULL;
      mLightMapTex = NULL;
   }
}

bool TerrainBlock::_setSquareSize( void *obj, const char *index, const char *data )
{
   TerrainBlock *terrain = static_cast<TerrainBlock*>( obj );

   F32 newSqaureSize = dAtof( data );
   if ( !mIsEqual( terrain->mSquareSize, newSqaureSize ) )
   {
      terrain->mSquareSize = newSqaureSize;

      if ( terrain->isServerObject() && terrain->isProperlyAdded() )
         terrain->_updateBounds();

      terrain->setMaskBits( HeightMapChangeMask | SizeMask );
   }

   return false;
}

bool TerrainBlock::_setBaseTexSize( void *obj, const char *index, const char *data )
{
   TerrainBlock *terrain = static_cast<TerrainBlock*>( obj );

   // NOTE: We're limiting the base texture size to 
   // 2048 as anything greater in size becomes too
   // large to generate for many cards.
   //
   // If you want to remove this limit feel free, but
   // prepare for problems if you don't ship the baked
   // base texture with your installer.
   //

   S32 texSize = mClamp( dAtoi( data ), 0, 2048 );
   if ( terrain->mBaseTexSize != texSize )
   {
      terrain->mBaseTexSize = texSize;
      terrain->setMaskBits( MaterialMask );
   }

   return false;
}

bool TerrainBlock::_setBaseTexFormat(void *obj, const char *index, const char *data)
{
   TerrainBlock *terrain = static_cast<TerrainBlock*>(obj);

   EngineEnumTable eTable = _baseTexFormat::_sEnumTable;

   for (U8 i = 0; i < eTable.getNumValues(); i++)
   {
      if (strcasecmp(eTable[i].mName, data) == 0)
      {
         terrain->mBaseTexFormat = (BaseTexFormat)eTable[i].mInt;
         terrain->_updateMaterials();

         if (terrain->isServerObject()) return false;
         terrain->_updateLayerTexture();
         // If the cached base texture is older that the terrain file or
         // it doesn't exist then generate and cache it.
         String baseCachePath = terrain->_getBaseTexCacheFileName();
         if (Platform::compareModifiedTimes(baseCachePath, terrain->mTerrainAsset->getTerrainFilePath()) < 0 && terrain->mUpdateBasetex)
            terrain->_updateBaseTexture(true);
         break;
      }
   }

   return false;
}

bool TerrainBlock::_setLightMapSize( void *obj, const char *index, const char *data )
{
   TerrainBlock *terrain = static_cast<TerrainBlock*>(obj);

   // Handle inspector value decrements correctly
   U32 mapSize = dAtoi( data );
   if ( mapSize == terrain->mLightMapSize-1 )
      mapSize = terrain->mLightMapSize/2;

   // Limit the lightmap size, and ensure it is a power of 2
   const U32 maxTextureSize = GFX->getCardProfiler()->queryProfile( "maxTextureSize", 1024 );
   mapSize = mClamp( getNextPow2( mapSize ), 0, maxTextureSize );

   if ( terrain->mLightMapSize != mapSize )
   {
      terrain->mLightMapSize = mapSize;
      terrain->setMaskBits( MaterialMask );
   }

   return false;
}

bool TerrainBlock::setFile( const FileName &terrFileName )
{
   if ( mTerrainAsset && mTerrainAsset->getTerrainFilePath() == StringTable->insert(terrFileName) )
      return mFile != NULL;

   Resource<TerrainFile> file = ResourceManager::get().load( terrFileName );
   if( !file )
      return false;
      
   setFile( file );
   setMaskBits( FileMask | HeightMapChangeMask );
   
   return true;
}

void TerrainBlock::setFile(const Resource<TerrainFile>& terr)
{
   if (mFile)
   {
      GFXTextureManager::removeEventDelegate(this, &TerrainBlock::_onTextureEvent);
      MATMGR->getFlushSignal().remove(this, &TerrainBlock::_onFlushMaterials);
   }

   mFile = terr;

   if (!mFile)
   {
      Con::errorf("TerrainBlock::setFile() - No valid terrain file!");
      return;
   }

   if (terr->mNeedsResaving)
   {
      if (Platform::messageBox("Update Terrain File", "You appear to have a Terrain file in an older format. Do you want Torque to update it?", MBOkCancel, MIQuestion) == MROk)
      {
         mFile->save(terr->mFilePath.getFullPath());
         mFile->mNeedsResaving = false;
      }
   }

   if (terr->mFileVersion != TerrainFile::FILE_VERSION || terr->mNeedsResaving)
   {
      Con::errorf(" *********************************************************");
      Con::errorf(" *********************************************************");
      Con::errorf(" *********************************************************");
      Con::errorf(" PLEASE RESAVE THE TERRAIN FILE FOR THIS MISSION!  THANKS!");
      Con::errorf(" *********************************************************");
      Con::errorf(" *********************************************************");
      Con::errorf(" *********************************************************");
   }


   _updateBounds();

   resetWorldBox();
   setRenderTransform(mObjToWorld);

   if (isClientObject())
   {
      if (mCRC != terr.getChecksum())
      {
         NetConnection::setLastError("Your terrain file doesn't match the version that is running on the server.");
         return;
      }

      clearLightMap();

      // Init the detail layer rendering helper.
      _updateMaterials();
      _updateLayerTexture();

      // If the cached base texture is older that the terrain file or
      // it doesn't exist then generate and cache it.
      String baseCachePath = _getBaseTexCacheFileName();
      if (Platform::compareModifiedTimes(baseCachePath, mTerrainAsset->getTerrainFilePath()) < 0 && mUpdateBasetex)
         _updateBaseTexture(true);

      // The base texture should have been cached by now... so load it.
      mBaseTex.set(baseCachePath, &GFXStaticTextureSRGBProfile, "TerrainBlock::mBaseTex");

      GFXTextureManager::addEventDelegate(this, &TerrainBlock::_onTextureEvent);
      MATMGR->getFlushSignal().notify(this, &TerrainBlock::_onFlushMaterials);

      // Build the terrain quadtree.
      _rebuildQuadtree();

      // Preload all the materials.
      mCell->preloadMaterials();

      mZoningDirty = true;
      SceneZoneSpaceManager::getZoningChangedSignal().notify(this, &TerrainBlock::_onZoningChanged);
   }
   else
      mCRC = terr.getChecksum();
}

bool TerrainBlock::setTerrainAsset(const StringTableEntry terrainAssetId)
{
   if (TerrainAsset::getAssetById(terrainAssetId, &mTerrainAsset))
   {
      //Special exception case. If we've defaulted to the 'no shape' mesh, don't save it out, we'll retain the original ids/paths so it doesn't break
      //the TSStatic
      if (!mTerrainAsset.isNull())
      {
         mTerrFileName = StringTable->EmptyString();
      }

      setFile(mTerrainAsset->getTerrainResource());

      setMaskBits(-1);

      return true;
   }

   return false;
}

bool TerrainBlock::save(const char *filename)
{
   return mFile->save(filename);
}

bool TerrainBlock::saveAsset()
{
   if (!mTerrainAsset.isNull() && mTerrainAsset->isAssetValid())
   {
      mTerrainAsset->clearAssetDependencyFields("terrainMaterailAsset");

      AssetQuery* pAssetQuery = new AssetQuery();
      pAssetQuery->registerObject();

      AssetDatabase.findAssetType(pAssetQuery, "TerrainMaterialAsset");

      TerrainBlock* terr = static_cast<TerrainBlock*>(getClientObject());
      if (!terr)
      {
         Con::warnf("No active client terrain while trying to save asset. Could be a server action, but should check to be sure!");
         terr = this;
      }

      for (U32 i = 0; i < pAssetQuery->mAssetList.size(); i++)
      {
         //Acquire it so we can check it for matches
         AssetPtr<TerrainMaterialAsset> terrMatAsset = pAssetQuery->mAssetList[i];

         for (U32 m = 0; m < terr->mFile->mMaterials.size(); m++)
         {
            StringTableEntry intMatName = terr->mFile->mMaterials[m]->getInternalName();

            StringTableEntry assetMatDefName = terrMatAsset->getMaterialDefinitionName();
            if (assetMatDefName == intMatName)
            {
               mTerrainAsset->addAssetDependencyField("terrainMaterailAsset", terrMatAsset.getAssetId());
            }
         }

         terrMatAsset.clear();
      }

      pAssetQuery->destroySelf();

      bool saveAssetSuccess = mTerrainAsset->saveAsset();

      if (!saveAssetSuccess)
         return false;

      return mFile->save(mTerrainAsset->getTerrainFilePath());
   }

   return false;
}

bool TerrainBlock::_setTerrainFile( void *obj, const char *index, const char *data )
{
   //TerrainBlock* terrain = static_cast<TerrainBlock*>( obj )->setFile( FileName( data ) );
   TerrainBlock* terrain = static_cast<TerrainBlock*>(obj);

   StringTableEntry file = StringTable->insert(data);

   if (file != StringTable->EmptyString())
   {
      StringTableEntry assetId = TerrainAsset::getAssetIdByFilename(file);
      if (assetId != StringTable->EmptyString())
      {
         if (terrain->setTerrainAsset(assetId))
         {
            terrain->mTerrainAssetId = assetId;
            terrain->mTerrFileName = StringTable->EmptyString();

            return false;
         }
      }
      else
      {
         terrain->mTerrainAsset = StringTable->EmptyString();
      }
   }

   return true;
}

bool TerrainBlock::_setTerrainAsset(void* obj, const char* index, const char* data)
{
   TerrainBlock* terr = static_cast<TerrainBlock*>(obj);// ->setFile(FileName(data));

   terr->mTerrainAssetId = StringTable->insert(data);

   return terr->setTerrainAsset(terr->mTerrainAssetId);
}

void TerrainBlock::_updateBounds()
{
   if ( !mFile )
      return; // quick fix to stop crashing when deleting terrainblocks

   // Setup our object space bounds.
   mBounds.minExtents.set( 0.0f, 0.0f, 0.0f );
   mBounds.maxExtents.set( getWorldBlockSize(), getWorldBlockSize(), 0.0f );
   getMinMaxHeight( &mBounds.minExtents.z, &mBounds.maxExtents.z );

   // Set our mObjBox to be equal to mBounds
   if (  mObjBox.maxExtents != mBounds.maxExtents || 
	      mObjBox.minExtents != mBounds.minExtents )
   {
      mObjBox = mBounds;
      resetWorldBox();
   }
}

void TerrainBlock::_onZoningChanged( SceneZoneSpaceManager *zoneManager )
{
   const SceneManager* sm = getSceneManager();

   if (mCell == NULL || (sm != NULL && sm->getZoneManager() != NULL && zoneManager != sm->getZoneManager()))
      return;

   mZoningDirty = true;
}

void TerrainBlock::setHeight( const Point2I &pos, F32 height )
{
   U16 ht = floatToFixed( height );
   mFile->setHeight( pos.x, pos.y, ht );

   // Note: We do not update the grid here as this could
   // be called several times in a loop.  We depend on the
   // caller doing a grid update when he is done.
}

F32 TerrainBlock::getHeight( const Point2I &pos )
{
   U16 ht = mFile->getHeight( pos.x, pos.y );
   return fixedToFloat( ht );
}

void TerrainBlock::updateGridMaterials( const Point2I &minPt, const Point2I &maxPt )
{
   if ( mCell )
   {
      // Tell the terrain cell that something changed.
      const RectI gridRect( minPt, maxPt - minPt );
      mCell->updateGrid( gridRect, true );
   }

   // We mark us as dirty... it will be updated
   // before the next time we render the terrain.
   mLayerTexDirty = true;

   // Signal anyone that cares that the opacity was changed.
   smUpdateSignal.trigger( LayersUpdate, this, minPt, maxPt );
}


Point2I TerrainBlock::getGridPos( const Point3F &worldPos ) const
{
   Point3F terrainPos = worldPos;
   getWorldTransform().mulP( terrainPos );

   F32 squareSize = ( F32 ) getSquareSize();
   F32 halfSquareSize = squareSize / 2.0;

   F32 x = ( terrainPos.x + halfSquareSize ) / squareSize;
   F32 y = ( terrainPos.y + halfSquareSize ) / squareSize;

   Point2I gridPos( ( S32 ) mFloor( x ), ( S32 ) mFloor( y ) );
   return gridPos;
}

void TerrainBlock::updateGrid( const Point2I &minPt, const Point2I &maxPt, bool updateClient )
{
   // On the client we just signal everyone that the height
   // map has changed... the server does the actual changes.
   if ( isClientObject() )
   {
      PROFILE_SCOPE( TerrainBlock_updateGrid_Client );

      // This depends on the client getting this call 'after' the server.
      // Which is currently the case.
      _updateBounds();
      mZoningDirty = true;

      smUpdateSignal.trigger( HeightmapUpdate, this, minPt, maxPt );

      // Tell the terrain cell that the height changed.
      const RectI gridRect( minPt, maxPt - minPt );
      mCell->updateGrid( gridRect );

      // Rebuild the physics representation.
      if ( mPhysicsRep )
      {
         // Delay the update by a few milliseconds so 
         // that we're not rebuilding during an active
         // editing operation.
         mPhysicsRep->queueCallback( 500, Delegate<void()>( this, &TerrainBlock::_updatePhysics ) );
      }

      return;
   }

   // Now on the server we rebuild the 
   // affected area of the grid map.
   mFile->updateGrid( minPt, maxPt );

   // Fix up the bounds.
   _updateBounds();

   // Rebuild the physics representation.
   if ( mPhysicsRep )
   {
      // Delay the update by a few milliseconds so 
      // that we're not rebuilding during an active
      // editing operation.
      mPhysicsRep->queueCallback( 500, Delegate<void()>( this, &TerrainBlock::_updatePhysics ) );
   }

   // Signal again here for any server side observers.
   smUpdateSignal.trigger( HeightmapUpdate, this, minPt, maxPt );

   // If this is a server object and the client update
   // was requested then try to use the local connection
   // pointer to do it.
   if ( updateClient && getClientObject() )
      ((TerrainBlock*)getClientObject())->updateGrid( minPt, maxPt, false );
}

bool TerrainBlock::getHeight( const Point2F &pos, F32 *height ) const
{
	PROFILE_SCOPE( TerrainBlock_getHeight );

   F32 invSquareSize = 1.0f / mSquareSize;
   F32 xp = pos.x * invSquareSize;
   F32 yp = pos.y * invSquareSize;
   S32 x = S32(xp);
   S32 y = S32(yp);
   xp -= (F32)x;
   yp -= (F32)y;

   const U32 blockMask = mFile->mSize - 1;

   if ( x & ~blockMask || y & ~blockMask )
      return false;
   
   x &= blockMask;
   y &= blockMask;
   
   const TerrainSquare *sq = mFile->findSquare( 0, x, y );
   if ( sq->flags & TerrainSquare::Empty )
      return false;

   F32 zBottomLeft = fixedToFloat( mFile->getHeight( x, y ) );
   F32 zBottomRight = fixedToFloat( mFile->getHeight( x + 1, y ) );
   F32 zTopLeft = fixedToFloat( mFile->getHeight( x, y + 1 ) );
   F32 zTopRight = fixedToFloat( mFile->getHeight( x + 1, y + 1 ) );

   if ( sq->flags & TerrainSquare::Split45 )
   {
      if (xp>yp)
         // bottom half
         *height = zBottomLeft + xp * (zBottomRight-zBottomLeft) + yp * (zTopRight-zBottomRight);
      else
         // top half
         *height = zBottomLeft + xp * (zTopRight-zTopLeft) + yp * (zTopLeft-zBottomLeft);
   }
   else
   {
      if (1.0f-xp>yp)
         // bottom half
         *height = zBottomRight + (1.0f-xp) * (zBottomLeft-zBottomRight) + yp * (zTopLeft-zBottomLeft);
      else
         // top half
         *height = zBottomRight + (1.0f-xp) * (zTopLeft-zTopRight) + yp * (zTopRight-zBottomRight);
   }

   return true;
}

bool TerrainBlock::getNormal( const Point2F &pos, Point3F *normal, bool normalize, bool skipEmpty ) const
{
	PROFILE_SCOPE( TerrainBlock_getNormal );

   F32 invSquareSize = 1.0f / mSquareSize;
   F32 xp = pos.x * invSquareSize;
   F32 yp = pos.y * invSquareSize;
   S32 x = S32(xp);
   S32 y = S32(yp);
   xp -= (F32)x;
   yp -= (F32)y;
   
   const U32 blockMask = mFile->mSize - 1;

   if ( x & ~blockMask || y & ~blockMask )
      return false;
   
   x &= blockMask;
   y &= blockMask;
   
   const TerrainSquare *sq = mFile->findSquare( 0, x, y );
   if ( skipEmpty && sq->flags & TerrainSquare::Empty )
      return false;

   F32 zBottomLeft = fixedToFloat( mFile->getHeight( x, y ) );
   F32 zBottomRight = fixedToFloat( mFile->getHeight( x + 1, y ) );
   F32 zTopLeft = fixedToFloat( mFile->getHeight( x, y + 1 ) );
   F32 zTopRight = fixedToFloat( mFile->getHeight( x + 1, y + 1 ) );

   if ( sq->flags & TerrainSquare::Split45 )
   {
      if (xp>yp)
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomRight-zTopRight, mSquareSize);
      else
         // top half
         normal->set(zTopLeft-zTopRight, zBottomLeft-zTopLeft, mSquareSize);
   }
   else
   {
      if (1.0f-xp>yp)
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomLeft-zTopLeft, mSquareSize);
      else
         // top half
         normal->set(zTopLeft-zTopRight, zBottomRight-zTopRight, mSquareSize);
   }

   if (normalize)
      normal->normalize();

   return true;
}

bool TerrainBlock::getSmoothNormal( const Point2F &pos, 
                                    Point3F *normal, 
                                    bool normalize,
                                    bool skipEmpty ) const
{
	PROFILE_SCOPE( TerrainBlock_getSmoothNormal );

   F32 invSquareSize = 1.0f / mSquareSize;
   F32 xp = pos.x * invSquareSize;
   F32 yp = pos.y * invSquareSize;
   S32 x = S32(xp);
   S32 y = S32(yp);
   
   const U32 blockMask = mFile->mSize - 1;

   if ( x & ~blockMask || y & ~blockMask )
      return false;
   
   x &= blockMask;
   y &= blockMask;
   
   const TerrainSquare *sq = mFile->findSquare( 0, x, y );
   if ( skipEmpty && sq->flags & TerrainSquare::Empty )
      return false;

   F32 h1 = fixedToFloat( mFile->getHeight( x + 1, y ) );
   F32 h2 = fixedToFloat( mFile->getHeight( x, y + 1 ) );
   F32 h3 = fixedToFloat( mFile->getHeight( x - 1, y ) );
   F32 h4 = fixedToFloat( mFile->getHeight( x, y - 1 ) );

   normal->set( h3 - h1, h4 - h2, mSquareSize * 2.0f );

   if ( normalize )
      normal->normalize();

   return true;
}

bool TerrainBlock::getNormalAndHeight( const Point2F &pos, Point3F *normal, F32 *height, bool normalize ) const
{
	PROFILE_SCOPE( TerrainBlock_getNormalAndHeight );

   F32 invSquareSize = 1.0f / mSquareSize;
   F32 xp = pos.x * invSquareSize;
   F32 yp = pos.y * invSquareSize;
   S32 x = S32(xp);
   S32 y = S32(yp);
   xp -= (F32)x;
   yp -= (F32)y;

   const U32 blockMask = mFile->mSize - 1;

   if ( x & ~blockMask || y & ~blockMask )
      return false;
   
   x &= blockMask;
   y &= blockMask;
   
   const TerrainSquare *sq = mFile->findSquare( 0, x, y );
   if ( sq->flags & TerrainSquare::Empty )
      return false;

   F32 zBottomLeft  = fixedToFloat( mFile->getHeight(x, y) );
   F32 zBottomRight = fixedToFloat( mFile->getHeight(x + 1, y) );
   F32 zTopLeft     = fixedToFloat( mFile->getHeight(x, y + 1) );
   F32 zTopRight    = fixedToFloat( mFile->getHeight(x + 1, y + 1) );

   if ( sq->flags & TerrainSquare::Split45 )
   {
      if (xp>yp)
      {
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomRight-zTopRight, mSquareSize);
         *height = zBottomLeft + xp * (zBottomRight-zBottomLeft) + yp * (zTopRight-zBottomRight);
      }
      else
      {
         // top half
         normal->set(zTopLeft-zTopRight, zBottomLeft-zTopLeft, mSquareSize);
         *height = zBottomLeft + xp * (zTopRight-zTopLeft) + yp * (zTopLeft-zBottomLeft);
      }
   }
   else
   {
      if (1.0f-xp>yp)
      {
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomLeft-zTopLeft, mSquareSize);
         *height = zBottomRight + (1.0f-xp) * (zBottomLeft-zBottomRight) + yp * (zTopLeft-zBottomLeft);
      }
      else
      {
         // top half
         normal->set(zTopLeft-zTopRight, zBottomRight-zTopRight, mSquareSize);
         *height = zBottomRight + (1.0f-xp) * (zTopLeft-zTopRight) + yp * (zTopRight-zBottomRight);
      }
   }

   if (normalize)
      normal->normalize();

   return true;
}


bool TerrainBlock::getNormalHeightMaterial(  const Point2F &pos, 
                                             Point3F *normal, 
                                             F32 *height, 
                                             StringTableEntry &matName ) const
{
	PROFILE_SCOPE( TerrainBlock_getNormalHeightMaterial );

   F32 invSquareSize = 1.0f / mSquareSize;
   F32 xp = pos.x * invSquareSize;
   F32 yp = pos.y * invSquareSize;
   S32 x = S32(xp);
   S32 y = S32(yp);
   S32 xm = S32(mFloor( xp + 0.5f ));   
   S32 ym = S32(mFloor( yp + 0.5f ));   
   xp -= (F32)x;
   yp -= (F32)y;

   const U32 blockMask = mFile->mSize - 1;

   if ( x & ~blockMask || y & ~blockMask )
      return false;
   
   x &= blockMask;
   y &= blockMask;
   
   const TerrainSquare *sq = mFile->findSquare( 0, x, y );
   if ( sq->flags & TerrainSquare::Empty )
      return false;

   F32 zBottomLeft  = fixedToFloat( mFile->getHeight(x, y) );
   F32 zBottomRight = fixedToFloat( mFile->getHeight(x + 1, y) );
   F32 zTopLeft     = fixedToFloat( mFile->getHeight(x, y + 1) );
   F32 zTopRight    = fixedToFloat( mFile->getHeight(x + 1, y + 1) );

   matName = mFile->getMaterialName( xm, ym );

   if ( sq->flags & TerrainSquare::Split45 )
   {
      if (xp>yp)
      {
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomRight-zTopRight, mSquareSize);
         *height = zBottomLeft + xp * (zBottomRight-zBottomLeft) + yp * (zTopRight-zBottomRight);
      }
      else
      {
         // top half
         normal->set(zTopLeft-zTopRight, zBottomLeft-zTopLeft, mSquareSize);
         *height = zBottomLeft + xp * (zTopRight-zTopLeft) + yp * (zTopLeft-zBottomLeft);
      }
   }
   else
   {
      if (1.0f-xp>yp)
      {
         // bottom half
         normal->set(zBottomLeft-zBottomRight, zBottomLeft-zTopLeft, mSquareSize);
         *height = zBottomRight + (1.0f-xp) * (zBottomLeft-zBottomRight) + yp * (zTopLeft-zBottomLeft);
      }
      else
      {
         // top half
         normal->set(zTopLeft-zTopRight, zBottomRight-zTopRight, mSquareSize);
         *height = zBottomRight + (1.0f-xp) * (zTopLeft-zTopRight) + yp * (zTopRight-zBottomRight);
      }
   }

   normal->normalize();

   return true;
}

U32 TerrainBlock::getMaterialCount() const
{
   return mFile->mMaterials.size();
}

void TerrainBlock::addMaterial( const String &name, U32 insertAt )
{
   TerrainMaterial *mat = TerrainMaterial::findOrCreate( name );

   StringTableEntry newMatName = StringTable->insert(name.c_str());

   if ( insertAt == -1 )
   {
      //Check to ensure we're not trying to add one that already exists, as that'd be kinda dumb
      for (U32 i = 0; i < mFile->mMaterials.size(); i++)
      {
         if (mFile->mMaterials[i]->getInternalName() == newMatName)
            return;
      }

      mFile->mMaterials.push_back( mat );
      mFile->_initMaterialInstMapping();

      //now we update our asset
      if (mTerrainAsset)
      {
         StringTableEntry terrMatName = StringTable->insert(name.c_str());

         AssetQuery* aq = new AssetQuery();
         U32 foundCount = AssetDatabase.findAssetType(aq, "TerrainMaterialAsset");

         for (U32 i = 0; i < foundCount; i++)
         {
            TerrainMaterialAsset* terrMatAsset = AssetDatabase.acquireAsset<TerrainMaterialAsset>(aq->mAssetList[i]);
            if (terrMatAsset && terrMatAsset->getMaterialDefinitionName() == terrMatName)
            {
               //Do iterative logic to find the next available slot and write to it with our new mat field
               mTerrainAsset->setDataField(StringTable->insert("terrainMaterialAsset"), nullptr, aq->mAssetList[i]);
            }
         }
      }
   }
   else
   {

      // TODO: Insert and reindex!        

   }

   mDetailsDirty = true;
   mLayerTexDirty = true;
}

void TerrainBlock::removeMaterial( U32 index )
{
   // Cannot delete if only one layer.
   if ( mFile->mMaterials.size() == 1 )
      return;

   mFile->mMaterials.erase( index );
   mFile->_initMaterialInstMapping();

   for ( S32 i = 0; i < mFile->mLayerMap.size(); i++ )
   {
      if ( mFile->mLayerMap[i] >= index &&
           mFile->mLayerMap[i] != 0 )
      {
            mFile->mLayerMap[i]--;
      }
   }

   mDetailsDirty = true;
   mLayerTexDirty = true;     
}

void TerrainBlock::updateMaterial( U32 index, const String &name )
{
   if ( index >= mFile->mMaterials.size() )
      return;

   mFile->mMaterials[ index ] = TerrainMaterial::findOrCreate( name );
   mFile->_initMaterialInstMapping();

   mDetailsDirty = true;
   mLayerTexDirty = true;
}

TerrainMaterial* TerrainBlock::getMaterial( U32 index ) const
{
   if ( index >= mFile->mMaterials.size() )
      return NULL;

   return mFile->mMaterials[ index ];
}

void TerrainBlock::deleteAllMaterials()
{
   mFile->mMaterials.clear();
   mFile->mMaterialInstMapping.clearMatInstList();
}

const char* TerrainBlock::getMaterialName( U32 index ) const
{
   if ( index < mFile->mMaterials.size() )
      return mFile->mMaterials[ index ]->getInternalName();

   return NULL;
}

void TerrainBlock::setLightMap( GBitmap *newLightMap )
{
   SAFE_DELETE( mLightMap );
   mLightMap = newLightMap;
   mLightMapTex = NULL;
}

void TerrainBlock::clearLightMap()
{
   if ( !mLightMap )
      mLightMap = new GBitmap( mLightMapSize, mLightMapSize, 0, GFXFormatR8G8B8 );

   mLightMap->fillWhite();
   mLightMapTex = NULL;
}

GFXTextureObject* TerrainBlock::getLightMapTex()
{
   if ( mLightMapTex.isNull() && mLightMap )
   {
      mLightMapTex.set( mLightMap, 
                        &GFXStaticTextureProfile, 
                        false, 
                        "TerrainBlock::getLightMapTex()" );
   }

   return mLightMapTex;
}

void TerrainBlock::onEditorEnable()
{
}

void TerrainBlock::onEditorDisable()
{
}

bool TerrainBlock::onAdd()
{
   if(!Parent::onAdd())
      return false;

   Resource<TerrainFile> terr;

   if (!mTerrainAsset.isNull())
   {
      terr = mTerrainAsset->getTerrainResource();

      if (terr == NULL)
      {
         if (isClientObject())
            NetConnection::setLastError("Unable to load terrain asset: %s", mTerrainAsset.getAssetId());
         return false;
      }

      setFile(terr);
   }
 
   addToScene();

   _updatePhysics();

   return true;
}

String TerrainBlock::_getBaseTexCacheFileName() const
{
   Torque::Path basePath( mTerrainAsset->getTerrainFilePath() );
   basePath.setFileName( basePath.getFileName() + "_basetex" );
   basePath.setExtension( formatToExtension(mBaseTexFormat) );
   return basePath.getFullPath();
}

void TerrainBlock::_rebuildQuadtree()
{
   SAFE_DELETE( mCell );

   // Recursively build the cells.
   mCell = TerrCell::init( this );

   // Build the shared PrimitiveBuffer.
   mCell->createPrimBuffer( &mPrimBuffer );
   deleteZodiacPrimitiveBuffer();
}

void TerrainBlock::_updatePhysics()
{
   if ( !PHYSICSMGR )
      return;

   SAFE_DELETE( mPhysicsRep );

   PhysicsCollision *colShape = NULL;

   // If we can steal the collision shape from the local server
   // object then do so as it saves us alot of cpu time and memory.
   //
   // TODO: We should move this sharing down into TerrFile where
   // it probably belongs.
   //
   if ( getServerObject() )
   {
      TerrainBlock *serverTerrain = (TerrainBlock*)getServerObject();
      colShape = serverTerrain->mPhysicsRep->getColShape();
   }
   else
   {
      if (getBlockSize() > 0)
      {
         // Get empty state of each vert
         bool* holes = new bool[getBlockSize() * getBlockSize()];
         for (U32 row = 0; row < getBlockSize(); row++)
            for (U32 column = 0; column < getBlockSize(); column++)
               holes[row + (column * getBlockSize())] = mFile->isEmptyAt(row, column);

         colShape = PHYSICSMGR->createCollision();
         colShape->addHeightfield(mFile->getHeightMap().address(), holes, getBlockSize(), mSquareSize, MatrixF::Identity);

         delete[] holes;
      }
   }
   if (getBlockSize() > 0)
   {
      PhysicsWorld* world = PHYSICSMGR->getWorld(isServerObject() ? "server" : "client");
      mPhysicsRep = PHYSICSMGR->createBody();
      mPhysicsRep->init(colShape, 0, 0, this, world);
      mPhysicsRep->setTransform(getTransform());
   }
   else
   {
      SAFE_DELETE(mPhysicsRep);
   }
}

void TerrainBlock::onRemove()
{
   removeFromScene();
   SceneZoneSpaceManager::getZoningChangedSignal().remove( this, &TerrainBlock::_onZoningChanged );

   SAFE_DELETE( mPhysicsRep );

   if ( isClientObject() )
   {
      mBaseTex = NULL;
      mLayerTex = NULL;
      SAFE_DELETE( mBaseMaterial );
      SAFE_DELETE( mDefaultMatInst );
      SAFE_DELETE( mCell );
      mPrimBuffer = NULL;
      mBaseShader = NULL;
      GFXTextureManager::removeEventDelegate( this, &TerrainBlock::_onTextureEvent );
      MATMGR->getFlushSignal().remove( this, &TerrainBlock::_onFlushMaterials );
   }

   Parent::onRemove();
}

void TerrainBlock::prepRenderImage( SceneRenderState* state )
{
   PROFILE_SCOPE(TerrainBlock_prepRenderImage);
   
   // If we need to update our cached 
   // zone state then do it now.
   if ( mZoningDirty )
   {
      mZoningDirty = false;
      mCell->updateZoning( getSceneManager()->getZoneManager() );
   }

   _renderBlock( state );
}

void TerrainBlock::setTransform(const MatrixF & mat)
{
   Parent::setTransform( mat );

   // Update world-space OBBs.
   if( mCell )
   {
      mCell->updateOBBs();
      mZoningDirty = true;
   }

   if ( mPhysicsRep )
      mPhysicsRep->setTransform( mat );

   setRenderTransform( mat );
   setMaskBits( TransformMask );

   if(isClientObject())
      smUpdateSignal.trigger( HeightmapUpdate, this, Point2I::Zero, Point2I::Max );
}

void TerrainBlock::setScale( const VectorF &scale )
{
   // We disable scaling... we never scale!
   Parent::setScale( VectorF::One );   
}

void TerrainBlock::initPersistFields()
{
   docsURL;
   addGroup( "Media" );

      addProtectedField("terrainAsset", TypeTerrainAssetId, Offset(mTerrainAssetId, TerrainBlock),
         &TerrainBlock::_setTerrainAsset, &defaultProtectedGetFn,
         "The source terrain data asset.");

      addProtectedField( "terrainFile", TypeStringFilename, Offset( mTerrFileName, TerrainBlock ), 
         &TerrainBlock::_setTerrainFile, &defaultProtectedGetFn,
         "The source terrain data file." );

   endGroup( "Media" );

   addGroup( "Misc" );

      addField( "castShadows", TypeBool, Offset( mCastShadows, TerrainBlock ),   
         "Allows the terrain to cast shadows onto itself and other objects."); 
   
      addProtectedField( "squareSize", TypeF32, Offset( mSquareSize, TerrainBlock ),
         &TerrainBlock::_setSquareSize, &defaultProtectedGetFn,
         "Indicates the spacing between points on the XY plane on the terrain." );

      addProtectedField( "baseTexSize", TypeS32, Offset( mBaseTexSize, TerrainBlock ),
         &TerrainBlock::_setBaseTexSize, &defaultProtectedGetFn,
         "Size of base texture size per meter." );

      addProtectedField("baseTexFormat", TYPEID<baseTexFormat>(), Offset(mBaseTexFormat, TerrainBlock),
         &TerrainBlock::_setBaseTexFormat, &defaultProtectedGetFn,
         "");

      addProtectedField( "lightMapSize", TypeS32, Offset( mLightMapSize, TerrainBlock ),
         &TerrainBlock::_setLightMapSize, &defaultProtectedGetFn,
         "Light map dimensions in pixels." );

      addField( "screenError", TypeS32, Offset( mScreenError, TerrainBlock ), "Not yet implemented." );
	
      addField( "updateBasetex", TypeBool, Offset( mUpdateBasetex, TerrainBlock ), "Whether or not to update the Base Texture" );

   endGroup( "Misc" );

   addGroup("AFX");
   addField("ignoreZodiacs",     TypeBool,      Offset(mIgnoreZodiacs,    TerrainBlock));
   endGroup("AFX");
   Parent::initPersistFields();

   removeField( "scale" );

   Con::addVariable( "$TerrainBlock::debugRender", TypeBool, &smDebugRender, "Triggers debug rendering of terrain cells\n\n"
	   "@ingroup Terrain");
   
   Con::addVariable( "$pref::Terrain::lodScale", TypeF32, &smLODScale, "A global LOD scale used to tweak the default terrain screen error value.\n\n"
		"@ingroup Terrain");

   Con::addVariable( "$pref::Terrain::detailScale", TypeF32, &smDetailScale, "A global detail scale used to tweak the material detail distances.\n\n" 
	   "@ingroup Terrain");
}

void TerrainBlock::inspectPostApply()
{
   Parent::inspectPostApply();
   setMaskBits( MiscMask );
}

U32 TerrainBlock::packUpdate(NetConnection* con, U32 mask, BitStream *stream)
{
   U32 retMask = Parent::packUpdate( con, mask, stream );
   
   if ( stream->writeFlag( mask & TransformMask ) )
      mathWrite( *stream, getTransform() );

   if ( stream->writeFlag( mask & SizeMask ) )
      stream->write( mSquareSize );

   stream->writeFlag( mCastShadows );
   
   if ( stream->writeFlag( mask & MaterialMask ) )
   {
      stream->write( mBaseTexSize );
      stream->write( mLightMapSize );
   }

   if ( stream->writeFlag( mask & FileMask ) )
   {
      stream->write(mCRC);
      stream->writeString( mTerrainAsset.getAssetId() );
   }

   stream->writeFlag( mask & HeightMapChangeMask );

   if ( stream->writeFlag( mask & MiscMask ) )
      stream->write( mScreenError );

   stream->writeInt(mBaseTexFormat, 3);
	
   stream->writeFlag(mUpdateBasetex);
   stream->writeFlag(mIgnoreZodiacs);

   return retMask;
}

void TerrainBlock::unpackUpdate(NetConnection* con, BitStream *stream)
{
   Parent::unpackUpdate( con, stream );
   
   if ( stream->readFlag() ) // TransformMask
   {
      MatrixF mat;
      mathRead( *stream, &mat );
      setTransform( mat );
   }


   if ( stream->readFlag() ) // SizeMask
      stream->read( &mSquareSize );

   mCastShadows = stream->readFlag();

   bool baseTexSizeChanged = false;
   if ( stream->readFlag() ) // MaterialMask
   {
      U32 baseTexSize;
      stream->read( &baseTexSize );
      if ( mBaseTexSize != baseTexSize )
      {
         mBaseTexSize = baseTexSize;
         baseTexSizeChanged = true;
      }

      U32 lightMapSize;
      stream->read( &lightMapSize );
      if ( mLightMapSize != lightMapSize )
      {
         mLightMapSize = lightMapSize;
         if ( isProperlyAdded() )
         {
            SAFE_DELETE( mLightMap );
            clearLightMap();
         }
      }
   }

   if (stream->readFlag()) // FileMask
   {
      stream->read(&mCRC);

      char buffer[256];
      stream->readString(buffer);
      setTerrainAsset(StringTable->insert(buffer));
   }
   if (baseTexSizeChanged && isProperlyAdded())
      _updateBaseTexture(NONE);

   if ( stream->readFlag() && isProperlyAdded() ) // HeightMapChangeMask
   {
      _updateBounds();
      _rebuildQuadtree();
      _updatePhysics();
      mDetailsDirty = true;
      mLayerTexDirty = true;
   }

   if ( stream->readFlag() ) // MiscMask
      stream->read( &mScreenError );

   mBaseTexFormat = (BaseTexFormat)stream->readInt(3);
	
   mUpdateBasetex = stream->readFlag();
   mIgnoreZodiacs = stream->readFlag();
}

void TerrainBlock::getMinMaxHeight( F32 *minHeight, F32 *maxHeight ) const 
{
   // We can get the bound height from the last grid level.
   const TerrainSquare *sq = mFile->findSquare( mFile->mGridLevels, 0, 0 );
   *minHeight = fixedToFloat( sq->minHeight );
   *maxHeight = fixedToFloat( sq->maxHeight );
}

void TerrainBlock::getUtilizedAssets(Vector<StringTableEntry>* usedAssetsList)
{
   if (!mTerrainAsset.isNull())
      usedAssetsList->push_back_unique(mTerrainAsset->getAssetId());
}
//-----------------------------------------------------------------------------
// Console Methods
//-----------------------------------------------------------------------------

bool TerrainBlock::renameTerrainMaterial(StringTableEntry oldMatName, StringTableEntry newMatName)
{
   TerrainMaterial* newMat = TerrainMaterial::findOrCreate(newMatName);
   if (!newMat)
      return false;

   U32 terrainMaterialCount = mFile->mMaterials.size();
   for (U32 i = 0; i < terrainMaterialCount; i++)
   {
      if (mFile->mMaterials[i]->getInternalName() == oldMatName)
      {
         mFile->mMaterials[i] = newMat;
      }
   }

   return true;
}

DefineEngineMethod( TerrainBlock, save, bool, ( const char* fileName),,
				   "@brief Saves the terrain block's terrain file to the specified file name.\n\n"

				   "@param fileName Name and path of file to save terrain data to.\n\n"

				   "@return True if file save was successful, false otherwise")
{
	char filename[256];
	dStrcpy(filename,fileName,256);
   char *ext = dStrrchr(filename, '.');
   if (!ext || dStricmp(ext, ".ter") != 0)
      dStrcat(filename, ".ter", 256);
   return static_cast<TerrainBlock*>(object)->save(filename);
}

DefineEngineMethod(TerrainBlock, saveAsset, bool, (), ,
   "@brief Saves the terrain block's terrain file to the specified file name.\n\n"

   "@param fileName Name and path of file to save terrain data to.\n\n"

   "@return True if file save was successful, false otherwise")
{
   return static_cast<TerrainBlock*>(object)->saveAsset();
}

DefineEngineMethod( TerrainBlock, setMaterialsDirty, void, (),, "")
{
   static_cast<TerrainBlock*>(object)->setMaterialsDirty();
}

//ConsoleMethod(TerrainBlock, save, bool, 3, 3, "(string fileName) - saves the terrain block's terrain file to the specified file name.")
//{
//   char filename[256];
//   dStrcpy(filename,argv[2],256);
//   char *ext = dStrrchr(filename, '.');
//   if (!ext || dStricmp(ext, ".ter") != 0)
//      dStrcat(filename, ".ter", 256);
//   return static_cast<TerrainBlock*>(object)->save(filename);
//}

ConsoleDocFragment _getTerrainHeight1(
   "@brief Gets the terrain height at the specified position\n\n"
   "@param position The world space point, minus the z (height) value. Formatted as (\"x y\")\n\n"
   "@return Returns the terrain height at the given point as an F32 value.\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainHeight( Point2I position );"
);
ConsoleDocFragment _getTerrainHeight2(
   "@brief Gets the terrain height at the specified position\n\n"
   "@param x The X coordinate in world space\n"
   "@param y The Y coordinate in world space\n\n"
   "@return Returns the terrain height at the given point as an F32 value.\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainHeight( F32 x, F32 y);"
);

DefineEngineFunction( getTerrainHeight, F32, (const char* ptOrX, const char* y), (""), "(Point2 pos) - gets the terrain height at the specified position."
				"@param pos The world space point, minus the z (height) value\n Can be formatted as either (\"x y\") or (x,y)\n"
				"@return Returns the terrain height at the given point as an F32 value.\n"
				"@hide")
{
   F32 height = 0.0f;

   Point2F pos;
   if(!String::isEmpty(ptOrX) && String::isEmpty(y))
      dSscanf(ptOrX, "%f %f", &pos.x, &pos.y);
   else if(!String::isEmpty(ptOrX) && !String::isEmpty(y))
   {
      pos.x = dAtof(ptOrX);
      pos.y = dAtof(y);
   }

   TerrainBlock * terrain = getTerrainUnderWorldPoint(Point3F(pos.x, pos.y, 5000.0f));
   if(terrain && terrain->isServerObject())
   {
      Point3F offset;
      terrain->getTransform().getColumn(3, &offset);
      pos -= Point2F(offset.x, offset.y);
      terrain->getHeight(pos, &height);
   }
   return height;
}

ConsoleDocFragment _getTerrainHeightBelowPosition1(
   "@brief Takes a world point and find the \"highest\" terrain underneath it\n\n"
   "@param position The world space point, minus the z (height) value. Formatted as (\"x y\")\n\n"
   "@return Returns the closest terrain height below the given point as an F32 value.\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainHeightBelowPosition( Point2I position );"
);
ConsoleDocFragment _getTerrainHeightBelowPosition2(
   "@brief Takes a world point and find the \"highest\" terrain underneath it\n\n"
   "@param x The X coordinate in world space\n"
   "@param y The Y coordinate in world space\n\n"
   "@return Returns the closest terrain height below the given point as an F32 value.\n\n"
   "@ingroup Terrain",
   NULL,
   "bool getTerrainHeightBelowPosition( F32 x, F32 y);"
);

DefineEngineFunction( getTerrainHeightBelowPosition, F32, (const char* ptOrX, const char* y, const char* z), ("", ""),
            "(Point3F pos) - gets the terrain height at the specified position."
				"@param pos The world space point. Can be formatted as either (\"x y z\") or (x,y,z)\n"
				"@note This function is useful if you simply want to grab the terrain height underneath an object.\n"
				"@return Returns the terrain height at the given point as an F32 value.\n"
				"@hide")
{
	F32 height = 0.0f;

   Point3F pos;
   if(!String::isEmpty(ptOrX) && String::isEmpty(y) && String::isEmpty(z))
      dSscanf(ptOrX, "%f %f %f", &pos.x, &pos.y, &pos.z);
   else if(!String::isEmpty(ptOrX) && !String::isEmpty(y) && !String::isEmpty(z))
   {
      pos.x = dAtof(ptOrX);
      pos.y = dAtof(y);
      pos.z = dAtof(z);
   }

	TerrainBlock * terrain = getTerrainUnderWorldPoint(pos);
	
	Point2F nohghtPos(pos.x, pos.y);

	if(terrain)
	{
		if(terrain->isServerObject())
		{
			Point3F offset;
			terrain->getTransform().getColumn(3, &offset);
			nohghtPos -= Point2F(offset.x, offset.y);
			terrain->getHeight(nohghtPos, &height);
		}
	}
	
	return height;
}
const U16* TerrainBlock::getZodiacPrimitiveBuffer()
{ 
   if (!zode_primBuffer && !mIgnoreZodiacs)
      TerrCell::createZodiacPrimBuffer(&zode_primBuffer);
   return zode_primBuffer;
}

void TerrainBlock::deleteZodiacPrimitiveBuffer()
{
   if (zode_primBuffer != 0)
   {
      delete [] zode_primBuffer;
      zode_primBuffer = 0;
   }
}

DefineEngineMethod(TerrainBlock, getTerrain, String, (), , "Returns the terrain file used for this terrain block, either via the asset or the filename assigned, which ever is valid")
{
   return object->getTerrain(); 
}
DefineEngineMethod(TerrainBlock, getTerrainAsset, String, (), , "Returns the assetId used for this terrain block")
{
   return object->getTerrainAssetId();
}
DefineEngineMethod(TerrainBlock, setTerrain, bool, (const char* terrain), , "Terrain assignment.first tries asset then flat file.")
{
   return object->_setTerrain(StringTable->insert(terrain));
}

DefineEngineMethod(TerrainBlock, getTerrainMaterialCount, S32, (), , "Gets the number of terrain materials for this block")
{
   return object->getTerrainMaterialCount();
}

DefineEngineMethod(TerrainBlock, getTerrainMaterialName, const char*, (S32 index), , "Gets the number of terrain materials for this block")
{
   if (index < 0 || index >= object->getTerrainMaterialCount())
      return StringTable->EmptyString();

   return object->getTerrainMaterialName(index);
}

DefineEngineMethod(TerrainBlock, renameTerrainMaterial, bool, (const char* oldMaterialName, const char* newMaterialName), , "Updates the terrain material from the original to the new name in the file. Mostly used for import/conversions.")
{
   return object->renameTerrainMaterial(StringTable->insert(oldMaterialName), StringTable->insert(newMaterialName));
}
