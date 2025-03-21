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
#include "renderInstance/renderDeferredMgr.h"

#include "gfx/gfxTransformSaver.h"
#include "materials/sceneData.h"
#include "materials/materialManager.h"
#include "materials/materialFeatureTypes.h"
#include "core/util/safeDelete.h"
#include "shaderGen/featureMgr.h"
#include "shaderGen/HLSL/depthHLSL.h"
#include "shaderGen/GLSL/depthGLSL.h"
#include "shaderGen/conditionerFeature.h"
#include "shaderGen/shaderGenVars.h"
#include "scene/sceneRenderState.h"
#include "gfx/gfxStringEnumTranslate.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxCardProfile.h"
#include "materials/customMaterialDefinition.h"
#include "lighting/advanced/advancedLightManager.h"
#include "lighting/advanced/advancedLightBinManager.h"
#include "terrain/terrCell.h"
#include "renderInstance/renderTerrainMgr.h"
#include "terrain/terrCellMaterial.h"
#include "math/mathUtils.h"
#include "math/util/matrixSet.h"
#include "gfx/gfxTextureManager.h"
#include "gfx/primBuilder.h"
#include "gfx/gfxDrawUtil.h"
#include "materials/shaderData.h"
#include "gfx/sim/cubemapData.h"

#include "materials/customShaderBindingData.h"

const MatInstanceHookType DeferredMatInstanceHook::Type( "Deferred" );
const String RenderDeferredMgr::BufferName("deferred");
const RenderInstType RenderDeferredMgr::RIT_Deferred("Deferred");
const String RenderDeferredMgr::ColorBufferName("color");
const String RenderDeferredMgr::MatInfoBufferName("matinfo");

IMPLEMENT_CONOBJECT(RenderDeferredMgr);

ConsoleDocClass( RenderDeferredMgr, 
   "@brief The render bin which performs a z+normals deferred used in Advanced Lighting.\n\n"
   "This render bin is used in Advanced Lighting to gather all opaque mesh render instances "
   "and render them to the g-buffer for use in lighting the scene and doing effects.\n\n"
   "PostEffect and other shaders can access the output of this bin by using the #deferred "
   "texture target name.  See the edge anti-aliasing post effect for an example.\n\n"
   "@see game/core/scripts/client/postFx/edgeAA." TORQUE_SCRIPT_EXTENSION "\n"
   "@ingroup RenderBin\n" );


RenderDeferredMgr::RenderSignal& RenderDeferredMgr::getRenderSignal()
{
   static RenderSignal theSignal;
   return theSignal;
}


RenderDeferredMgr::RenderDeferredMgr( bool gatherDepth,
                                    GFXFormat format )
   :  Parent(  RIT_Deferred,
               0.01f,
               0.01f,
               format,
               Point2I( Parent::DefaultTargetSize, Parent::DefaultTargetSize),
               gatherDepth ? Parent::DefaultTargetChainLength : 0 ),
      mDeferredMatInstance( NULL )
{
   notifyType( RenderPassManager::RIT_Decal );
   notifyType( RenderPassManager::RIT_DecalRoad );
   notifyType( RenderPassManager::RIT_Mesh );
   notifyType( RenderPassManager::RIT_Terrain );
   notifyType( RenderPassManager::RIT_Object );
   notifyType( RenderPassManager::RIT_Probes );

   // We want a full-resolution buffer
   mTargetSizeType = RenderTexTargetBinManager::WindowSize;

   if(getTargetChainLength() > 0)
      GFXShader::addGlobalMacro( "TORQUE_LINEAR_DEPTH" );

   mNamedTarget.registerWithName( BufferName );
   mColorTarget.registerWithName( ColorBufferName );
   mMatInfoTarget.registerWithName( MatInfoBufferName );

   _registerFeatures();
}

RenderDeferredMgr::~RenderDeferredMgr()
{
   GFXShader::removeGlobalMacro( "TORQUE_LINEAR_DEPTH" );

   mColorTarget.release();
   mMatInfoTarget.release();
   _unregisterFeatures();
   SAFE_DELETE( mDeferredMatInstance );
}

void RenderDeferredMgr::_registerFeatures()
{
   ConditionerFeature *cond = new LinearEyeDepthConditioner( getTargetFormat() );
   FEATUREMGR->registerFeature( MFT_DeferredConditioner, cond );
   mNamedTarget.setConditioner( cond );
}

void RenderDeferredMgr::_unregisterFeatures()
{
   mNamedTarget.setConditioner( NULL );
   FEATUREMGR->unregisterFeature(MFT_DeferredConditioner);
}

bool RenderDeferredMgr::setTargetSize(const Point2I &newTargetSize)
{
   bool ret = Parent::setTargetSize( newTargetSize );
   mNamedTarget.setViewport( GFX->getViewport() );
   mColorTarget.setViewport( GFX->getViewport() );
   mMatInfoTarget.setViewport( GFX->getViewport() );
   return ret;
}

bool RenderDeferredMgr::_updateTargets()
{
   PROFILE_SCOPE(RenderDeferredMgr_updateTargets);

   bool ret = Parent::_updateTargets();

   // check for an output conditioner, and update it's format
   ConditionerFeature *outputConditioner = dynamic_cast<ConditionerFeature *>(FEATUREMGR->getByType(MFT_DeferredConditioner));
   if( outputConditioner && outputConditioner->setBufferFormat(mTargetFormat) )
   {
      // reload materials, the conditioner needs to alter the generated shaders
   }

   // TODO: these formats should be passed in and not hard-coded
   const GFXFormat colorFormat = GFXFormatR8G8B8A8_SRGB;
   const GFXFormat matInfoFormat = GFXFormatR8G8B8A8;

   // andrewmac: Deferred Shading Color Buffer
   if (mColorTex.getFormat() != colorFormat || mColorTex.getWidthHeight() != mTargetSize || GFX->recentlyReset())
   {
      mColorTarget.release();
      mColorTex.set(mTargetSize.x, mTargetSize.y, colorFormat,
         &GFXRenderTargetSRGBProfile, avar("%s() - (line %d)", __FUNCTION__, __LINE__),
         1, GFXTextureManager::AA_MATCH_BACKBUFFER);
      mColorTarget.setTexture(mColorTex);
 
      for (U32 i = 0; i < mTargetChainLength; i++)
         mTargetChain[i]->attachTexture(GFXTextureTarget::Color1, mColorTarget.getTexture());
   }
 
   // andrewmac: Deferred Shading Material Info Buffer
   if (mMatInfoTex.getFormat() != matInfoFormat || mMatInfoTex.getWidthHeight() != mTargetSize || GFX->recentlyReset())
   {
      mMatInfoTarget.release();
      mMatInfoTex.set(mTargetSize.x, mTargetSize.y, matInfoFormat,
         &GFXRenderTargetProfile, avar("%s() - (line %d)", __FUNCTION__, __LINE__),
         1, GFXTextureManager::AA_MATCH_BACKBUFFER);
      mMatInfoTarget.setTexture(mMatInfoTex);
 
      for (U32 i = 0; i < mTargetChainLength; i++)
         mTargetChain[i]->attachTexture(GFXTextureTarget::Color2, mMatInfoTarget.getTexture());
   }

   //scene color target
   NamedTexTargetRef sceneColorTargetRef = NamedTexTarget::find("AL_FormatToken");
   if (sceneColorTargetRef.isValid())
   {
      for (U32 i = 0; i < mTargetChainLength; i++)
         mTargetChain[i]->attachTexture(GFXTextureTarget::Color3, sceneColorTargetRef->getTexture(0));
   }
   else
   {
      Con::errorf("RenderDeferredMgr: Could not find AL_FormatToken");
      return false;
   }

   GFX->finalizeReset();

   return ret;
}

void RenderDeferredMgr::_createDeferredMaterial()
{
   SAFE_DELETE(mDeferredMatInstance);

   const GFXVertexFormat *vertexFormat = getGFXVertexFormat<GFXVertexPNTTB>();

   MatInstance* deferredMat = static_cast<MatInstance*>(MATMGR->createMatInstance("AL_DefaultDeferredMaterial", vertexFormat));
   AssertFatal( deferredMat, "TODO: Handle this better." );
   mDeferredMatInstance = new DeferredMatInstance(deferredMat, this);
   mDeferredMatInstance->init( MATMGR->getDefaultFeatures(), vertexFormat);
   delete deferredMat;
}

void RenderDeferredMgr::setDeferredMaterial( DeferredMatInstance *mat )
{
   SAFE_DELETE(mDeferredMatInstance);
   mDeferredMatInstance = mat;
}

void RenderDeferredMgr::addElement( RenderInst *inst )
{
   PROFILE_SCOPE( RenderDeferredMgr_addElement )

   // Skip out if this bin is disabled.
   if (  gClientSceneGraph->getCurrentRenderState() &&
         gClientSceneGraph->getCurrentRenderState()->disableAdvancedLightingBins() )
      return;

   // First what type of render instance is it?
   const bool isDecalMeshInst = ((inst->type == RenderPassManager::RIT_Decal)||(inst->type == RenderPassManager::RIT_DecalRoad));

   const bool isMeshInst = inst->type == RenderPassManager::RIT_Mesh;

   const bool isTerrainInst = inst->type == RenderPassManager::RIT_Terrain;

   const bool isProbeInst = inst->type == RenderPassManager::RIT_Probes;

   // Get the material if its a mesh.
   BaseMatInstance* matInst = NULL;
   if ( isMeshInst || isDecalMeshInst )
      matInst = static_cast<MeshRenderInst*>(inst)->matInst;

   if (matInst)
   {
      // If its a custom material and it refracts... skip it.
      if (matInst->isCustomMaterial() &&
         static_cast<CustomMaterial*>(matInst->getMaterial())->mRefract)
         return;

      // Make sure we got a deferred material.
      matInst = getDeferredMaterial(matInst);
      if (!matInst || !matInst->isValid())
         return;
   }

   // We're gonna add it to the bin... get the right element list.
   Vector< MainSortElem > *elementList;
   if ( isMeshInst || isDecalMeshInst )
      elementList = &mElementList;
   else if ( isTerrainInst )
      elementList = &mTerrainElementList;
   else if (isProbeInst)
      elementList = &mProbeElementList;
   else
      elementList = &mObjectElementList;

   elementList->increment();
   MainSortElem &elem = elementList->last();
   elem.inst = inst;

   // Store the original key... we might need it.
   U32 originalKey = elem.key;

   // Sort front-to-back first to get the most fillrate savings.
   const F32 invSortDistSq = F32_MAX - inst->sortDistSq;
   elem.key = *((U32*)&invSortDistSq);

   // Next sort by pre-pass material if its a mesh... use the original sort key.
   if (isMeshInst && matInst)
      elem.key2 = matInst->getStateHint();
   else
      elem.key2 = originalKey;
}

void RenderDeferredMgr::sort()
{
   PROFILE_SCOPE( RenderDeferredMgr_sort );
   Parent::sort();
   dQsort( mTerrainElementList.address(), mTerrainElementList.size(), sizeof(MainSortElem), cmpKeyFunc);
   dQsort( mObjectElementList.address(), mObjectElementList.size(), sizeof(MainSortElem), cmpKeyFunc);
}

void RenderDeferredMgr::clear()
{
   Parent::clear();
   mProbeElementList.clear();
   mTerrainElementList.clear();
   mObjectElementList.clear();
}

void RenderDeferredMgr::render( SceneRenderState *state )
{
   PROFILE_SCOPE(RenderDeferredMgr_render);

   // Take a look at the SceneRenderState and see if we should skip drawing the pre-pass
   if ( state->disableAdvancedLightingBins() )
      return;

   // NOTE: We don't early out here when the element list is
   // zero because we need the deferred to be cleared.

   // Automagically save & restore our viewport and transforms.
   GFXTransformSaver saver;

   GFXDEBUGEVENT_SCOPE( RenderDeferredMgr_Render, ColorI::RED );

   // Tell the superclass we're about to render
   const bool isRenderingToTarget = _onPreRender(state);

   // Clear z-buffer and g-buffer.
   GFX->clear(GFXClearZBuffer | GFXClearStencil, LinearColorF::ZERO, 0.0f, 0);
   GFX->clearColorAttachment(0, LinearColorF::ONE);//normdepth
   GFX->clearColorAttachment(1, LinearColorF::ZERO);//albedo
   GFX->clearColorAttachment(2, LinearColorF::ZERO);//matinfo
   //AL_FormatToken is cleared by it's own class

   // Restore transforms
   MatrixSet &matrixSet = getRenderPass()->getMatrixSet();
   matrixSet.restoreSceneViewProjection();
   const MatrixF worldViewXfm = GFX->getWorldMatrix();

   // Setup the default deferred material for object instances.
   if ( !mDeferredMatInstance )
      _createDeferredMaterial();
   if ( mDeferredMatInstance )
   {
      matrixSet.setWorld(MatrixF::Identity);
      mDeferredMatInstance->setTransforms(matrixSet, state);
   }

   // Signal start of deferred
   getRenderSignal().trigger( state, this, true );
   
   // First do a loop and render all the terrain... these are 
   // usually the big blockers in a scene and will save us fillrate
   // on the smaller meshes and objects.

   // The terrain doesn't need any scene graph data
   // in the the deferred... so just clear it.
   SceneData sgData;
   sgData.init( state, SceneData::DeferredBin );

   Vector< MainSortElem >::const_iterator itr = mTerrainElementList.begin();
   for ( ; itr != mTerrainElementList.end(); itr++ )
   {
      TerrainRenderInst *ri = static_cast<TerrainRenderInst*>( itr->inst );

      TerrainCellMaterial *mat = ri->cellMat->getDeferredMat();

      GFX->setPrimitiveBuffer( ri->primBuff );
      GFX->setVertexBuffer( ri->vertBuff );

      mat->setTransformAndEye(   *ri->objectToWorldXfm,
                                 worldViewXfm,
                                 GFX->getProjectionMatrix(),
                                 state->getFarPlane() );

      while ( mat->setupPass( state, sgData ) )
         GFX->drawPrimitive( ri->prim );
   }

   // init loop data
   GFXTextureObject *lastLM = NULL;
   GFXCubemap *lastCubemap = NULL;
   GFXTextureObject *lastReflectTex = NULL;
   GFXTextureObject *lastAccuTex = NULL;
   
   // Next render all the meshes.
   itr = mElementList.begin();
   for ( ; itr != mElementList.end(); )
   {
      MeshRenderInst *ri = static_cast<MeshRenderInst*>( itr->inst );

      // Get the deferred material.
      BaseMatInstance *mat = getDeferredMaterial( ri->matInst );

      // Set up SG data proper like and flag it 
      // as a pre-pass render
      setupSGData( ri, sgData );

      Vector< MainSortElem >::const_iterator meshItr, endOfBatchItr = itr;

      while ( mat->setupPass( state, sgData ) )
      {
         meshItr = itr;
         for ( ; meshItr != mElementList.end(); meshItr++ )
         {
            MeshRenderInst *passRI = static_cast<MeshRenderInst*>( meshItr->inst );

            // Check to see if we need to break this batch.
            //
            // NOTE: We're comparing the non-deferred materials 
            // here so we don't incur the cost of looking up the 
            // deferred hook on each inst.
            //
            if ( newPassNeeded( ri, passRI ) )
               break;

            // Set up SG data for this instance.
            setupSGData( passRI, sgData );
            mat->setSceneInfo(state, sgData);

            matrixSet.setWorld(*passRI->objectToWorld);
            matrixSet.setView(*passRI->worldToCamera);
            matrixSet.setProjection(*passRI->projection);
            mat->setTransforms(matrixSet, state);

            // Setup HW skinning transforms if applicable
            if (mat->usesHardwareSkinning())
            {
               mat->setNodeTransforms(passRI->mNodeTransforms, passRI->mNodeTransformCount);
            }

			   //push along any overriden fields that are instance-specific as well
			   if (passRI->mCustomShaderData.size() > 0)
			   {
				   mat->setCustomShaderData(passRI->mCustomShaderData);
			   }

            // If we're instanced then don't render yet.
            if ( mat->isInstanced() )
            {
               // Let the material increment the instance buffer, but
               // break the batch if it runs out of room for more.
               if ( !mat->stepInstance() )
               {
                  meshItr++;
                  break;
               }

               continue;
            }

            bool dirty = false;

            // set the lightmaps if different
            if( passRI->lightmap && passRI->lightmap != lastLM )
            {
               sgData.lightmap = passRI->lightmap;
               lastLM = passRI->lightmap;
               dirty = true;
            }

            // set the cubemap if different.
            if ( passRI->cubemap != lastCubemap )
            {
               sgData.cubemap = passRI->cubemap;
               lastCubemap = passRI->cubemap;
               dirty = true;
            }

            if ( passRI->reflectTex != lastReflectTex )
            {
               sgData.reflectTex = passRI->reflectTex;
               lastReflectTex = passRI->reflectTex;
               dirty = true;
            }
            
            // Update accumulation texture if it changed.
            // Note: accumulation texture can be NULL, and must be updated.
            if (passRI->accuTex != lastAccuTex)
            {
               sgData.accuTex = passRI->accuTex;
               lastAccuTex = passRI->accuTex;
               dirty = true;
            }

            if ( dirty )
               mat->setTextureStages( state, sgData );

            // Setup the vertex and index buffers.
            mat->setBuffers( passRI->vertBuff, passRI->primBuff );

            // Render this sucker.
            if ( passRI->prim )
               GFX->drawPrimitive( *passRI->prim );
            else
               GFX->drawPrimitive( passRI->primBuffIndex );
         }

         // Draw the instanced batch.
         if ( mat->isInstanced() )
         {
            // Sets the buffers including the instancing stream.
            mat->setBuffers( ri->vertBuff, ri->primBuff );

            if ( ri->prim )
               GFX->drawPrimitive( *ri->prim );
            else
               GFX->drawPrimitive( ri->primBuffIndex );
         }

         endOfBatchItr = meshItr;

      } // while( mat->setupPass(state, sgData) )

      // Force the increment if none happened, otherwise go to end of batch.
      itr = ( itr == endOfBatchItr ) ? itr + 1 : endOfBatchItr;
   }

   // The final loop is for object render instances.
   itr = mObjectElementList.begin();
   for ( ; itr != mObjectElementList.end(); itr++ )
   {
      ObjectRenderInst *ri = static_cast<ObjectRenderInst*>( itr->inst );
      if ( ri->renderDelegate )
         ri->renderDelegate( ri, state, mDeferredMatInstance );
   }

   // Signal end of pre-pass
   getRenderSignal().trigger( state, this, false );

   if(isRenderingToTarget)
      _onPostRender();
}

const GFXStateBlockDesc & RenderDeferredMgr::getOpaqueStenciWriteDesc( bool lightmappedGeometry /*= true*/ )
{
   static bool sbInit = false;
   static GFXStateBlockDesc sOpaqueStaticLitStencilWriteDesc;
   static GFXStateBlockDesc sOpaqueDynamicLitStencilWriteDesc;

   if(!sbInit)
   {
      sbInit = true;

      // Build the static opaque stencil write/test state block descriptions
      sOpaqueStaticLitStencilWriteDesc.stencilDefined = true;
      sOpaqueStaticLitStencilWriteDesc.stencilEnable = true;
      sOpaqueStaticLitStencilWriteDesc.stencilWriteMask = 0x03;
      sOpaqueStaticLitStencilWriteDesc.stencilMask = 0x03;
      sOpaqueStaticLitStencilWriteDesc.stencilRef = RenderDeferredMgr::OpaqueStaticLitMask;
      sOpaqueStaticLitStencilWriteDesc.stencilPassOp = GFXStencilOpReplace;
      sOpaqueStaticLitStencilWriteDesc.stencilFailOp = GFXStencilOpKeep;
      sOpaqueStaticLitStencilWriteDesc.stencilZFailOp = GFXStencilOpKeep;
      sOpaqueStaticLitStencilWriteDesc.stencilFunc = GFXCmpAlways;

      // Same only dynamic
      sOpaqueDynamicLitStencilWriteDesc = sOpaqueStaticLitStencilWriteDesc;
      sOpaqueDynamicLitStencilWriteDesc.stencilRef = RenderDeferredMgr::OpaqueDynamicLitMask;
   }

   return (lightmappedGeometry ? sOpaqueStaticLitStencilWriteDesc : sOpaqueDynamicLitStencilWriteDesc);
}

const GFXStateBlockDesc & RenderDeferredMgr::getOpaqueStencilTestDesc()
{
   static bool sbInit = false;
   static GFXStateBlockDesc sOpaqueStencilTestDesc;

   if(!sbInit)
   {
      // Build opaque test
      sbInit = true;
      sOpaqueStencilTestDesc.stencilDefined = true;
      sOpaqueStencilTestDesc.stencilEnable = true;
      sOpaqueStencilTestDesc.stencilWriteMask = 0xFE;
      sOpaqueStencilTestDesc.stencilMask = 0x03;
      sOpaqueStencilTestDesc.stencilRef = 0;
      sOpaqueStencilTestDesc.stencilPassOp = GFXStencilOpKeep;
      sOpaqueStencilTestDesc.stencilFailOp = GFXStencilOpKeep;
      sOpaqueStencilTestDesc.stencilZFailOp = GFXStencilOpKeep;
      sOpaqueStencilTestDesc.stencilFunc = GFXCmpLess;
   }

   return sOpaqueStencilTestDesc;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------


ProcessedDeferredMaterial::ProcessedDeferredMaterial( Material& mat, const RenderDeferredMgr *deferredMgr )
: Parent(mat), mDeferredMgr(deferredMgr), mIsLightmappedGeometry(false)
{

}

void ProcessedDeferredMaterial::_determineFeatures( U32 stageNum,
                                                   MaterialFeatureData &fd,
                                                   const FeatureSet &features )
{
   if (GFX->getAdapterType() == NullDevice) return;
   Parent::_determineFeatures( stageNum, fd, features );
   if (fd.features.hasFeature(MFT_ForwardShading))
      return;

   // Find this for use down below...
   bool bEnableMRTLightmap = false;
   AdvancedLightBinManager *lightBin;
   if ( Sim::findObject( "AL_LightBinMgr", lightBin ) )
      bEnableMRTLightmap = lightBin->MRTLightmapsDuringDeferred();

   // If this material has a lightmap or tonemap (texture or baked vertex color),
   // it must be static. Otherwise it is dynamic.
   mIsLightmappedGeometry = ( fd.features.hasFeature( MFT_ToneMap ) ||
                              fd.features.hasFeature( MFT_LightMap ) ||
                              fd.features.hasFeature( MFT_VertLit ) ||
                              ( bEnableMRTLightmap && (fd.features.hasFeature( MFT_IsTranslucent ) ||
                                                      fd.features.hasFeature( MFT_ForwardShading ) ||
                                                      fd.features.hasFeature( MFT_IsTranslucentZWrite) ) ) );

   // Integrate proper opaque stencil write state
   mUserDefined.addDesc( mDeferredMgr->getOpaqueStenciWriteDesc( mIsLightmappedGeometry ) );

   FeatureSet newFeatures;

   // These are always on for deferred.
   newFeatures.addFeature( MFT_EyeSpaceDepthOut );
   newFeatures.addFeature( MFT_DeferredConditioner );

#ifndef TORQUE_DEDICATED

   //tag all materials running through deferred as deferred
   newFeatures.addFeature(MFT_isDeferred);

   // Deferred Shading : Diffuse
   if (mStages[stageNum].getTex( MFT_DiffuseMap ))
   {
      newFeatures.addFeature(MFT_DiffuseMap);
   }
   newFeatures.addFeature( MFT_DiffuseColor );
   
   if (mMaterial->mInvertRoughness[stageNum])
      newFeatures.addFeature(MFT_InvertRoughness);

   // Deferred Shading : PBR Config
   if( mStages[stageNum].getTex( MFT_OrmMap ) )
   {
       newFeatures.addFeature( MFT_OrmMap );
   }
   else
       newFeatures.addFeature( MFT_ORMConfigVars );

   if (mStages[stageNum].getTex(MFT_GlowMap))
   {
      newFeatures.addFeature(MFT_GlowMap);
   }

   // Deferred Shading : Material Info Flags
   newFeatures.addFeature( MFT_MatInfoFlags );

   for ( U32 i=0; i < fd.features.getCount(); i++ )
   {
      const FeatureType &type = fd.features.getAt( i );

      // Turn on the diffuse texture only if we
      // have alpha test.
      if ( type == MFT_AlphaTest )
      {
         newFeatures.addFeature( MFT_AlphaTest );
         newFeatures.addFeature( MFT_DiffuseMap );
      }

      else if ( type == MFT_IsTranslucentZWrite )
      {
         newFeatures.addFeature( MFT_IsTranslucentZWrite );
         newFeatures.addFeature( MFT_DiffuseMap );
      }

      // Always allow these.
      else if (   type == MFT_IsBC3nm ||
                  type == MFT_IsBC5nm ||
                  type == MFT_TexAnim ||
                  type == MFT_NormalMap ||
                  type == MFT_DetailNormalMap ||
                  type == MFT_AlphaTest ||
                  type == MFT_Parallax ||
                  type == MFT_Visibility ||
                  type == MFT_UseInstancing ||
                  type == MFT_DiffuseVertColor ||
                  type == MFT_DetailMap ||
                  type == MFT_DiffuseMapAtlas||
                  type == MFT_GlowMask)
         newFeatures.addFeature( type );

      // Add any transform features.
      else if (   type.getGroup() == MFG_PreTransform ||
                  type.getGroup() == MFG_Transform ||
                  type.getGroup() == MFG_PostTransform )
         newFeatures.addFeature( type );
   }

   if (mMaterial->mAccuEnabled[stageNum])
   {
      newFeatures.addFeature(MFT_AccuMap);
      mHasAccumulation = true;
   }

   // we need both diffuse and normal maps + sm3 to have an accu map
   if (newFeatures[MFT_AccuMap] &&
      (!newFeatures[MFT_DiffuseMap] ||
      !newFeatures[MFT_NormalMap] ||
      GFX->getPixelShaderVersion() < 3.0f)) {
      AssertWarn(false, "SAHARA: Using an Accu Map requires SM 3.0 and a normal map.");
      newFeatures.removeFeature(MFT_AccuMap);
      mHasAccumulation = false;
   }

   // if we still have the AccuMap feature, we add all accu constant features
   if (newFeatures[MFT_AccuMap]) {
      // add the dependencies of the accu map
      newFeatures.addFeature(MFT_AccuScale);
      newFeatures.addFeature(MFT_AccuDirection);
      newFeatures.addFeature(MFT_AccuStrength);
      newFeatures.addFeature(MFT_AccuCoverage);
      newFeatures.addFeature(MFT_AccuSpecular);
      // now remove some features that are not compatible with this
      newFeatures.removeFeature(MFT_UseInstancing);
   }

   // If there is lightmapped geometry support, add the MRT light buffer features
   if(bEnableMRTLightmap)
   {
      // If this material has a lightmap, pass it through, and flag it to
      // send it's output to RenderTarget3
      if( fd.features.hasFeature( MFT_ToneMap ) )
      {
         newFeatures.addFeature( MFT_ToneMap );
         newFeatures.addFeature( MFT_LightbufferMRT );
      }
      else if( fd.features.hasFeature( MFT_LightMap ) )
      {
         newFeatures.addFeature( MFT_LightMap );
         newFeatures.addFeature( MFT_LightbufferMRT );
      }
      else if( fd.features.hasFeature( MFT_VertLit ) )
      {
         // Flag un-tone-map if necesasary
         if( fd.features.hasFeature( MFT_DiffuseMap ) )
            newFeatures.addFeature( MFT_VertLitTone );

         newFeatures.addFeature( MFT_VertLit );
         newFeatures.addFeature( MFT_LightbufferMRT );
      }
      else if (!fd.features.hasFeature(MFT_GlowMap))
      {
            newFeatures.addFeature( MFT_RenderTarget3_Zero );
      }
   }

   // cubemaps only available on stage 0 for now - bramage   
   if ( stageNum < 1 && 
         (  (  mMaterial->mCubemapData && mMaterial->mCubemapData->mCubemap ) ||
               mMaterial->mDynamicCubemap ) )
   {
      if (!mMaterial->mDynamicCubemap)
         fd.features.addFeature(MFT_StaticCubemap);
      newFeatures.addFeature( MFT_CubeMap );
   }
   if (mMaterial->mVertLit[stageNum])
      newFeatures.addFeature(MFT_VertLit);

   if (mMaterial->mMinnaertConstant[stageNum] > 0.0f)
      newFeatures.addFeature(MFT_MinnaertShading);

   if (mMaterial->mSubSurface[stageNum])
      newFeatures.addFeature(MFT_SubSurface);
   
#endif

   // Set the new features.
   fd.features = newFeatures;
}

U32 ProcessedDeferredMaterial::getNumStages()
{
   // Loops through all stages to determine how many 
   // stages we actually use.  
   // 
   // The first stage is always active else we shouldn't be
   // creating the material to begin with.
   U32 numStages = 1;

   U32 i;
   for( i=1; i<Material::MAX_STAGES; i++ )
   {
      // Assume stage is inactive
      bool stageActive = false;

      // Cubemaps only on first stage
      if( i == 0 )
      {
         // If we have a cubemap the stage is active
         if( mMaterial->mCubemapData || mMaterial->mDynamicCubemap )
         {
            numStages++;
            continue;
         }
      }

      // If we have a texture for the a feature the 
      // stage is active.
      if ( mStages[i].hasValidTex() )
         stageActive = true;
      
      // If this stage has diffuse color, it's active
      if (  mMaterial->mDiffuse[i].alpha > 0 &&
            mMaterial->mDiffuse[i] != LinearColorF::WHITE )
         stageActive = true;

      // If we have a Material that is vertex lit
      // then it may not have a texture
      if( mMaterial->mVertLit[i] )
         stageActive = true;

      // Increment the number of active stages
      numStages += stageActive;
   }

   return numStages;
}

void ProcessedDeferredMaterial::addStateBlockDesc(const GFXStateBlockDesc& desc)
{
   GFXStateBlockDesc deferredStateBlock = desc;

   // Adjust color writes if this is a pure z-fill pass
   const bool pixelOutEnabled = mDeferredMgr->getTargetChainLength() > 0;
   if ( !pixelOutEnabled )
   {
      deferredStateBlock.colorWriteDefined = true;
      deferredStateBlock.colorWriteRed = pixelOutEnabled;
      deferredStateBlock.colorWriteGreen = pixelOutEnabled;
      deferredStateBlock.colorWriteBlue = pixelOutEnabled;
      deferredStateBlock.colorWriteAlpha = pixelOutEnabled;
   }

   // Never allow the alpha test state when rendering
   // the deferred as we use the alpha channel for the
   // depth information... MFT_AlphaTest will handle it.
   deferredStateBlock.alphaDefined = true;
   deferredStateBlock.alphaTestEnable = false;

   // If we're translucent then we're doing deferred blending
   // which never writes to the depth channels.
   const bool isTranslucent = getMaterial()->isTranslucent();
   if ( isTranslucent )
   {
      deferredStateBlock.setBlend( true, GFXBlendSrcAlpha, GFXBlendInvSrcAlpha );
      deferredStateBlock.setColorWrites(false, false, false, true);
   }

   // Enable z reads, but only enable zwrites if we're not translucent.
   deferredStateBlock.setZReadWrite( true, isTranslucent ? false : true );

   // Pass to parent
   Parent::addStateBlockDesc(deferredStateBlock);
}

DeferredMatInstance::DeferredMatInstance(MatInstance* root, const RenderDeferredMgr *deferredMgr)
: Parent(*root->getMaterial()), mDeferredMgr(deferredMgr)
{
   mFeatureList = root->getRequestedFeatures();
   mVertexFormat = root->getVertexFormat();
   mUserObject = root->getUserObject();
}

DeferredMatInstance::~DeferredMatInstance()
{
}

ProcessedMaterial* DeferredMatInstance::getShaderMaterial()
{
   return new ProcessedDeferredMaterial(*mMaterial, mDeferredMgr);
}

bool DeferredMatInstance::init( const FeatureSet &features,
                               const GFXVertexFormat *vertexFormat )
{
   bool vaild = Parent::init(features, vertexFormat);

   if (mMaterial && mMaterial->getDiffuseMap(0) != StringTable->EmptyString() && String(mMaterial->getDiffuseMap(0)).startsWith("#"))
   {
      String difName = mMaterial->getDiffuseMap(0);
      String texTargetBufferName = difName.substr(1, difName.length() - 1);
      NamedTexTarget *texTarget = NamedTexTarget::find(texTargetBufferName);
      RenderPassData* rpd = getPass(0);

      if (rpd)
      {
         rpd->mTexSlot[0].texTarget = texTarget;
         rpd->mTexType[0] = Material::TexTarget;
         rpd->mSamplerNames[0] = "diffuseMap";
      }
   }
   return vaild;
}

DeferredMatInstanceHook::DeferredMatInstanceHook( MatInstance *baseMatInst,
                                                const RenderDeferredMgr *deferredMgr )
   : mHookedDeferredMatInst(NULL), mDeferredManager(deferredMgr)
{
   // If the material is a custom material then
   // hope that using DefaultDeferredMaterial gives
   // them a good deferred.
   if ( baseMatInst->isCustomMaterial() )
   {
      MatInstance* dummyInst = static_cast<MatInstance*>( MATMGR->createMatInstance( "AL_DefaultDeferredMaterial", baseMatInst->getVertexFormat() ) );

      mHookedDeferredMatInst = new DeferredMatInstance( dummyInst, deferredMgr );
      mHookedDeferredMatInst->init( dummyInst->getRequestedFeatures(), baseMatInst->getVertexFormat());

      delete dummyInst;
      return;
   }

   // Create the deferred material instance.
   mHookedDeferredMatInst = new DeferredMatInstance(baseMatInst, deferredMgr);
   mHookedDeferredMatInst->getFeaturesDelegate() = baseMatInst->getFeaturesDelegate();

   // Get the features, but remove the instancing feature if the
   // original material didn't end up using it.
   FeatureSet features = baseMatInst->getRequestedFeatures();
   if ( !baseMatInst->isInstanced() )
      features.removeFeature( MFT_UseInstancing );

   // Initialize the material.
   mHookedDeferredMatInst->init(features, baseMatInst->getVertexFormat());
}

DeferredMatInstanceHook::~DeferredMatInstanceHook()
{
   SAFE_DELETE(mHookedDeferredMatInst);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void LinearEyeDepthConditioner::processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd )
{
   // find depth
   ShaderFeature *depthFeat = FEATUREMGR->getByType( MFT_EyeSpaceDepthOut );
   AssertFatal( depthFeat != NULL, "No eye space depth feature found!" );

   Var *depth = (Var*) LangElement::find(depthFeat->getOutputVarName());
   AssertFatal( depth, "Something went bad with ShaderGen. The depth should be already generated by the EyeSpaceDepthOut feature." );

   MultiLine *meta = new MultiLine;

   meta->addStatement( assignOutput( depth ) );

   output = meta;
}

Var *LinearEyeDepthConditioner::_conditionOutput( Var *unconditionedOutput, MultiLine *meta )
{
   Var *retVar = NULL;

   String fracMethodName = (GFX->getAdapterType() == OpenGL) ? "fract" : "frac";

   switch(getBufferFormat())
   {
   case GFXFormatR8G8B8A8:
      retVar = new Var;
      retVar->setType("float4");
      retVar->setName("_ppDepth");
      meta->addStatement( new GenOp( "   // depth conditioner: packing to rgba\r\n" ) );
      meta->addStatement( new GenOp(
         avar( "   @ = %s(@ * (255.0/256) * float4(1, 255, 255 * 255, 255 * 255 * 255));\r\n", fracMethodName.c_str() ),
         new DecOp(retVar), unconditionedOutput ) );
      break;
   default:
      retVar = unconditionedOutput;
      meta->addStatement( new GenOp( "   // depth conditioner: no conditioning\r\n" ) );
      break;
   }

   AssertFatal( retVar != NULL, avar( "Cannot condition output to buffer format: %s", GFXStringTextureFormat[getBufferFormat()] ) );
   return retVar;
}

Var *LinearEyeDepthConditioner::_unconditionInput( Var *conditionedInput, MultiLine *meta )
{
   String float4Typename = (GFX->getAdapterType() == OpenGL) ? "vec4" : "float4";

   Var *retVar = conditionedInput;
   if(getBufferFormat() != GFXFormat_COUNT)
   {
      retVar = new Var;
      retVar->setType(float4Typename.c_str());
      retVar->setName("_ppDepth");
      meta->addStatement( new GenOp( avar( "   @ = %s(0, 0, 1, 1);\r\n", float4Typename.c_str() ), new DecOp(retVar) ) );

      switch(getBufferFormat())
      {
      case GFXFormatR32F:
      case GFXFormatR16F:
         meta->addStatement( new GenOp( "   // depth conditioner: float texture\r\n" ) );
         meta->addStatement( new GenOp( "   @.w = @.r;\r\n", retVar, conditionedInput ) );
         break;

      case GFXFormatR8G8B8A8:
         meta->addStatement( new GenOp( "   // depth conditioner: unpacking from rgba\r\n" ) );
         meta->addStatement( new GenOp(
            avar( "   @.w = dot(@ * (256.0/255), %s(1, 1 / 255, 1 / (255 * 255), 1 / (255 * 255 * 255)));\r\n", float4Typename.c_str() )
            , retVar, conditionedInput ) );
         break;
      default:
         AssertFatal(false, "LinearEyeDepthConditioner::_unconditionInput - Unrecognized buffer format");
      }
   }

   return retVar;
}

Var* LinearEyeDepthConditioner::printMethodHeader( MethodType methodType, const String &methodName, Stream &stream, MultiLine *meta )
{
   const bool isCondition = ( methodType == ConditionerFeature::ConditionMethod );

   Var *retVal = NULL;

   // The uncondition method inputs are changed
   if( isCondition )
      retVal = Parent::printMethodHeader( methodType, methodName, stream, meta );
   else
   {
      Var *methodVar = new Var;
      methodVar->setName(methodName);
      if (GFX->getAdapterType() == OpenGL)
         methodVar->setType("vec4");
      else
         methodVar->setType("inline float4");
      DecOp *methodDecl = new DecOp(methodVar);

      Var *deferredSampler = new Var;
      deferredSampler->setName("deferredSamplerVar");
      deferredSampler->setType("sampler2D");
      DecOp *deferredSamplerDecl = NULL;

      Var *deferredTex = NULL;
      DecOp *deferredTexDecl = NULL;
      if (GFX->getAdapterType() == Direct3D11)
      {
         deferredSampler->setType("SamplerState");

         deferredTex = new Var;
         deferredTex->setName("deferredTexVar");
         deferredTex->setType("Texture2D");
         deferredTexDecl = new DecOp(deferredTex);
      }

      deferredSamplerDecl = new DecOp(deferredSampler);

      Var *screenUV = new Var;
      screenUV->setName("screenUVVar");
      if (GFX->getAdapterType() == OpenGL)
         screenUV->setType("vec2");
      else
         screenUV->setType("float2");
      DecOp *screenUVDecl = new DecOp(screenUV);

      Var *bufferSample = new Var;
      bufferSample->setName("bufferSample");
      if (GFX->getAdapterType() == OpenGL)
         bufferSample->setType("vec4");
      else
         bufferSample->setType("float4");
      DecOp *bufferSampleDecl = new DecOp(bufferSample);

      if (deferredTex)
         meta->addStatement(new GenOp("@(@, @, @)\r\n", methodDecl, deferredSamplerDecl, deferredTexDecl, screenUVDecl));
      else
         meta->addStatement(new GenOp("@(@, @)\r\n", methodDecl, deferredSamplerDecl, screenUVDecl));

      meta->addStatement(new GenOp("{\r\n"));

      meta->addStatement(new GenOp("   // Sampler g-buffer\r\n"));

      // The linear depth target has no mipmaps, so use tex2dlod when
      // possible so that the shader compiler can optimize.
      if (GFX->getAdapterType() == OpenGL)
         meta->addStatement(new GenOp("@ = texture2DLod(@, @, 0); \r\n", bufferSampleDecl, deferredSampler, screenUV));
      else
      {
         if (deferredTex)
            meta->addStatement(new GenOp("@ = @.SampleLevel(@, @, 0);\r\n", bufferSampleDecl, deferredTex, deferredSampler, screenUV));
         else
            meta->addStatement(new GenOp("@ = tex2Dlod(@, float4(@,0,0));\r\n", bufferSampleDecl, deferredSampler, screenUV));
      }

      // We don't use this way of passing var's around, so this should cause a crash
      // if something uses this improperly
      retVal = bufferSample;
   }

   return retVal;
}
