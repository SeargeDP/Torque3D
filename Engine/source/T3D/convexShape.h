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

#ifndef _CONVEXSHAPE_H_
#define _CONVEXSHAPE_H_

#ifndef _SCENEOBJECT_H_
#include "scene/sceneObject.h"
#endif
#ifndef _GFXVERTEXBUFFER_H_
#include "gfx/gfxVertexBuffer.h"
#endif
#ifndef _GFXPRIMITIVEBUFFER_H_
#include "gfx/gfxPrimitiveBuffer.h"
#endif
#ifndef _CONVEX_H_
#include "collision/convex.h"
#endif

#include "T3D/assets/MaterialAsset.h"

class ConvexShape;

// Crap name, but whatcha gonna do.
class ConvexShapeCollisionConvex : public Convex
{
	typedef Convex Parent;
	friend class ConvexShape;

protected:

	ConvexShape *pShape;

public:

   ConvexShapeCollisionConvex() { pShape = NULL; mType = ConvexShapeCollisionConvexType; }

	ConvexShapeCollisionConvex( const ConvexShapeCollisionConvex& cv ) {
		mType      = ConvexShapeCollisionConvexType;
		mObject    = cv.mObject;
		pShape     = cv.pShape;		
	}

	Point3F support(const VectorF& vec) const override;
	void getFeatures(const MatrixF& mat,const VectorF& n, ConvexFeature* cf) override;
	void getPolyList(AbstractPolyList* list) override;
};


GFXDeclareVertexFormat( ConvexVert )
{
   Point3F point;
   GFXVertexColor color;
   Point3F normal;
   Point3F tangent;
   Point2F texCoord;
};

class PhysicsBody;


class ConvexShape : public SceneObject
{
   typedef SceneObject Parent;
   friend class GuiConvexEditorCtrl;
   friend class GuiConvexEditorUndoAction;
   friend class ConvexShapeCollisionConvex;

public:

   enum NetBits {
      TransformMask = Parent::NextFreeMask,
      UpdateMask = Parent::NextFreeMask << 1,
      NextFreeMask = Parent::NextFreeMask << 2
   };

   // Declaring these structs directly within ConvexShape to prevent
   // the otherwise excessively deep scoping we had.
   // eg. ConvexShape::Face::Triangle ...

   // Define our vertex format here so we don't have to
   // change it in multiple spots later
   typedef GFXVertexPNTTB VertexType;

   struct Edge
   {
      U32 p0;
      U32 p1;
   };

   struct Triangle
   {
      U32 p0;
      U32 p1;
      U32 p2;

      U32 operator [](U32 index) const
      {
         AssertFatal(index >= 0 && index <= 2, "index out of range");
         return *((&p0) + index);
      }
   };

   struct Face
   {
      Vector< Edge > edges;
      Vector< U32 > points;
      Vector< U32 > winding;
      Vector< Point2F > texcoords;
      Vector< Triangle > triangles;
      Point3F tangent;
      Point3F normal;
      Point3F centroid;
      F32 area;
      S32 id;
   };

   struct surfaceMaterial
   {
      // The name of the Material we will use for rendering
      DECLARE_MATERIALASSET(surfaceMaterial, Material);

      DECLARE_ASSET_SETGET(surfaceMaterial, Material);

      // The actual Material instance
      BaseMatInstance* materialInst;

      surfaceMaterial()
      {
         INIT_ASSET(Material);

         materialInst = NULL;
      }
   };

   struct surfaceUV
   {
      S32 matID;
      Point2F offset;
      Point2F scale;
      float   zRot;
      bool horzFlip;
      bool vertFlip;

      surfaceUV() : matID(0), offset(Point2F(0.0f, 0.0f)), scale(Point2F(1.0f, 1.0f)), zRot(0.0f), horzFlip(false), vertFlip(false) {}
   };

   struct surfaceBuffers
   {
      // The GFX vertex and primitive buffers
      GFXVertexBufferHandle< VertexType > mVertexBuffer;
      GFXPrimitiveBufferHandle            mPrimitiveBuffer;

      U32 mVertCount;
      U32 mPrimCount;
   };

   struct Geometry
   {
      void generate(const Vector< PlaneF >& planes, const Vector< Point3F >& tangents, const Vector< surfaceMaterial > surfaceTextures, const Vector< Point2F > texOffset, const Vector< Point2F > texScale, const Vector< bool > horzFlip, const Vector< bool > vertFlip);

      Vector< Point3F > points;
      Vector< Face > faces;
   };

   static bool smRenderEdges;

   // To prevent bitpack overflows.
   // This is only indirectly enforced by trucation when serializing.
   static const S32 smMaxSurfaces = 100;

public:

   ConvexShape();
   virtual ~ConvexShape();

   DECLARE_CONOBJECT(ConvexShape);
   DECLARE_CATEGORY("Object \t Simple");

   // ConsoleObject
   static void initPersistFields();

   // SimObject 
   void inspectPostApply() override;
   bool onAdd() override;
   void onRemove() override;
   void writeFields(Stream& stream, U32 tabStop) override;
   bool writeField(StringTableEntry fieldname, const char* value) override;

   U32 getSpecialFieldSize(StringTableEntry fieldName) override;
   const char* getSpecialFieldOut(StringTableEntry fieldName, const U32& index) override;

   // NetObject
   U32 packUpdate(NetConnection* conn, U32 mask, BitStream* stream) override;
   void unpackUpdate(NetConnection* conn, BitStream* stream) override;

   // SceneObject
   void onScaleChanged() override;
   void setTransform(const MatrixF& mat) override;
   void prepRenderImage(SceneRenderState* state) override;
   void buildConvex(const Box3F& box, Convex* convex) override;
   bool buildPolyList(PolyListContext context, AbstractPolyList* polyList, const Box3F& box, const SphereF& sphere) override;
   bool buildExportPolyList(ColladaUtils::ExportData* exportData, const Box3F& box, const SphereF&) override;
   bool castRay(const Point3F& start, const Point3F& end, RayInfo* info) override;
   bool collideBox(const Point3F& start, const Point3F& end, RayInfo* info) override;


   void updateBounds(bool recenter);
   void recenter();

   /// Geometry access.
   /// @{

   MatrixF getSurfaceWorldMat(S32 faceid, bool scaled = false) const;
   void cullEmptyPlanes(Vector< U32 >* removedPlanes);
   void exportToCollada();
   void resizePlanes(const Point3F& size);
   void getSurfaceLineList(S32 surfId, Vector< Point3F >& lineList);
   Geometry& getGeometry() { return mGeometry; }
   Vector<MatrixF>& getSurfaces() { return mSurfaces; }
   void getSurfaceTriangles(S32 surfId, Vector< Point3F >* outPoints, Vector< Point2F >* outCoords, bool worldSpace);

   /// @}

   /// Geometry Visualization.
   /// @{

   void renderFaceEdges(S32 faceid, const ColorI& color = ColorI::WHITE, F32 lineWidth = 1.0f);

   /// @}

   String getMaterialName() { return mMaterialName; }

protected:

   void _updateMaterial();
   void _updateGeometry(bool updateCollision = false);
   void _updateCollision();
   void _export(OptimizedPolyList* plist, const Box3F& box, const SphereF& sphere);

   void _renderDebug(ObjectRenderInst* ri, SceneRenderState* state, BaseMatInstance* mat);

   static S32 QSORT_CALLBACK _comparePlaneDist(const void* a, const void* b);

   static bool protectedSetSurface(void* object, const char* index, const char* data);

   static bool protectedSetSurfaceTexture(void* object, const char* index, const char* data);
   static bool protectedSetSurfaceUV(void* object, const char* index, const char* data);

protected:

   DECLARE_MATERIALASSET(ConvexShape, Material);
   DECLARE_ASSET_SETGET(ConvexShape, Material);

   // The actual Material instance
   BaseMatInstance* mMaterialInst;

   // The GFX vertex and primitive buffers
   /*GFXVertexBufferHandle< VertexType > mVertexBuffer;
   GFXPrimitiveBufferHandle            mPrimitiveBuffer;

   U32 mVertCount;
   U32 mPrimCount;*/

   Geometry mGeometry;

   Vector< PlaneF > mPlanes;

   Vector< MatrixF > mSurfaces;

   Vector< Point3F > mFaceCenters;

   //this is mostly for storage purposes, so we can save the texture mods
   Vector< surfaceMaterial > mSurfaceTextures;
   Vector< surfaceUV > mSurfaceUVs;
   Vector< surfaceBuffers > mSurfaceBuffers;

   Convex* mConvexList;

   PhysicsBody* mPhysicsRep;

   /// Geometry visualization
   /// @{      

   F32 mNormalLength;

   /// @}

};

#endif // _CONVEXSHAPE_H_
