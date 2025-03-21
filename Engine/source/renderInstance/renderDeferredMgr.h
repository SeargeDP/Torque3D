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
#ifndef _DEFERRED_MGR_H_
#define _DEFERRED_MGR_H_

#include "renderInstance/renderTexTargetBinManager.h"
#include "materials/matInstance.h"
#include "materials/processedShaderMaterial.h"
#include "shaderGen/conditionerFeature.h"
#include "core/util/autoPtr.h"

// Forward declare
class DeferredMatInstance;

// This render manager renders opaque objects to the z-buffer as a z-fill pass.
// It can optionally accumulate data from this opaque render pass into a render
// target for later use.
class RenderDeferredMgr : public RenderTexTargetBinManager
{
   typedef RenderTexTargetBinManager Parent;

public:

   // registered buffer name
   static const String BufferName;

   // andremwac: Deferred Rendering
   static const String ColorBufferName;
   static const String MatInfoBufferName;

   // Generic Deferred Render Instance Type
   static const RenderInstType RIT_Deferred;

   RenderDeferredMgr( bool gatherDepth = true, 
                     GFXFormat format = GFXFormatR16G16B16A16 );

   virtual ~RenderDeferredMgr();

   virtual void setDeferredMaterial( DeferredMatInstance *mat );

   // RenderBinManager interface
   void render(SceneRenderState * state) override;
   void sort() override;
   void clear() override;
   void addElement( RenderInst *inst ) override;

   // ConsoleObject
   DECLARE_CONOBJECT(RenderDeferredMgr);


   typedef Signal<void(const SceneRenderState*, RenderDeferredMgr*, bool)> RenderSignal;

   static RenderSignal& getRenderSignal();  

   static const U32 OpaqueStaticLitMask = BIT(1);     ///< Stencil mask for opaque, lightmapped pixels
   static const U32 OpaqueDynamicLitMask = BIT(0);    ///< Stencil mask for opaque, dynamic lit pixels

   static const GFXStateBlockDesc &getOpaqueStencilTestDesc();
   static const GFXStateBlockDesc &getOpaqueStenciWriteDesc(bool lightmappedGeometry = true);

   bool setTargetSize(const Point2I &newTargetSize) override;

   inline BaseMatInstance* getDeferredMaterial( BaseMatInstance *mat );

protected:

   /// The terrain render instance elements.
   Vector< MainSortElem > mTerrainElementList;

   /// The object render instance elements.
   Vector< MainSortElem > mObjectElementList;

   DeferredMatInstance *mDeferredMatInstance;

   ///
   Vector< MainSortElem > mProbeElementList;

   virtual void _registerFeatures();
   virtual void _unregisterFeatures();
   bool _updateTargets() override;
   virtual void _createDeferredMaterial();

   bool _lightManagerActivate(bool active);

   // Deferred Shading
   NamedTexTarget                      mColorTarget;
   NamedTexTarget                      mMatInfoTarget;
   GFXTexHandle                        mColorTex;
   GFXTexHandle                        mMatInfoTex;
   GFXTexHandle                        mDiffuseLightTex;
   GFXTexHandle                        mSpecularLightTex;
   GFXShaderConstBufferRef             mShaderConsts; 
};

//------------------------------------------------------------------------------

class ProcessedDeferredMaterial : public ProcessedShaderMaterial
{
   typedef ProcessedShaderMaterial Parent;
   
public:   
   ProcessedDeferredMaterial(Material& mat, const RenderDeferredMgr *deferredMgr);

   U32 getNumStages() override;

   void addStateBlockDesc(const GFXStateBlockDesc& desc) override;

protected:
   void _determineFeatures( U32 stageNum, MaterialFeatureData &fd, const FeatureSet &features ) override;

   const RenderDeferredMgr *mDeferredMgr;
   bool mIsLightmappedGeometry;
};

//------------------------------------------------------------------------------

class DeferredMatInstance : public MatInstance
{
   typedef MatInstance Parent;

public:   
   DeferredMatInstance(MatInstance* root, const RenderDeferredMgr *deferredMgr);
   virtual ~DeferredMatInstance();

   bool init()
   {
      return init( mFeatureList, mVertexFormat );
   }   

   // MatInstance
   bool init(   const FeatureSet &features, 
                        const GFXVertexFormat *vertexFormat ) override;

protected:      
   ProcessedMaterial* getShaderMaterial() override;

   const RenderDeferredMgr *mDeferredMgr;
};

//------------------------------------------------------------------------------

class DeferredMatInstanceHook : public MatInstanceHook
{
public:
   DeferredMatInstanceHook(MatInstance *baseMatInst, const RenderDeferredMgr *deferredMgr);
   virtual ~DeferredMatInstanceHook();

   virtual DeferredMatInstance *getDeferredMatInstance() { return mHookedDeferredMatInst; }

   const MatInstanceHookType& getType() const override { return Type; }

   /// The type for deferred material hooks.
   static const MatInstanceHookType Type;

protected:
   DeferredMatInstance *mHookedDeferredMatInst; 
   const RenderDeferredMgr *mDeferredManager;
};

//------------------------------------------------------------------------------

// A very simple, default depth conditioner feature
class LinearEyeDepthConditioner : public ConditionerFeature
{
   typedef ConditionerFeature Parent;

public:
   LinearEyeDepthConditioner(const GFXFormat bufferFormat) 
      : Parent(bufferFormat)
   {

   }

   String getName() override
   {
      return "Linear Eye-Space Depth Conditioner";
   }

   void processPix( Vector<ShaderComponent*> &componentList, const MaterialFeatureData &fd ) override;
protected:
   Var *_conditionOutput( Var *unconditionedOutput, MultiLine *meta ) override;
   Var *_unconditionInput( Var *conditionedInput, MultiLine *meta ) override;

   Var *printMethodHeader( MethodType methodType, const String &methodName, Stream &stream, MultiLine *meta ) override;
};


inline BaseMatInstance* RenderDeferredMgr::getDeferredMaterial( BaseMatInstance *mat )
{
   DeferredMatInstanceHook *hook = static_cast<DeferredMatInstanceHook*>( mat->getHook( DeferredMatInstanceHook::Type ) );
   if ( !hook )
   {
      hook = new DeferredMatInstanceHook( static_cast<MatInstance*>( mat ), this );
      mat->addHook( hook );
   }

   return hook->getDeferredMatInstance();
}

#endif // _DEFERRED_MGR_H_

