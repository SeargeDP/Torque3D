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
#include "environment/meshRoad.h"

#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "util/catmullRom.h"
#include "math/util/quadTransforms.h"
#include "scene/simPath.h"
#include "scene/sceneRenderState.h"
#include "scene/sceneManager.h"
#include "scene/sgUtil.h"
#include "renderInstance/renderPassManager.h"
#include "T3D/gameBase/gameConnection.h"
#include "core/stream/bitStream.h"
#include "gfx/gfxDrawUtil.h"
#include "gfx/gfxTransformSaver.h"
#include "gfx/primBuilder.h"
#include "gfx/gfxDebugEvent.h"
#include "materials/materialManager.h"
#include "math/mathIO.h"
#include "math/mathUtils.h"
#include "math/util/frustum.h"
#include "gui/3d/guiTSControl.h"
#include "materials/shaderData.h"
#include "gfx/sim/gfxStateBlockData.h"
#include "gfx/sim/debugDraw.h"
#include "collision/concretePolyList.h"
#include "T3D/physics/physicsPlugin.h"
#include "T3D/physics/physicsBody.h"
#include "T3D/physics/physicsCollision.h"
#include "environment/nodeListManager.h"
#ifdef TORQUE_AFX_ENABLED
#include "afx/ce/afxZodiacMgr.h"
#endif

#define MIN_METERS_PER_SEGMENT 1.0f
#define MIN_NODE_DEPTH 0.25f
#define MAX_NODE_DEPTH 50.0f
#define MIN_NODE_WIDTH 0.25f
#define MAX_NODE_WIDTH 50.0f


static U32 gIdxArray[6][2][3] = {
   { { 0, 4, 5 }, { 0, 5, 1 }, },   // Top Face
   { { 2, 6, 4 }, { 2, 4, 0 }, },   // Left Face
   { { 1, 5, 7 }, { 1, 7, 3 }, },   // Right Face
   { { 2, 3, 7 }, { 2, 7, 6 }, },   // Bottom Face
   { { 0, 1, 3 }, { 0, 3, 2 }, },   // Front Face
   { { 4, 6, 7 }, { 4, 7, 5 }, },   // Back Face
};

static S32 QSORT_CALLBACK compareHitSegments(const void* a,const void* b)
{
   const MeshRoadHitSegment *fa = (MeshRoadHitSegment*)a;
   const MeshRoadHitSegment *fb = (MeshRoadHitSegment*)b;

   F32 diff = fb->t - fa->t;

   return (diff > 0) ? 1 : (diff < 0) ? -1 : 0;
}


//-----------------------------------------------------------------------------
// MeshRoadNodeList Struct
//-----------------------------------------------------------------------------

struct MeshRoadNodeList : public NodeListManager::NodeList
{
   Vector<Point3F>   mPositions;
   Vector<F32>       mWidths;
   Vector<F32>       mDepths;
   Vector<VectorF>   mNormals;

   MeshRoadNodeList() { }
   virtual ~MeshRoadNodeList() { }
};

//-----------------------------------------------------------------------------
// MeshRoadNodeEvent Class
//-----------------------------------------------------------------------------

class MeshRoadNodeEvent : public NodeListEvent
{
   typedef NodeListEvent Parent;

public:
   Vector<Point3F>   mPositions;
   Vector<F32>       mWidths;
   Vector<F32>       mDepths;
   Vector<VectorF>   mNormals;

public:
   MeshRoadNodeEvent() { mNodeList = NULL; }
   virtual ~MeshRoadNodeEvent() { }

   void pack(NetConnection*, BitStream*) override;
   void unpack(NetConnection*, BitStream*) override;

   void copyIntoList(NodeListManager::NodeList* copyInto) override;
   void padListToSize() override;

   DECLARE_CONOBJECT(MeshRoadNodeEvent);
};

void MeshRoadNodeEvent::pack(NetConnection* conn, BitStream* stream)
{
   Parent::pack( conn, stream );

   stream->writeInt( mPositions.size(), 16 );

   for (U32 i=0; i<mPositions.size(); ++i)
   {
      mathWrite( *stream, mPositions[i] );
      stream->write( mWidths[i] );
      stream->write( mDepths[i] );
      mathWrite( *stream, mNormals[i] );         
   }
}

void MeshRoadNodeEvent::unpack(NetConnection* conn, BitStream* stream)
{
   mNodeList = new MeshRoadNodeList();

   Parent::unpack( conn, stream );

   U32 count = stream->readInt( 16 );

   Point3F pos;
   F32 width, depth;
   VectorF normal;

   MeshRoadNodeList* list = static_cast<MeshRoadNodeList*>(mNodeList);

   for (U32 i=0; i<count; ++i)
   {
      mathRead( *stream, &pos );
      stream->read( &width );   
      stream->read( &depth );
      mathRead( *stream, &normal );

      list->mPositions.push_back( pos );
      list->mWidths.push_back( width );
      list->mDepths.push_back( depth );
      list->mNormals.push_back( normal );
   }

   list->mTotalValidNodes = count;

   // Do we have a complete list?
   if (list->mPositions.size() >= mTotalNodes)
      list->mListComplete = true;
}

void MeshRoadNodeEvent::copyIntoList(NodeListManager::NodeList* copyInto)
{
   MeshRoadNodeList* prevList = dynamic_cast<MeshRoadNodeList*>(copyInto);
   MeshRoadNodeList* list = static_cast<MeshRoadNodeList*>(mNodeList);

   // Merge our list with the old list.
   for (U32 i=mLocalListStart, index=0; i<mLocalListStart+list->mPositions.size(); ++i, ++index)
   {
      prevList->mPositions[i] = list->mPositions[index];
      prevList->mWidths[i] = list->mWidths[index];
      prevList->mDepths[i] = list->mDepths[index];
      prevList->mNormals[i] = list->mNormals[index];
   }
}

void MeshRoadNodeEvent::padListToSize()
{
   MeshRoadNodeList* list = static_cast<MeshRoadNodeList*>(mNodeList);

   U32 totalValidNodes = list->mTotalValidNodes;

   // Pad our list front?
   if (mLocalListStart)
   {
      MeshRoadNodeList* newlist = new MeshRoadNodeList();
      newlist->mPositions.increment(mLocalListStart);
      newlist->mWidths.increment(mLocalListStart);
      newlist->mDepths.increment(mLocalListStart);
      newlist->mNormals.increment(mLocalListStart);

      newlist->mPositions.merge(list->mPositions);
      newlist->mWidths.merge(list->mWidths);
      newlist->mDepths.merge(list->mDepths);
      newlist->mNormals.merge(list->mNormals);

      delete list;
      mNodeList = list = newlist;
   }

   // Pad our list end?
   if (list->mPositions.size() < mTotalNodes)
   {
      U32 delta = mTotalNodes - list->mPositions.size();
      list->mPositions.increment(delta);
      list->mWidths.increment(delta);
      list->mDepths.increment(delta);
      list->mNormals.increment(delta);
   }

   list->mTotalValidNodes = totalValidNodes;
}

IMPLEMENT_CO_NETEVENT_V1(MeshRoadNodeEvent);

ConsoleDocClass( MeshRoadNodeEvent,
   "@brief Sends messages to the Mesh Road Editor\n\n"
   "Editor use only.\n\n"
   "@internal"
);

//-----------------------------------------------------------------------------
// MeshRoadNodeListNotify Class
//-----------------------------------------------------------------------------

class MeshRoadNodeListNotify : public NodeListNotify
{
   typedef NodeListNotify Parent;

protected:
   SimObjectPtr<MeshRoad> mRoad;

public:
   MeshRoadNodeListNotify( MeshRoad* road, U32 listId ) { mRoad = road; mListId = listId; }
   virtual ~MeshRoadNodeListNotify() { mRoad = NULL; }

   void sendNotification( NodeListManager::NodeList* list ) override;
};

void MeshRoadNodeListNotify::sendNotification( NodeListManager::NodeList* list )
{
   if (mRoad.isValid())
   {
      // Build the road's nodes
      MeshRoadNodeList* roadList = dynamic_cast<MeshRoadNodeList*>( list );
      if (roadList)
         mRoad->buildNodesFromList( roadList );
   }
}

//-------------------------------------------------------------------------
// MeshRoadProfile Class
//-------------------------------------------------------------------------

MeshRoadProfile::MeshRoadProfile()
{
   mRoad = NULL;

   // Set transformation matrix to identity
   mObjToSlice.identity();
   mSliceToObj.identity();
}

S32 MeshRoadProfile::clickOnLine(Point3F &p)
{
   Point3F newProfilePt;
   Point3F ptOnSegment;
   F32 dist = 0.0f;
   F32 minDist = 99999.0f;
   U32 idx = 0;

   for(U32 i=0; i < mNodes.size()-1; i++)
   {
      ptOnSegment = MathUtils::mClosestPointOnSegment(mNodes[i].getPosition(), mNodes[i+1].getPosition(), p);

      dist = (p - ptOnSegment).len();

      if(dist < minDist)
      {
         minDist = dist;
         newProfilePt = ptOnSegment;
         idx = i+1;
      }
   }

   if(minDist <= 0.1f)
   {
      p.set(newProfilePt.x, newProfilePt.y, newProfilePt.z);

      return idx;
   }

   return -1;
}

void MeshRoadProfile::addPoint(U32 nodeId, Point3F &p)
{
   if(nodeId < mNodes.size() && nodeId != 0)
   {
      p.z = 0.0f;
      mNodes.insert(nodeId, p);
      mSegMtrls.insert(nodeId-1, mSegMtrls[nodeId-1]);
      mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
      generateNormals();
   }
}

void MeshRoadProfile::removePoint(U32 nodeId)
{
   if(nodeId > 0 && nodeId < mNodes.size()-1)
   {
      mNodes.erase(nodeId);
      mSegMtrls.remove(nodeId-1);
      mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
      generateNormals();
   }
}

void MeshRoadProfile::setNodePosition(U32 nodeId, Point3F pos)
{
   if(nodeId < mNodes.size())
   {
      mNodes[nodeId].setPosition(pos.x, pos.y);
      mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
      generateNormals();
   }
}

void MeshRoadProfile::toggleSmoothing(U32 nodeId)
{
   if(nodeId > 0 && nodeId < mNodes.size()-1)
   {
      mNodes[nodeId].setSmoothing(!mNodes[nodeId].isSmooth());
      mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
      generateNormals();
   }
}

void MeshRoadProfile::toggleSegMtrl(U32 seg)
{
   if(seg < mSegMtrls.size())
   {
      switch(mSegMtrls[seg])
      {
      case MeshRoad::Side:		mSegMtrls[seg] = MeshRoad::Top;		break;
      case MeshRoad::Top:		mSegMtrls[seg] = MeshRoad::Bottom;	break;
      case MeshRoad::Bottom:	mSegMtrls[seg] = MeshRoad::Side;		break;
      }

      mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
   }
}

void MeshRoadProfile::generateNormals()
{
   VectorF t, b, n, t2, n2;
   Point3F averagePt;

   mNodeNormals.clear();

   // Loop through all profile line segments
   for(U32 i=0; i < mNodes.size()-1; i++)
   {
      // Calculate normal for each node in line segment
      for(U32 j=0; j<2; j++)
      {
         // Smoothed Node: Average the node with nodes before and after.
         // Direction between the node and the average is the smoothed normal.
         if( mNodes[i+j].isSmooth() )
         {
            b = Point3F(0.0f, 0.0f, 1.0f);
            t = mNodes[i+j-1].getPosition() - mNodes[i+j].getPosition();
            n = mCross(t, b);
            n.normalizeSafe();

            t2 = mNodes[i+j].getPosition() - mNodes[i+j+1].getPosition();
            n2 = mCross(t2, b);
            n2.normalizeSafe();

            n += n2;
         }
         // Non-smoothed Node: Normal is perpendicular to segment.
         else
         {
            b = Point3F(0.0f, 0.0f, 1.0f);
            t = mNodes[i].getPosition() - mNodes[i+1].getPosition();
            n = mCross(t, b);
         }

         n.normalizeSafe();
         mNodeNormals.push_back(n);
      }
   }
}

void MeshRoadProfile::generateEndCap(F32 width)
{
   Point3F pt;

   mCap.newPoly();

   for ( U32 i = 0; i < mNodes.size(); i++ )
   {
      pt = mNodes[i].getPosition();
      mCap.addVert(pt);
   }

   for ( S32 i = mNodes.size()-1; i >= 0; i-- )
   {
      pt = mNodes[i].getPosition();
      pt.x = -pt.x - width;
      mCap.addVert(pt);
   }

   mCap.decompose();
}

void MeshRoadProfile::setProfileDepth(F32 depth)
{
   Point3F curPos = mNodes[mNodes.size()-1].getPosition();

   mNodes[mNodes.size()-1].setPosition(curPos.x, -depth);
}

void MeshRoadProfile::setTransform(const MatrixF &mat, const Point3F &p)
{
   mObjToSlice.identity();
   mSliceToObj.identity();

   mObjToSlice *= mat;
   mSliceToObj *= mObjToSlice.inverse();
   mSliceToObj.transpose();

   mStartPos = p;
}

void MeshRoadProfile::getNodeWorldPos(U32 nodeId, Point3F &p)
{
   if(nodeId < mNodes.size())
   {
      p = mNodes[nodeId].getPosition();
      mObjToSlice.mulP(p);
      p += mStartPos;
   }
}

void MeshRoadProfile::getNormToSlice(U32 normId, VectorF &n)
{
   if(normId < mNodeNormals.size())
   {
      n = mNodeNormals[normId];
      mObjToSlice.mulP(n);
   }
}

void MeshRoadProfile::getNormWorldPos(U32 normId, Point3F &p)
{
   if(normId < mNodeNormals.size())
   {
      U32 nodeId = normId/2 + (U32)(mFmod(normId,2.0f));
      p = mNodes[nodeId].getPosition();
      p += 0.5f * mNodeNormals[normId];		// Length = 0.5 units
      mObjToSlice.mulP(p);
      p += mStartPos;
   }
}

void MeshRoadProfile::worldToObj(Point3F &p)
{
   p -= mStartPos;
   mSliceToObj.mulP(p);
   p.z = 0.0f;
}

void MeshRoadProfile::objToWorld(Point3F &p)
{
   mObjToSlice.mulP(p);
   p += mStartPos;
}

F32 MeshRoadProfile::getProfileLen()
{
   F32 sum = 0.0f;
   Point3F segmentVec;

   for(U32 i=0; i < mNodes.size()-1; i++)
   {
      segmentVec = mNodes[i+1].getPosition() - mNodes[i].getPosition();
      sum += segmentVec.len();
   }

   return sum;
}

F32 MeshRoadProfile::getNodePosPercent(U32 nodeId)
{
   nodeId = mFmod(nodeId, mNodes.size());

   if(nodeId == 0)
      return 0.0f;
   else if(nodeId == mNodes.size()-1)
      return 1.0f;

   F32 totLen = getProfileLen();
   F32 sum = 0.0f;
   Point3F segmentVec;

   for(U32 i=0; i < nodeId; i++)
   {
      segmentVec = mNodes[i+1].getPosition() - mNodes[i].getPosition();
      sum += segmentVec.len();
   }

   return sum/totLen;
}

void MeshRoadProfile::resetProfile(F32 defaultDepth)
{
   Point3F pos(0.0f, 0.0f, 0.0f);

   mNodes.clear();
   mNodes.push_back(pos);

   pos.y = -defaultDepth;
   mNodes.push_back(pos);

   mSegMtrls.clear();
   mSegMtrls.push_back(MeshRoad::Side);

   mRoad->setMaskBits(MeshRoad::ProfileMask | MeshRoad::RegenMask);
   generateNormals();
}

//------------------------------------------------------------------------------
// MeshRoadConvex Class
//------------------------------------------------------------------------------

const MatrixF& MeshRoadConvex::getTransform() const
{
   return MatrixF::Identity; //mObject->getTransform();    
}

Box3F MeshRoadConvex::getBoundingBox() const
{
   return box;
}

Box3F MeshRoadConvex::getBoundingBox(const MatrixF& mat, const Point3F& scale) const
{
   Box3F newBox = box;
   newBox.minExtents.convolve(scale);
   newBox.maxExtents.convolve(scale);
   mat.mul(newBox);
   return newBox;
}

Point3F MeshRoadConvex::support(const VectorF& vec) const
{
   F32 bestDot = mDot( verts[0], vec );

   const Point3F *bestP = &verts[0];
   for(S32 i=1; i<4; i++)
   {
      F32 newD = mDot(verts[i], vec);
      if(newD > bestDot)
      {
         bestDot = newD;
         bestP = &verts[i];
      }
   }

   return *bestP;
}

void MeshRoadConvex::getFeatures(const MatrixF& mat, const VectorF& n, ConvexFeature* cf)
{
   cf->material = 0;
   cf->mObject = mObject;

   // For a tetrahedron this is pretty easy... first
   // convert everything into world space.
   Point3F tverts[4];
   mat.mulP(verts[0], &tverts[0]);
   mat.mulP(verts[1], &tverts[1]);
   mat.mulP(verts[2], &tverts[2]);
   mat.mulP(verts[3], &tverts[3]);

   // Points...
   S32 firstVert = cf->mVertexList.size();
   cf->mVertexList.increment(); cf->mVertexList.last() = tverts[0];
   cf->mVertexList.increment(); cf->mVertexList.last() = tverts[1];
   cf->mVertexList.increment(); cf->mVertexList.last() = tverts[2];
   cf->mVertexList.increment(); cf->mVertexList.last() = tverts[3];

   // Edges...
   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+0;
   cf->mEdgeList.last().vertex[1] = firstVert+1;

   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+1;
   cf->mEdgeList.last().vertex[1] = firstVert+2;

   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+2;
   cf->mEdgeList.last().vertex[1] = firstVert+0;

   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+3;
   cf->mEdgeList.last().vertex[1] = firstVert+0;

   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+3;
   cf->mEdgeList.last().vertex[1] = firstVert+1;

   cf->mEdgeList.increment();
   cf->mEdgeList.last().vertex[0] = firstVert+3;
   cf->mEdgeList.last().vertex[1] = firstVert+2;

   // Triangles...
   cf->mFaceList.increment();
   cf->mFaceList.last().normal = PlaneF(tverts[2], tverts[1], tverts[0]);
   cf->mFaceList.last().vertex[0] = firstVert+2;
   cf->mFaceList.last().vertex[1] = firstVert+1;
   cf->mFaceList.last().vertex[2] = firstVert+0;

   cf->mFaceList.increment();
   cf->mFaceList.last().normal = PlaneF(tverts[1], tverts[0], tverts[3]);
   cf->mFaceList.last().vertex[0] = firstVert+1;
   cf->mFaceList.last().vertex[1] = firstVert+0;
   cf->mFaceList.last().vertex[2] = firstVert+3;

   cf->mFaceList.increment();
   cf->mFaceList.last().normal = PlaneF(tverts[2], tverts[1], tverts[3]);
   cf->mFaceList.last().vertex[0] = firstVert+2;
   cf->mFaceList.last().vertex[1] = firstVert+1;
   cf->mFaceList.last().vertex[2] = firstVert+3;

   cf->mFaceList.increment();
   cf->mFaceList.last().normal = PlaneF(tverts[0], tverts[2], tverts[3]);
   cf->mFaceList.last().vertex[0] = firstVert+0;
   cf->mFaceList.last().vertex[1] = firstVert+2;
   cf->mFaceList.last().vertex[2] = firstVert+3;
}


void MeshRoadConvex::getPolyList( AbstractPolyList* list )
{
   // Transform the list into object space and set the pointer to the object
   //MatrixF i( mObject->getTransform() );
   //Point3F iS( mObject->getScale() );
   //list->setTransform(&i, iS);

   list->setTransform( &MatrixF::Identity, Point3F::One );
   list->setObject(mObject);

   // Points...
   S32 base =  list->addPoint(verts[1]);
   list->addPoint(verts[2]);
   list->addPoint(verts[0]);
   list->addPoint(verts[3]);

   // Planes...
   list->begin(0,0);
   list->vertex(base + 2);
   list->vertex(base + 1);
   list->vertex(base + 0);
   list->plane(base + 2, base + 1, base + 0);
   list->end();
   list->begin(0,0);
   list->vertex(base + 2);
   list->vertex(base + 1);
   list->vertex(base + 3);
   list->plane(base + 2, base + 1, base + 3);
   list->end();
   list->begin(0,0);
   list->vertex(base + 3);
   list->vertex(base + 1);
   list->vertex(base + 0);
   list->plane(base + 3, base + 1, base + 0);
   list->end();
   list->begin(0,0);
   list->vertex(base + 2);
   list->vertex(base + 3);
   list->vertex(base + 0);
   list->plane(base + 2, base + 3, base + 0);
   list->end();
}


//------------------------------------------------------------------------------
// MeshRoadSegment Class
//------------------------------------------------------------------------------

MeshRoadSegment::MeshRoadSegment()
{
   mPlaneCount = 0;
   columns = 0;
   rows = 0;
   numVerts = 0;
   numTriangles = 0;

   startVert = 0;
   endVert = 0;
   startIndex = 0;
   endIndex = 0;

   slice0 = NULL;
   slice1 = NULL;
}

MeshRoadSegment::MeshRoadSegment( MeshRoadSlice *rs0, MeshRoadSlice *rs1, const MatrixF &roadMat )
{
   columns = 0;
   rows = 0;
   numVerts = 0;
   numTriangles = 0;

   startVert = 0;
   endVert = 0;
   startIndex = 0;
   endIndex = 0;

   slice0 = rs0;
   slice1 = rs1;

   // Calculate the bounding box(s)
   worldbounds.minExtents = worldbounds.maxExtents = rs0->p0;

   for(U32 i=0; i < rs0->verts.size(); i++)
      worldbounds.extend( rs0->verts[i] );

   for(U32 i=0; i < rs1->verts.size(); i++)
      worldbounds.extend( rs1->verts[i] );

   objectbounds = worldbounds;
   roadMat.mul( objectbounds );

   // Calculate the planes for this segment
   // Will be used for intersection/buoyancy tests

   mPlaneCount = 6;
   mPlanes[0].set( slice0->pb0, slice0->p0, slice1->p0 ); // left   
   mPlanes[1].set( slice1->pb2, slice1->p2, slice0->p2 ); // right   
   mPlanes[2].set( slice0->pb2, slice0->p2, slice0->p0 ); // near   
   mPlanes[3].set( slice1->p0, slice1->p2, slice1->pb2 ); // far   
   mPlanes[4].set( slice1->p2, slice1->p0, slice0->p0 ); // top   
   mPlanes[5].set( slice0->pb0, slice1->pb0, slice1->pb2 ); // bottom
}

void MeshRoadSegment::set( MeshRoadSlice *rs0, MeshRoadSlice *rs1 )
{
   columns = 0;
   rows = 0;
   numVerts = 0;
   numTriangles = 0;

   startVert = 0;
   endVert = 0;
   startIndex = 0;
   endIndex = 0;

   slice0 = rs0;
   slice1 = rs1;
}

bool MeshRoadSegment::intersectBox( const Box3F &bounds ) const
{
   // This code copied from Frustum class.

   Point3F maxPoint;
   F32 maxDot;

   // Note the planes are ordered left, right, near, 
   // far, top, bottom for getting early rejections
   // from the typical horizontal scene.
   for ( S32 i = 0; i < mPlaneCount; i++ )
   {
      // This is pretty much as optimal as you can
      // get for a plane vs AABB test...
      // 
      // 4 comparisons
      // 3 multiplies
      // 2 adds
      // 1 negation
      //
      // It will early out as soon as it detects the
      // bounds is outside one of the planes.

      if ( mPlanes[i].x > 0 )
         maxPoint.x = bounds.maxExtents.x;
      else
         maxPoint.x = bounds.minExtents.x;

      if ( mPlanes[i].y > 0 )
         maxPoint.y = bounds.maxExtents.y;
      else
         maxPoint.y = bounds.minExtents.y;

      if ( mPlanes[i].z > 0 )
         maxPoint.z = bounds.maxExtents.z;
      else
         maxPoint.z = bounds.minExtents.z;

      maxDot = mDot( maxPoint, mPlanes[ i ] );

      if ( maxDot <= -mPlanes[ i ].d )
         return false;
   }

   return true;
}

bool MeshRoadSegment::containsPoint( const Point3F &pnt ) const
{
   // This code from Frustum class.

   F32 maxDot;

   // Note the planes are ordered left, right, near, 
   // far, top, bottom for getting early rejections
   // from the typical horizontal scene.
   for ( S32 i = 0; i < mPlaneCount; i++ )
   {
      const PlaneF &plane = mPlanes[ i ];

      // This is pretty much as optimal as you can
      // get for a plane vs point test...
      // 
      // 1 comparison
      // 2 multiplies
      // 1 adds
      //
      // It will early out as soon as it detects the
      // point is outside one of the planes.

      maxDot = mDot( pnt, plane ) + plane.d;
      if ( maxDot < 0.0f )
         return false;
   }

   return true;
}

F32 MeshRoadSegment::distanceToSurface(const Point3F &pnt) const
{
   return mPlanes[4].distToPlane( pnt );
}

//------------------------------------------------------------------------------
// MeshRoad Class
//------------------------------------------------------------------------------

ConsoleDocClass( MeshRoad,
   "@brief A strip of rectangular mesh segments defined by a 3D spline "
   "for prototyping road-shaped objects in your scene.\n\n"
   
   "User may control width and depth per node, overall spline shape in three "
   "dimensions, and seperate Materials for rendering the top, bottom, and side surfaces.\n\n"
   
   "MeshRoad is not capable of handling intersections, branches, curbs, or other "
   "desirable features in a final 'road' asset and is therefore intended for "
   "prototyping and experimentation.\n\n"

   "Materials assigned to MeshRoad should tile vertically.\n\n"

   "@ingroup Terrain"
);

bool MeshRoad::smEditorOpen = false;
bool MeshRoad::smShowBatches = false;
bool MeshRoad::smShowSpline = true;
bool MeshRoad::smShowRoad = true;
bool MeshRoad::smShowRoadProfile = false;
bool MeshRoad::smWireframe = true;
SimObjectPtr<SimSet> MeshRoad::smServerMeshRoadSet = NULL;

GFXStateBlockRef MeshRoad::smWireframeSB;

IMPLEMENT_CO_NETOBJECT_V1(MeshRoad);

MeshRoad::MeshRoad()
: mTextureLength( 5.0f ),
  mBreakAngle( 3.0f ),
  mWidthSubdivisions( 0 ),
  mPhysicsRep( NULL )
{
   mConvexList = new Convex;

   // Setup NetObject.
   mTypeMask |= StaticObjectType | StaticShapeObjectType;
   mNetFlags.set(Ghostable);

   mMatInst[Top] = NULL;
   mMatInst[Bottom] = NULL;
   mMatInst[Side] = NULL;
   mTypeMask |= TerrainLikeObjectType;
   for (U32 i = 0; i < SurfaceCount; i++)
   {
      mVertCount[i] = 0;
      mTriangleCount[i] = 0;
   }

   INIT_ASSET(TopMaterial);
   INIT_ASSET(BottomMaterial);
   INIT_ASSET(SideMaterial);

   mSideProfile.mRoad = this;
}

MeshRoad::~MeshRoad()
{   
   delete mConvexList;
   mConvexList = NULL;
}

FRangeValidator mrTextureLengthV(0.1f, FLT_MAX);
void MeshRoad::initPersistFields()
{
   docsURL;
   addGroup( "MeshRoad" );

      INITPERSISTFIELD_MATERIALASSET(TopMaterial, MeshRoad, "Material for the upper surface of the road.");
      INITPERSISTFIELD_MATERIALASSET(BottomMaterial, MeshRoad, "Material for the bottom surface of the road.");
      INITPERSISTFIELD_MATERIALASSET(SideMaterial, MeshRoad, "Material for the side surface of the road.");

      addFieldV( "textureLength", TypeRangedF32, Offset( mTextureLength, MeshRoad ), &mrTextureLengthV,
         "The length in meters of textures mapped to the MeshRoad." );      

      addFieldV( "breakAngle", TypeRangedF32, Offset( mBreakAngle, MeshRoad ), &CommonValidators::PosDegreeRange,
         "Angle in degrees - MeshRoad will subdivide the spline if its curve is greater than this threshold." ); 

      addFieldV( "widthSubdivisions", TypeRangedS32, Offset( mWidthSubdivisions, MeshRoad ), &CommonValidators::PositiveInt,
         "Subdivide segments widthwise this many times when generating vertices." );

   endGroup( "MeshRoad" );

   addGroup( "Internal" );

      addProtectedField( "Node", TypeString, 0, &addNodeFromField, &emptyStringProtectedGetFn,
         "Do not modify, for internal use.", AbstractClassRep::FIELD_HideInInspectors | AbstractClassRep::FIELD_SpecialtyArrayField);

      addProtectedField( "ProfileNode", TypeString, 0, &addProfileNodeFromField, &emptyStringProtectedGetFn,
         "Do not modify, for internal use.", AbstractClassRep::FIELD_HideInInspectors | AbstractClassRep::FIELD_SpecialtyArrayField);

   endGroup( "Internal" );

   Parent::initPersistFields();
}

void MeshRoad::consoleInit()
{
   Parent::consoleInit();

   Con::addVariable( "$MeshRoad::EditorOpen", TypeBool, &MeshRoad::smEditorOpen, "True if the MeshRoad editor is open, otherwise false.\n"
      "@ingroup Editors\n");
   Con::addVariable( "$MeshRoad::wireframe", TypeBool, &MeshRoad::smWireframe, "If true, will render the wireframe of the road.\n"
      "@ingroup Editors\n");
   Con::addVariable( "$MeshRoad::showBatches", TypeBool, &MeshRoad::smShowBatches, "Determines if the debug rendering of the batches cubes is displayed or not.\n"
      "@ingroup Editors\n");
   Con::addVariable( "$MeshRoad::showSpline", TypeBool, &MeshRoad::smShowSpline, "If true, the spline on which the curvature of this road is based will be rendered.\n"
      "@ingroup Editors\n");
   Con::addVariable( "$MeshRoad::showRoad", TypeBool, &MeshRoad::smShowRoad, "If true, the road will be rendered. When in the editor, roads are always rendered regardless of this flag.\n"
      "@ingroup Editors\n");
   Con::addVariable( "$MeshRoad::showRoadProfile", TypeBool, &MeshRoad::smShowRoadProfile, "If true, the road profile will be shown in the editor.\n"
      "@ingroup Editors\n");
}

bool MeshRoad::addNodeFromField( void *object, const char *index, const char *data )
{
   MeshRoad *pObj = static_cast<MeshRoad*>(object);

   //if ( !pObj->isProperlyAdded() )
   //{      
   F32 width, depth;
   Point3F pos, normal;      
   U32 result = dSscanf( data, "%g %g %g %g %g %g %g %g", &pos.x, &pos.y, &pos.z, &width, &depth, &normal.x, &normal.y, &normal.z );      
   if ( result == 8 )
      pObj->_addNode( pos, width, depth, normal );      
   //}

   return false;
}

bool MeshRoad::addProfileNodeFromField( void* obj, const char *index, const char* data )
{
   MeshRoad *pObj = static_cast<MeshRoad*>(obj);

   F32 x, y;
   U32 smooth, mtrl;

   U32 result = dSscanf( data, "%g %g %d %d", &x, &y, &smooth, &mtrl );
   if ( result == 4 )
   {
      if(!pObj->mSideProfile.mNodes.empty())
         pObj->mSideProfile.mSegMtrls.push_back(mtrl);

      MeshRoadProfileNode node;
      node.setPosition(x, y);
      node.setSmoothing(smooth != 0);
      pObj->mSideProfile.mNodes.push_back(node);
   }

   return false;
}

bool MeshRoad::onAdd()
{
   if ( !Parent::onAdd() ) 
      return false;

   // Reset the World Box.
   //setGlobalBounds();
   resetWorldBox();

   // Set the Render Transform.
   setRenderTransform(mObjToWorld);

   // Add to ServerMeshRoadSet
   if ( isServerObject() )
   {
      getServerSet()->addObject( this );      
   }

   if ( isClientObject() )
      _initMaterial();

   // If this road was not created from a file, give profile two default nodes
   if(mSideProfile.mNodes.empty())
   {
      // Initialize with two nodes in vertical line with unit length
      MeshRoadProfileNode node1(Point3F(0.0f, 0.0f, 0.0f));
      MeshRoadProfileNode node2(Point3F(0.0f, -5.0f, 0.0f));

      mSideProfile.mNodes.push_back(node1);
      mSideProfile.mNodes.push_back(node2);

      // Both node normals are straight to the right, perpendicular to the profile line
      VectorF norm(1.0f, 0.0f, 0.0f);

      mSideProfile.mNodeNormals.push_back(norm);
      mSideProfile.mNodeNormals.push_back(norm);

      mSideProfile.mSegMtrls.push_back(MeshRoad::Side);
   }
   else
      mSideProfile.generateNormals();

   // Generate the Vert/Index buffers and everything else.
   _regenerate();

   // Add to Scene.
   addToScene();

   return true;
}

void MeshRoad::onRemove()
{
   SAFE_DELETE( mPhysicsRep );

   mConvexList->nukeList();

   for ( U32 i = 0; i < SurfaceCount; i++ )
   {
      SAFE_DELETE( mMatInst[i] );
   }

   removeFromScene();
   Parent::onRemove();
}

void MeshRoad::inspectPostApply()
{
   // Set Parent.
   Parent::inspectPostApply();

   //if ( mMetersPerSegment < MIN_METERS_PER_SEGMENT )
   //   mMetersPerSegment = MIN_METERS_PER_SEGMENT;

   setMaskBits(MeshRoadMask);
}

void MeshRoad::onStaticModified( const char* slotName, const char*newValue )
{
   Parent::onStaticModified( slotName, newValue );

   if ( dStricmp( slotName, "breakAngle" ) == 0 )
   {
      setMaskBits( RegenMask );
   }
}

void MeshRoad::writeFields( Stream &stream, U32 tabStop )
{
   Parent::writeFields( stream, tabStop );

   // Now write all nodes

   stream.write(2, "\r\n");   

   for ( U32 i = 0; i < mNodes.size(); i++ )
   {
      const MeshRoadNode &node = mNodes[i];

      stream.writeTabs(tabStop);

      char buffer[1024];
      dMemset( buffer, 0, 1024 );
      dSprintf( buffer, 1024, "Node = \"%g %g %g %g %g %g %g %g\";", node.point.x, node.point.y, node.point.z, node.width, node.depth, node.normal.x, node.normal.y, node.normal.z );      
      stream.writeLine( (const U8*)buffer );
   }

   stream.write(2, "\r\n");

   Point3F nodePos;
   U8 smooth, mtrl;

   for ( U32 i = 0; i < mSideProfile.mNodes.size(); i++ )
   {
      nodePos = mSideProfile.mNodes[i].getPosition();

      if(i)
         mtrl = mSideProfile.mSegMtrls[i-1];
      else
         mtrl = 0;

      if(mSideProfile.mNodes[i].isSmooth())
         smooth = 1;
      else
         smooth = 0;

      stream.writeTabs(tabStop);

      char buffer[1024];
      dMemset( buffer, 0, 1024 );
      dSprintf( buffer, 1024, "ProfileNode = \"%.6f %.6f %d %d\";", nodePos.x, nodePos.y, smooth, mtrl);
      stream.writeLine( (const U8*)buffer );
   }
}

bool MeshRoad::writeField( StringTableEntry fieldname, const char *value )
{   
   if ( fieldname == StringTable->insert("Node") )
      return false;

   if ( fieldname == StringTable->insert("ProfileNode") )
      return false;

   return Parent::writeField( fieldname, value );
}

U32 MeshRoad::getSpecialFieldSize(StringTableEntry fieldName)
{
   if (fieldName == StringTable->insert("Node"))
   {
      return mNodes.size();
   }
   else if (fieldName == StringTable->insert("ProfileNode"))
   {
      return mSideProfile.mNodes.size();
   }

   return 0;
}

const char* MeshRoad::getSpecialFieldOut(StringTableEntry fieldName, const U32& index)
{
   if (fieldName == StringTable->insert("Node"))
   {
      if (index >= mNodes.size())
         return NULL;

      const MeshRoadNode& node = mNodes[index];

      char buffer[1024];
      dMemset(buffer, 0, 1024);
      dSprintf(buffer, 1024, "Node = \"%g %g %g %g %g %g %g %g\";", node.point.x, node.point.y, node.point.z, node.width, node.depth, node.normal.x, node.normal.y, node.normal.z);

      return StringTable->insert(buffer);
   }
   else if (fieldName == StringTable->insert("ProfileNode") && mSideProfile.mNodes.size())
   {
      Point3F nodePos = mSideProfile.mNodes[index].getPosition();
      U8 smooth, mtrl;

      if (index)
         mtrl = mSideProfile.mSegMtrls[index - 1];
      else
         mtrl = 0;

      if (mSideProfile.mNodes[index].isSmooth())
         smooth = 1;
      else
         smooth = 0;

      char buffer[1024];
      dMemset(buffer, 0, 1024);
      dSprintf(buffer, 1024, "ProfileNode = \"%.6f %.6f %d %d\";", nodePos.x, nodePos.y, smooth, mtrl);

      return StringTable->insert(buffer);
   }

   return NULL;
}

void MeshRoad::onEditorEnable()
{
}

void MeshRoad::onEditorDisable()
{
}

SimSet* MeshRoad::getServerSet()
{
   if ( !smServerMeshRoadSet )
   {
      smServerMeshRoadSet = new SimSet();
      smServerMeshRoadSet->registerObject( "ServerMeshRoadSet" );
      Sim::getRootGroup()->addObject( smServerMeshRoadSet );
   }

   return smServerMeshRoadSet;
}

void MeshRoad::prepRenderImage( SceneRenderState* state )
{
   if ( mNodes.size() <= 1 )
      return;

   RenderPassManager *renderPass = state->getRenderPass();
   
   // Normal Road RenderInstance
   // Always rendered when the editor is not open
   // otherwise obey the smShowRoad flag
   if ( smShowRoad || !smEditorOpen )
   {
#ifdef TORQUE_AFX_ENABLED
      afxZodiacMgr::renderMeshRoadZodiacs(state, this);
#endif
      MeshRenderInst coreRI;
      coreRI.clear();
      coreRI.objectToWorld = &MatrixF::Identity;
      coreRI.worldToCamera = renderPass->allocSharedXform(RenderPassManager::View);
      coreRI.projection = renderPass->allocSharedXform(RenderPassManager::Projection);
      coreRI.type = RenderPassManager::RIT_Mesh;      
      
      BaseMatInstance *matInst;
      for ( U32 i = 0; i < SurfaceCount; i++ )
      {             
         matInst = state->getOverrideMaterial( mMatInst[i] );   
         if ( !matInst )
            continue;

         // Get the lights if we haven't already.
         if ( matInst->isForwardLit() && !coreRI.lights[0] )
         {
            LightQuery query;
            query.init( getWorldSphere() );
            query.getLights( coreRI.lights, 8 );
         }

         MeshRenderInst *ri = renderPass->allocInst<MeshRenderInst>();
         *ri = coreRI;

         // Currently rendering whole road, fix to cull and batch
         // per segment.

         // Set the correct material for rendering.
         ri->matInst = matInst;
         ri->vertBuff = &mVB[i];
         ri->primBuff = &mPB[i];

         ri->prim = renderPass->allocPrim();
         ri->prim->type = GFXTriangleList;
         ri->prim->minIndex = 0;
         ri->prim->startIndex = 0;
         ri->prim->numPrimitives = mTriangleCount[i];
         ri->prim->startVertex = 0;
         ri->prim->numVertices = mVertCount[i];

         // We sort by the material then vertex buffer.
         ri->defaultKey = matInst->getStateHint();
         ri->defaultKey2 = (uintptr_t)ri->vertBuff; // Not 64bit safe!

         renderPass->addInst( ri );  
      }
   }

   // Debug RenderInstance
   // Only when editor is open.
   if ( smEditorOpen )
   {
      ObjectRenderInst *ri = state->getRenderPass()->allocInst<ObjectRenderInst>();
      ri->renderDelegate.bind( this, &MeshRoad::_debugRender );
      ri->type = RenderPassManager::RIT_Editor;
      state->getRenderPass()->addInst( ri );
   }
}

void MeshRoad::_initMaterial()
{
   if (mTopMaterialAsset.notNull())
   {
      if (!mMatInst[Top] || !String(mTopMaterialAsset->getMaterialDefinitionName()).equal(mMatInst[Top]->getMaterial()->getName(), String::NoCase))
      {
         SAFE_DELETE(mMatInst[Top]);

         Material* tMat = nullptr;
         if (!Sim::findObject(mTopMaterialAsset->getMaterialDefinitionName(), tMat))
            Con::errorf("MeshRoad::_initMaterial - Material %s was not found.", mTopMaterialAsset->getMaterialDefinitionName());

         mMaterial[Top] = tMat;

         if (mMaterial[Top])
            mMatInst[Top] = mMaterial[Top]->createMatInstance();
         else
            mMatInst[Top] = MATMGR->createMatInstance("WarningMaterial");

         mMatInst[Top]->init(MATMGR->getDefaultFeatures(), getGFXVertexFormat<GFXVertexPNTT>());
      }
   }

   if (mBottomMaterialAsset.notNull())
   {
      if (!mMatInst[Bottom] || !String(mBottomMaterialAsset->getMaterialDefinitionName()).equal(mMatInst[Bottom]->getMaterial()->getName(), String::NoCase))
      {

         SAFE_DELETE(mMatInst[Bottom]);

         Material* tMat = nullptr;
         if (!Sim::findObject(mBottomMaterialAsset->getMaterialDefinitionName(), tMat))
            Con::errorf("MeshRoad::_initMaterial - Material %s was not found.", mBottomMaterialAsset->getMaterialDefinitionName());

         mMaterial[Bottom] = tMat;

         if (mMaterial[Bottom])
            mMatInst[Bottom] = mMaterial[Bottom]->createMatInstance();
         else
            mMatInst[Bottom] = MATMGR->createMatInstance("WarningMaterial");

         mMatInst[Bottom]->init(MATMGR->getDefaultFeatures(), getGFXVertexFormat<GFXVertexPNTT>());
      }
   }

   if (mSideMaterialAsset.notNull())
   {
      if (!mMatInst[Side] || !String(mSideMaterialAsset->getMaterialDefinitionName()).equal(mMatInst[Side]->getMaterial()->getName(), String::NoCase))
      {
         SAFE_DELETE(mMatInst[Side]);

         Material* tMat = nullptr;
         if (!Sim::findObject(mSideMaterialAsset->getMaterialDefinitionName(), tMat))
            Con::errorf("MeshRoad::_initMaterial - Material %s was not found.", mSideMaterialAsset->getMaterialDefinitionName());

         mMaterial[Side] = tMat;

         if (mMaterial[Side])
            mMatInst[Side] = mMaterial[Side]->createMatInstance();
         else
            mMatInst[Side] = MATMGR->createMatInstance("WarningMaterial");

         mMatInst[Side]->init(MATMGR->getDefaultFeatures(), getGFXVertexFormat<GFXVertexPNTT>());
      }
   }
}

void MeshRoad::_debugRender( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance* )
{
   //MeshRoadConvex convex;
   //buildConvex( Box3F(true), convex );
   //convex.render();   
   //GFXDrawUtil *drawer = GFX->getDrawUtil();

   //GFX->setStateBlock( smStateBlock );
   return;
   /*
   U32 convexCount = mDebugConvex.size();

   PrimBuild::begin( GFXTriangleList, convexCount * 12 );
   PrimBuild::color4i( 0, 0, 255, 155 );

   for ( U32 i = 0; i < convexCount; i++ )
   {
      MeshRoadConvex *convex = mDebugConvex[i];

      Point3F a = convex->verts[0];
      Point3F b = convex->verts[1];
      Point3F c = convex->verts[2];
      Point3F p = convex->verts[3];

      //mObjToWorld.mulP(a);
      //mObjToWorld.mulP(b);
      //mObjToWorld.mulP(c);
      //mObjToWorld.mulP(p);
      
      PrimBuild::vertex3fv( c );
      PrimBuild::vertex3fv( b );
      PrimBuild::vertex3fv( a );      

      PrimBuild::vertex3fv( b );
      PrimBuild::vertex3fv( a );
      PrimBuild::vertex3fv( p );

      PrimBuild::vertex3fv( c );
      PrimBuild::vertex3fv( b );
      PrimBuild::vertex3fv( p );

      PrimBuild::vertex3fv( a );
      PrimBuild::vertex3fv( c );
      PrimBuild::vertex3fv( p );
   }

   PrimBuild::end();

   for ( U32 i = 0; i < mSegments.size(); i++ )
   {
      ///GFX->getDrawUtil()->drawWireBox( mSegments[i].worldbounds, ColorI(255,0,0,255) );
   }

   GFX->enterDebugEvent( ColorI( 255, 0, 0 ), "DecalRoad_debugRender" );
   GFXTransformSaver saver;

   GFX->setStateBlock( smStateBlock );      

   Point3F size(1,1,1);
   ColorI color( 255, 0, 0, 255 );

   if ( smShowBatches )  
   {
      for ( U32 i = 0; i < mBatches.size(); i++ )   
      {
         const Box3F &box = mBatches[i].bounds;
         Point3F center;
         box.getCenter( &center );

         GFX->getDrawUtil()->drawWireCube( ( box.maxExtents - box.minExtents ) * 0.5f, center, ColorI(255,100,100,255) );         
      }
   }

   GFX->leaveDebugEvent();
   */
}

U32 MeshRoad::packUpdate(NetConnection * con, U32 mask, BitStream * stream)
{  
   U32 retMask = Parent::packUpdate(con, mask, stream);

   if ( stream->writeFlag( mask & MeshRoadMask ) )
   {
      // Write Object Transform.
      stream->writeAffineTransform( mObjToWorld );

      // Write Materials
      PACK_ASSET(con, TopMaterial);
      PACK_ASSET(con, BottomMaterial);
      PACK_ASSET(con, SideMaterial);

      stream->write( mTextureLength );      
      stream->write( mBreakAngle );
      stream->write( mWidthSubdivisions );
   }

   if ( stream->writeFlag( mask & ProfileMask ) )
   {
      stream->writeInt( mSideProfile.mNodes.size(), 16 );

      for( U32 i = 0; i < mSideProfile.mNodes.size(); i++ )
      {
         mathWrite( *stream, mSideProfile.mNodes[i].getPosition() );
         stream->writeFlag( mSideProfile.mNodes[i].isSmooth() );

         if(i)
            stream->writeInt(mSideProfile.mSegMtrls[i-1], 3);
         else
            stream->writeInt(0, 3);
      }
   }

   if ( stream->writeFlag( mask & NodeMask ) )
   {
      const U32 nodeByteSize = 32; // Based on sending all of a node's parameters

      // Test if we can fit all of our nodes within the current stream.
      // We make sure we leave 100 bytes still free in the stream for whatever
      // may follow us.
      S32 allowedBytes = stream->getWriteByteSize() - 100;
      if ( stream->writeFlag( (nodeByteSize * mNodes.size()) < allowedBytes ) )
      {
         // All nodes should fit, so send them out now.
         stream->writeInt( mNodes.size(), 16 );

         for ( U32 i = 0; i < mNodes.size(); i++ )
         {
            mathWrite( *stream, mNodes[i].point );
            stream->write( mNodes[i].width );
            stream->write( mNodes[i].depth );
            mathWrite( *stream, mNodes[i].normal );         
         }
      }
      else
      {
         // There isn't enough space left in the stream for all of the
         // nodes.  Batch them up into NetEvents.
         U32 id = gServerNodeListManager->nextListId();
         U32 count = 0;
         U32 index = 0;
         while (count < mNodes.size())
         {
            count += NodeListManager::smMaximumNodesPerEvent;
            if (count > mNodes.size())
            {
               count = mNodes.size();
            }

            MeshRoadNodeEvent* event = new MeshRoadNodeEvent();
            event->mId = id;
            event->mTotalNodes = mNodes.size();
            event->mLocalListStart = index;

            for (; index<count; ++index)
            {
               event->mPositions.push_back( mNodes[index].point );
               event->mWidths.push_back( mNodes[index].width );
               event->mDepths.push_back( mNodes[index].depth );
               event->mNormals.push_back( mNodes[index].normal );
            }

            con->postNetEvent( event );
         }

         stream->write( id );
      }
   }

   stream->writeFlag( mask & RegenMask );

   // Were done ...
   return retMask;
}

void MeshRoad::unpackUpdate(NetConnection * con, BitStream * stream)
{
   // Unpack Parent.
   Parent::unpackUpdate(con, stream);

   // MeshRoadMask
   if(stream->readFlag())
   {
      MatrixF		ObjectMatrix;
      stream->readAffineTransform(&ObjectMatrix);
      Parent::setTransform(ObjectMatrix);

      UNPACK_ASSET(con, TopMaterial);
      UNPACK_ASSET(con, BottomMaterial);
      UNPACK_ASSET(con, SideMaterial);

      if ( isProperlyAdded() )
         _initMaterial(); 

      stream->read( &mTextureLength );

      stream->read( &mBreakAngle );

      stream->read( &mWidthSubdivisions );
   }

   // ProfileMask
   if(stream->readFlag())
   {
      Point3F pos;

      mSideProfile.mNodes.clear();
      mSideProfile.mSegMtrls.clear();

      U32 count = stream->readInt( 16 );

      for( U32 i = 0; i < count; i++)
      {
         mathRead( *stream, &pos );
         MeshRoadProfileNode node(pos);
         node.setSmoothing( stream->readFlag() );
         mSideProfile.mNodes.push_back(node);

         if(i)
            mSideProfile.mSegMtrls.push_back(stream->readInt(3));
         else
            stream->readInt(3);
      }

      mSideProfile.generateNormals();
   }

   // NodeMask
   if ( stream->readFlag() )
   {
      if (stream->readFlag())
      {
         // Nodes have been passed in this update
         U32 count = stream->readInt( 16 );

         mNodes.clear();

         Point3F pos, normal;
         F32 width, depth;
         for ( U32 i = 0; i < count; i++ )
         {
            mathRead( *stream, &pos );
            stream->read( &width );   
            stream->read( &depth );
            mathRead( *stream, &normal );
            _addNode( pos, width, depth, normal );           
         }
      }
      else
      {
         // Nodes will arrive as events
         U32 id;
         stream->read( &id );

         // Check if the road's nodes made it here before we did.
         NodeListManager::NodeList* list = NULL;
         if ( gClientNodeListManager->findListById( id, &list, true) )
         {
            // Work with the completed list
            MeshRoadNodeList* roadList = dynamic_cast<MeshRoadNodeList*>( list );
            if (roadList)
               buildNodesFromList( roadList );

            delete list;
         }
         else
         {
            // Nodes have not yet arrived, so register our interest in the list
            MeshRoadNodeListNotify* notify = new MeshRoadNodeListNotify( this, id );
            gClientNodeListManager->registerNotification( notify );
         }
      }
   }

   if ( stream->readFlag() && isProperlyAdded() )   
      _regenerate();
}

void MeshRoad::setTransform( const MatrixF &mat )
{
   for ( U32 i = 0; i < mNodes.size(); i++ )
   {
      mWorldToObj.mulP( mNodes[i].point );
      mat.mulP( mNodes[i].point );
   }

   Parent::setTransform( mat );

   if ( mPhysicsRep )
      mPhysicsRep->setTransform( mat );

   // Regenerate and update the client
   _regenerate();
   setMaskBits( NodeMask | RegenMask );
}

void MeshRoad::setScale( const VectorF &scale )
{
   // We ignore scale requests from the editor
   // right now.

   //Parent::setScale( scale );
}

void MeshRoad::buildConvex(const Box3F& box, Convex* convex)
{
   if ( mSlices.size() < 2 )
      return;

   mConvexList->collectGarbage();
   mDebugConvex.clear();

   Box3F realBox = box;
   mWorldToObj.mul(realBox);
   realBox.minExtents.convolveInverse(mObjScale);
   realBox.maxExtents.convolveInverse(mObjScale);

   if (realBox.isOverlapped(getObjBox()) == false)
      return;

   U32 segmentCount = mSegments.size();

   U32 numConvexes ;
   U32 halfConvexes;
   U32 nextSegOffset = 2*mSideProfile.mNodes.size();
   U32 leftSideOffset = nextSegOffset/2;
   U32 k2, capIdx1, capIdx2, capIdx3;

   // Create convex(s) for each segment
   for ( U32 i = 0; i < segmentCount; i++ )
   {
      const MeshRoadSegment &segment = mSegments[i];

      // Is this segment overlapped?
      if ( !segment.getWorldBounds().isOverlapped( box ) )
         continue;

      // Each segment has 6 faces
      for ( U32 j = 0; j < 6; j++ )
      {
         // Only first segment has front face
         if ( j == 4 && i != 0 )
            continue;
         // Only last segment has back face
         if ( j == 5 && i != segmentCount-1 )
            continue;

         // The top and bottom sides have 2 convex(s)
         // The left, right, front, and back sides depend on the user-defined profile
         switch(j)
         {
            case 0:  numConvexes = 2; break;                                     // Top
            case 1:                                                              // Left
            case 2:  numConvexes = 2* (mSideProfile.mNodes.size()-1); break;     // Right
            case 3:  numConvexes = 2; break;                                     // Bottom
            case 4:                                                              // Front
            case 5:  numConvexes = mSideProfile.mCap.getNumTris(); break;        // Back
            default: numConvexes = 0;
         }

         halfConvexes = numConvexes/2;

         for ( U32 k = 0; k < numConvexes; k++ )
         {
            // See if this convex exists in the working set already...
            Convex* cc = 0;
            CollisionWorkingList& wl = convex->getWorkingList();
            for ( CollisionWorkingList* itr = wl.wLink.mNext; itr != &wl; itr = itr->wLink.mNext ) 
            {
               if ( itr->mConvex->getType() == MeshRoadConvexType )
               {
                  MeshRoadConvex *pConvex = static_cast<MeshRoadConvex*>(itr->mConvex);

                  if ( pConvex->pRoad == this &&
                       pConvex->segmentId == i &&
                       pConvex->faceId == j &&
                       pConvex->triangleId == k )           
                  {
                     cc = itr->mConvex;
                     break;
                  }
               }
            }
            if (cc)
               continue;

            Point3F a, b, c;

            // Top or Bottom
            if(j == 0 || j == 3)
            {
               // Get the triangle...
               U32 idx0 = gIdxArray[j][k][0];
               U32 idx1 = gIdxArray[j][k][1];
               U32 idx2 = gIdxArray[j][k][2];

               a = segment[idx0];
               b = segment[idx1];
               c = segment[idx2];
            }
            // Left Side
            else if(j == 1)
            {
               if(k >= halfConvexes)
               {
                  k2 = k + leftSideOffset - halfConvexes;
                  a = segment.slice1->verts[k2];
                  b = segment.slice0->verts[k2];
                  c = segment.slice1->verts[k2 + 1];
               }
               else
               {
                  k2 = k + leftSideOffset;
                  a = segment.slice0->verts[k2];
                  b = segment.slice0->verts[k2 + 1];
                  c = segment.slice1->verts[k2 + 1];
               }
            }
            // Right Side
            else if(j == 2)
            {
//             a.set(2*k, 2*k, 0.0f);
//             b.set(2*k, 2*k, 2.0f);
//             c.set(2*(k+1), 2*(k+1), 0.0f);

               if(k >= halfConvexes)
               {
                  k2 = k - halfConvexes;
                  a = segment.slice1->verts[k2];
                  b = segment.slice1->verts[k2 + 1];
                  c = segment.slice0->verts[k2];
               }
               else
               {
                  a = segment.slice0->verts[k];
                  b = segment.slice1->verts[k + 1];
                  c = segment.slice0->verts[k + 1];
               }
            }
            // Front
            else if(j == 4)
            {
               k2 = nextSegOffset + leftSideOffset - 1;

               capIdx1 = mSideProfile.mCap.getTriIdx(k, 0);
               capIdx2 = mSideProfile.mCap.getTriIdx(k, 1);
               capIdx3 = mSideProfile.mCap.getTriIdx(k, 2);

               if(capIdx1 >= leftSideOffset)
                  capIdx1 = k2 - capIdx1;

               if(capIdx2 >= leftSideOffset)
                  capIdx2 = k2 - capIdx2;

               if(capIdx3 >= leftSideOffset)
                  capIdx3 = k2 - capIdx3;

               a = segment.slice0->verts[capIdx1];
               b = segment.slice0->verts[capIdx2];
               c = segment.slice0->verts[capIdx3];
            }
            // Back
            else
            {
               k2 = nextSegOffset + leftSideOffset - 1;

               capIdx1 = mSideProfile.mCap.getTriIdx(k, 0);
               capIdx2 = mSideProfile.mCap.getTriIdx(k, 1);
               capIdx3 = mSideProfile.mCap.getTriIdx(k, 2);

               if(capIdx1 >= leftSideOffset)
                  capIdx1 = k2 - capIdx1;

               if(capIdx2 >= leftSideOffset)
                  capIdx2 = k2 - capIdx2;

               if(capIdx3 >= leftSideOffset)
                  capIdx3 = k2 - capIdx3;

               a = segment.slice1->verts[capIdx3];
               b = segment.slice1->verts[capIdx2];
               c = segment.slice1->verts[capIdx1];
            }
            
            // Transform the result into object space!
            //mWorldToObj.mulP( a );
            //mWorldToObj.mulP( b );
            //mWorldToObj.mulP( c );            

            PlaneF p( c, b, a );
            Point3F peak = ((a + b + c) / 3.0f) + (p * 0.15f);

            // Set up the convex...
            MeshRoadConvex *cp = new MeshRoadConvex();

            mConvexList->registerObject( cp );
            convex->addToWorkingList( cp );

            cp->mObject    = this;
            cp->pRoad   = this;
            cp->segmentId = i;
            cp->faceId = j;
            cp->triangleId = k;

            cp->normal = p;
            cp->verts[0] = c;
            cp->verts[1] = b;
            cp->verts[2] = a;
            cp->verts[3] = peak;

            // Update the bounding box.
            Box3F &bounds = cp->box;
            bounds.minExtents.set( F32_MAX,  F32_MAX,  F32_MAX );
            bounds.maxExtents.set( -F32_MAX, -F32_MAX, -F32_MAX );

            bounds.minExtents.setMin( a );
            bounds.minExtents.setMin( b );
            bounds.minExtents.setMin( c );
            bounds.minExtents.setMin( peak );

            bounds.maxExtents.setMax( a );
            bounds.maxExtents.setMax( b );
            bounds.maxExtents.setMax( c );
            bounds.maxExtents.setMax( peak );

            mDebugConvex.push_back(cp);
         }
      }
   }
}

bool MeshRoad::buildPolyList( PolyListContext, AbstractPolyList* polyList, const Box3F &box, const SphereF & )
{
   if ( mSlices.size() < 2 )
      return false;

   polyList->setTransform( &MatrixF::Identity, Point3F::One );
   polyList->setObject(this);

   // JCF: optimize this to not always add everything.

   return buildSegmentPolyList( polyList, 0, mSegments.size() - 1, true, true );
}

bool MeshRoad::buildSegmentPolyList( AbstractPolyList* polyList, U32 startSegIdx, U32 endSegIdx, bool capFront, bool capEnd )
{
   if ( mSlices.size() < 2 )
      return false;

   // Add verts
   for ( U32 i = startSegIdx; i <= endSegIdx; i++ )
   {
      const MeshRoadSegment &seg = mSegments[i];

      if ( i == startSegIdx )
      {
         for(U32 j = 0; j < seg.slice0->verts.size(); j++)
            polyList->addPoint( seg.slice0->verts[j] );
      }

      for(U32 j = 0; j < seg.slice1->verts.size(); j++)
         polyList->addPoint( seg.slice1->verts[j] );
   }

   // Temporaries to hold indices for the corner points of a quad.
   S32 p00, p01, p11, p10;
   S32 pb00, pb01, pb11, pb10;
   U32 offset = 0;
   S32 a, b, c;
   U32 mirror;

   DebugDrawer *ddraw = NULL;//DebugDrawer::get();
   ClippedPolyList *cpolyList = dynamic_cast<ClippedPolyList*>(polyList);
   MatrixF mat;
   Point3F scale;
   if ( cpolyList )
      cpolyList->getTransform( &mat, &scale );

   U32 nextSegOffset = 2*mSideProfile.mNodes.size();
   U32 leftSideOffset = nextSegOffset/2;

   for ( U32 i = startSegIdx; i <= endSegIdx; i++ )
   {
      p00 = offset + leftSideOffset;
      p10 = offset;
      pb00 = offset + nextSegOffset - 1;
      pb10 = offset + leftSideOffset - 1;
      p01 = offset + nextSegOffset + leftSideOffset;
      p11 = offset + nextSegOffset;
      pb01 = offset + 2*nextSegOffset - 1;
      pb11 = offset + nextSegOffset + leftSideOffset - 1;

      // Top Face

      polyList->begin( 0,0 );
      polyList->vertex( p00 );
      polyList->vertex( p01 );
      polyList->vertex( p11 );
      polyList->plane( p00, p01, p11 );
      polyList->end();

      if ( ddraw && cpolyList )
      {
         Point3F v0 = cpolyList->mVertexList[p00].point;
         mat.mulP( v0 );
         Point3F v1 = cpolyList->mVertexList[p01].point;
         mat.mulP( v1 );
         Point3F v2 = cpolyList->mVertexList[p11].point;
         mat.mulP( v2 );
         ddraw->drawTri( v0, v1, v2 );
         ddraw->setLastZTest( false );
         ddraw->setLastTTL( 0 );
      }

      polyList->begin( 0,0 );
      polyList->vertex( p00 );
      polyList->vertex( p11 );
      polyList->vertex( p10 );
      polyList->plane( p00, p11, p10 );
      polyList->end();
      if ( ddraw && cpolyList )
      {
         ddraw->drawTri( cpolyList->mVertexList[p00].point, cpolyList->mVertexList[p11].point, cpolyList->mVertexList[p10].point );
         ddraw->setLastTTL( 0 );
      }

      if (buildPolyList_TopSurfaceOnly)
      {
         offset += 4;
         continue;
      }
      // Left Face
      for(U32 j = leftSideOffset; j < nextSegOffset-1; j++)
      {
         a = offset + j;
         b = a + nextSegOffset + 1;
         c = b - 1;

         polyList->begin( 0,0 );
         polyList->vertex( a );
         polyList->vertex( b );
         polyList->vertex( c);
         polyList->plane( a, b, c );
         polyList->end();

         a = offset + j;
         b = a + 1;
         c = a + nextSegOffset + 1;

         polyList->begin( 0,0 );
         polyList->vertex( a );
         polyList->vertex( b );
         polyList->vertex( c );
         polyList->plane( a, b, c );
         polyList->end();
      }

      // Right Face
      for(U32 j = 0; j < leftSideOffset-1; j++)
      {
         a = offset + j;
         b = a + nextSegOffset;
         c = b + 1;

         polyList->begin( 0,0 );
         polyList->vertex( a );
         polyList->vertex( b );
         polyList->vertex( c);
         polyList->plane( a, b, c );
         polyList->end();

         a = offset + j;
         b = a + nextSegOffset + 1;
         c = a + 1;

         polyList->begin( 0,0 );
         polyList->vertex( a );
         polyList->vertex( b );
         polyList->vertex( c );
         polyList->plane( a, b, c );
         polyList->end();
      }

      // Bottom Face

      polyList->begin( 0,0 );
      polyList->vertex( pb00 );
      polyList->vertex( pb10 );
      polyList->vertex( pb11 );
      polyList->plane( pb00, pb10, pb11 );
      polyList->end();

      polyList->begin( 0,0 );
      polyList->vertex( pb00 );
      polyList->vertex( pb11 );
      polyList->vertex( pb01 );
      polyList->plane( pb00, pb11, pb01 );
      polyList->end();  

      // Front Face

      if ( i == startSegIdx && capFront )
      {
         mirror = nextSegOffset + leftSideOffset - 1;

         for(U32 j = 0; j < mSideProfile.mCap.getNumTris(); j++)
         {
            a = mSideProfile.mCap.getTriIdx(j, 0);
            b = mSideProfile.mCap.getTriIdx(j, 1);
            c = mSideProfile.mCap.getTriIdx(j, 2);

            if(a >= leftSideOffset)
               a = mirror - a;

            if(b >= leftSideOffset)
               b = mirror - b;

            if(c >= leftSideOffset)
               c = mirror - c;

            polyList->begin( 0,0 );
            polyList->vertex( a );
            polyList->vertex( b );
            polyList->vertex( c );
            polyList->plane( a, b, c );
            polyList->end();
         }
      }

      // Back Face
      if ( i == endSegIdx && capEnd )
      {
         mirror = nextSegOffset + leftSideOffset - 1;

         for(U32 j = 0; j < mSideProfile.mCap.getNumTris(); j++)
         {
            a = mSideProfile.mCap.getTriIdx(j, 0);
            b = mSideProfile.mCap.getTriIdx(j, 1);
            c = mSideProfile.mCap.getTriIdx(j, 2);

            if(a >= leftSideOffset)
               a = offset + nextSegOffset + mirror - a;

            if(b >= leftSideOffset)
               b = offset + nextSegOffset + mirror - b;

            if(c >= leftSideOffset)
               c = offset + nextSegOffset + mirror - c;

            polyList->begin( 0,0 );
            polyList->vertex( c );
            polyList->vertex( b );
            polyList->vertex( a );
            polyList->plane( c, b, a );
            polyList->end();
         }
      }

      offset += nextSegOffset;
   }

   return true;
}

bool MeshRoad::castRay( const Point3F &s, const Point3F &e, RayInfo *info )
{
   Point3F start = s;
   Point3F end = e;
   mObjToWorld.mulP(start);
   mObjToWorld.mulP(end);

   F32 out = 1.0f;   // The output fraction/percentage along the line defined by s and e
   VectorF norm(0.0f, 0.0f, 0.0f);     // The normal of the face intersected
   
   Vector<MeshRoadHitSegment> hitSegments;

   for ( U32 i = 0; i < mSegments.size(); i++ )
   {
      const MeshRoadSegment &segment = mSegments[i];

      F32 t;
      VectorF n;

      if ( segment.getWorldBounds().collideLine( start, end, &t, &n ) )
      {
         hitSegments.increment();
         hitSegments.last().t = t;
         hitSegments.last().idx = i;         
      }
   }

   dQsort( hitSegments.address(), hitSegments.size(), sizeof(MeshRoadHitSegment), compareHitSegments );

   U32 idx0, idx1, idx2;
   F32 t;

   for ( U32 i = 0; i < hitSegments.size(); i++ )
   {
      U32 segIdx = hitSegments[i].idx;
      const MeshRoadSegment &segment = mSegments[segIdx];

      U32 numConvexes ;
      U32 halfConvexes;
      U32 nextSegOffset = 2*mSideProfile.mNodes.size();
      U32 leftSideOffset = nextSegOffset/2;
      U32 k2, capIdx1, capIdx2, capIdx3;

      // Each segment has 6 faces
      for ( U32 j = 0; j < 6; j++ )
      {
         if ( j == 4 && segIdx != 0 )
            continue;

         if ( j == 5 && segIdx != mSegments.size() - 1 )
            continue;

         // The top and bottom sides have 2 convex(s)
         // The left, right, front, and back sides depend on the user-defined profile
         switch(j)
         {
         case 0:  numConvexes = 2; break;                                  // Top
         case 1:                                                           // Left
         case 2:  numConvexes = 2* (mSideProfile.mNodes.size()-1); break;  // Right
         case 3:  numConvexes = 2; break;                                  // Bottom
         case 4:                                                           // Front
         case 5:  numConvexes = mSideProfile.mCap.getNumTris(); break;     // Back
         default: numConvexes = 0;
         }

         halfConvexes = numConvexes/2;

         // Each face has 2 triangles
         for ( U32 k = 0; k < numConvexes; k++ )
         {
            const Point3F *a = NULL;
            const Point3F *b = NULL;
            const Point3F *c = NULL;

            // Top or Bottom
            if(j == 0 || j == 3)
            {
               idx0 = gIdxArray[j][k][0];
               idx1 = gIdxArray[j][k][1];
               idx2 = gIdxArray[j][k][2];

               a = &segment[idx0];
               b = &segment[idx1];
               c = &segment[idx2];
            }
            // Left Side
            else if(j == 1)
            {
               if(k >= halfConvexes)
               {
                  k2 = k + leftSideOffset - halfConvexes;
                  a = &segment.slice1->verts[k2];
                  b = &segment.slice0->verts[k2];
                  c = &segment.slice1->verts[k2 + 1];
               }
               else
               {
                  k2 = k + leftSideOffset;
                  a = &segment.slice0->verts[k2];
                  b = &segment.slice0->verts[k2 + 1];
                  c = &segment.slice1->verts[k2 + 1];
               }
            }
            // Right Side
            else if(j == 2)
            {
               if(k >= halfConvexes)
               {
                  k2 = k - halfConvexes;
                  a = &segment.slice1->verts[k2];
                  b = &segment.slice1->verts[k2 + 1];
                  c = &segment.slice0->verts[k2];
               }
               else
               {
                  a = &segment.slice0->verts[k];
                  b = &segment.slice1->verts[k + 1];
                  c = &segment.slice0->verts[k + 1];
               }
            }
            // Front
            else if(j == 4)
            {
               k2 = nextSegOffset + leftSideOffset - 1;

               capIdx1 = mSideProfile.mCap.getTriIdx(k, 0);
               capIdx2 = mSideProfile.mCap.getTriIdx(k, 1);
               capIdx3 = mSideProfile.mCap.getTriIdx(k, 2);

               if(capIdx1 >= leftSideOffset)
                  capIdx1 = k2 - capIdx1;

               if(capIdx2 >= leftSideOffset)
                  capIdx2 = k2 - capIdx2;

               if(capIdx3 >= leftSideOffset)
                  capIdx3 = k2 - capIdx3;

               a = &segment.slice0->verts[capIdx1];
               b = &segment.slice0->verts[capIdx2];
               c = &segment.slice0->verts[capIdx3];
            }
            // Back
            else
            {
               k2 = nextSegOffset + leftSideOffset - 1;

               capIdx1 = mSideProfile.mCap.getTriIdx(k, 0);
               capIdx2 = mSideProfile.mCap.getTriIdx(k, 1);
               capIdx3 = mSideProfile.mCap.getTriIdx(k, 2);

               if(capIdx1 >= leftSideOffset)
                  capIdx1 = k2 - capIdx1;

               if(capIdx2 >= leftSideOffset)
                  capIdx2 = k2 - capIdx2;

               if(capIdx3 >= leftSideOffset)
                  capIdx3 = k2 - capIdx3;

               a = &segment.slice1->verts[capIdx3];
               b = &segment.slice1->verts[capIdx2];
               c = &segment.slice1->verts[capIdx1];
            }

            if ( !MathUtils::mLineTriangleCollide( start, end, 
                                                   *c, *b, *a,
                                                   NULL,
                                                   &t ) )
               continue;
            
            if ( t >= 0.0f && t < 1.0f && t < out )
            {
               out = t;               
               norm = PlaneF( *a, *b, *c );
            }
         }
      }

      if (out >= 0.0f && out < 1.0f)
         break;
   }

   if (out >= 0.0f && out < 1.0f)
   {
      info->t = out;
      info->normal = norm;
      info->point.interpolate(start, end, out);
      info->face = -1;
      info->object = this;
      info->material = this->mMatInst[0];
      return true;
   }

   return false;
}

bool MeshRoad::collideBox(const Point3F &start, const Point3F &end, RayInfo* info)
{   
   Con::warnf( "MeshRoad::collideBox() - not yet implemented!" );
   return Parent::collideBox( start, end, info );
}

void MeshRoad::_regenerate()
{               
   if ( mNodes.size() == 0 )
      return;

   if ( mSideProfile.mNodes.size() == 2 && mSideProfile.mNodes[1].getPosition().x == 0.0f)
      mSideProfile.setProfileDepth(mNodes[0].depth);

   const Point3F &nodePt = mNodes.first().point;

   MatrixF mat( true );
   mat.setPosition( nodePt );
   Parent::setTransform( mat );

   _generateSlices();

   // Make sure we are in the correct bins given our world box.
   if( getSceneManager() != NULL )
      getSceneManager()->notifyObjectDirty( this );
}

void MeshRoad::_generateSlices()
{      
   if ( mNodes.size() < 2 )
      return;

   // Create the spline, initialized with the MeshRoadNode(s)
   U32 nodeCount = mNodes.size();
   MeshRoadSplineNode *splineNodes = new MeshRoadSplineNode[nodeCount];

   for ( U32 i = 0; i < nodeCount; i++ )
   {
      MeshRoadSplineNode &splineNode = splineNodes[i];
      const MeshRoadNode &node = mNodes[i];

      splineNode.x = node.point.x;
      splineNode.y = node.point.y;
      splineNode.z = node.point.z;
      splineNode.width = node.width;
      splineNode.depth = node.depth;
      splineNode.normal = node.normal;
   }

   CatmullRom<MeshRoadSplineNode> spline;
   spline.initialize( nodeCount, splineNodes );
   delete [] splineNodes;

   mSlices.clear();
      
   VectorF lastBreakVector(0,0,0);      
   MeshRoadSlice slice;
   MeshRoadSplineNode lastBreakNode;
   lastBreakNode = spline.evaluate(0.0f);

   for ( U32 i = 1; i < mNodes.size(); i++ )
   {
      F32 t1 = spline.getTime(i);
      F32 t0 = spline.getTime(i-1);
      
      F32 segLength = spline.arcLength( t0, t1 );

      U32 numSegments = mCeil( segLength / MIN_METERS_PER_SEGMENT );
      numSegments = getMax( numSegments, (U32)1 );
      F32 tstep = ( t1 - t0 ) / numSegments; 

      U32 startIdx = 0;
      U32 endIdx = ( i == nodeCount - 1 ) ? numSegments + 1 : numSegments;

      for ( U32 j = startIdx; j < endIdx; j++ )
      {
         F32 t = t0 + tstep * j;
         MeshRoadSplineNode splineNode = spline.evaluate(t);

         VectorF toNodeVec = splineNode.getPosition() - lastBreakNode.getPosition();
         toNodeVec.normalizeSafe();

         if ( lastBreakVector.isZero() )
            lastBreakVector = toNodeVec;

         F32 angle = mRadToDeg( mAcos( mDot( toNodeVec, lastBreakVector ) ) );

         if ( j == startIdx || 
            ( j == endIdx - 1 && i == mNodes.size() - 1 ) ||
              angle > mBreakAngle )
         {
            // Push back a spline node
            slice.p1.set( splineNode.x, splineNode.y, splineNode.z );            
            slice.width = splineNode.width;
            slice.depth = splineNode.depth;
            slice.normal = splineNode.normal;   
            slice.normal.normalize();
            slice.parentNodeIdx = i-1;
            slice.t = t;
            mSlices.push_back( slice );         

            lastBreakVector = splineNode.getPosition() - lastBreakNode.getPosition();
            lastBreakVector.normalizeSafe();

            lastBreakNode = splineNode;
         }          
      }
   }
   
   MatrixF mat(true);
   Box3F box;

   U32 lastProfileNode = mSideProfile.mNodes.size() - 1;
   F32 depth = mSideProfile.mNodes[lastProfileNode].getPosition().y;
   F32 bttmOffset = mSideProfile.mNodes[lastProfileNode].getPosition().x;

   for ( U32 i = 0; i < mSlices.size(); i++ )
   {
      // Calculate uvec, fvec, and rvec for all slices
      calcSliceTransform( i, mat );
      MeshRoadSlice *slicePtr = &mSlices[i];
      mat.getColumn( 0, &slicePtr->rvec );
      mat.getColumn( 1, &slicePtr->fvec );
      mat.getColumn( 2, &slicePtr->uvec );

      // Calculate p0/p2/pb0/pb2 for all slices
      slicePtr->p0 = slicePtr->p1 - slicePtr->rvec * slicePtr->width * 0.5f;
      slicePtr->p2 = slicePtr->p1 + slicePtr->rvec * slicePtr->width * 0.5f;
      slicePtr->pb0 = slicePtr->p0 + slicePtr->uvec * depth - slicePtr->rvec * bttmOffset;
      slicePtr->pb2 = slicePtr->p2 + slicePtr->uvec * depth + slicePtr->rvec * bttmOffset;

     // Generate or extend the object/world bounds
      if ( i == 0 )
      {
         box.minExtents = slicePtr->p0;
         box.maxExtents = slicePtr->p2;
         box.extend(slicePtr->pb0 );
         box.extend(slicePtr->pb2 );
      }
      else
      {
         box.extend(slicePtr->p0 );
         box.extend(slicePtr->p2 );
         box.extend(slicePtr->pb0 );
         box.extend(slicePtr->pb2 );
      }

      // Right side
      Point3F pos;
      VectorF norm;

      MatrixF profileMat1(true);
      profileMat1.setRow(0, slicePtr->rvec);
      profileMat1.setRow(1, slicePtr->uvec);
      profileMat1.setRow(2, -slicePtr->fvec);

      // Left side
      MatrixF profileMat2(true);
      profileMat2.setRow(0, -slicePtr->rvec);
      profileMat2.setRow(1, slicePtr->uvec);
      profileMat2.setRow(2, slicePtr->fvec);

      for(U32 profile = 0; profile < 2; profile++)
      {
         if(profile)
            mSideProfile.setTransform(profileMat2, slicePtr->p0);
         else
            mSideProfile.setTransform(profileMat1, slicePtr->p2);

         // Retain original per-node depth functionality
         if(mSideProfile.mNodes.size() == 2 && mSideProfile.mNodes[1].getPosition().y == -mSlices[0].depth)
         {
            mSideProfile.getNodeWorldPos(0, pos);
            slicePtr->verts.push_back(pos);
            box.extend( pos );

            pos.z -= slicePtr->depth;
            slicePtr->verts.push_back(pos);
            box.extend( pos );

            if(profile)
               slicePtr->pb0 = pos;
            else
               slicePtr->pb2 = pos;

            mSideProfile.getNormToSlice(0, norm);
            slicePtr->norms.push_back(norm);

            mSideProfile.getNormToSlice(1, norm);
            slicePtr->norms.push_back(norm);
         }
         // New profile functionality
         else
         {
            for(U32 j = 0; j < mSideProfile.mNodes.size(); j++)
            {
               mSideProfile.getNodeWorldPos(j, pos);
               slicePtr->verts.push_back(pos);
               box.extend( pos );
            }

            for(U32 j = 0; j < mSideProfile.mNodeNormals.size(); j++)
            {
               mSideProfile.getNormToSlice(j, norm);
               slicePtr->norms.push_back(norm);
            }
         }
      }
   } 

   mWorldBox = box;
   resetObjectBox();

   _generateSegments();   
}

void MeshRoad::_generateSegments()
{
   SAFE_DELETE( mPhysicsRep );

   mSegments.clear();

   for ( U32 i = 0; i < mSlices.size() - 1; i++ )
   {
      MeshRoadSegment seg( &mSlices[i], &mSlices[i+1], getWorldTransform() );

      mSegments.push_back( seg );
   }

   //mSideProfile.generateEndCap(mSlices[0].width);

   if ( isClientObject() )
      _generateVerts();

   if ( PHYSICSMGR )
   {
      ConcretePolyList polylist;
      if ( buildPolyList( PLC_Collision, &polylist, getWorldBox(), getWorldSphere() ) )
      {
         polylist.triangulate();

         PhysicsCollision *colShape = PHYSICSMGR->createCollision();
         colShape->addTriangleMesh( polylist.mVertexList.address(),
            polylist.mVertexList.size(),
            polylist.mIndexList.address(),
            polylist.mIndexList.size() / 3,
            MatrixF::Identity );

         PhysicsWorld *world = PHYSICSMGR->getWorld( isServerObject() ? "server" : "client" );
         mPhysicsRep = PHYSICSMGR->createBody();
         mPhysicsRep->init( colShape, 0, 0, this, world );
      }
   }
}

void MeshRoad::_generateVerts()
{           
   const U32 widthDivisions = getMax( 0, mWidthSubdivisions );
   const F32 divisionStep = 1.0f / (F32)( widthDivisions + 1 );
   const U32 sliceCount = mSlices.size();
   const U32 segmentCount = mSegments.size();

   U32 numProfSide, numProfTop, numProfBottom;

   numProfSide = numProfTop = numProfBottom = 0;

   // Find how many profile segments are set to side, top, and bottom materials
   for ( U32 i = 0; i < mSideProfile.mSegMtrls.size(); i++)
   {
      switch(mSideProfile.mSegMtrls[i])
      {
      case Side:     numProfSide++;    break;
      case Top:      numProfTop++;     break;
      case Bottom:   numProfBottom++;  break;
      }
   }

   F32 profLen = mSideProfile.getProfileLen();

   mVertCount[Top] = ( 2 + widthDivisions ) * sliceCount;
   mVertCount[Top] += sliceCount * numProfTop * 4;
   mTriangleCount[Top] = segmentCount * 2 * ( widthDivisions + 1 );
   mTriangleCount[Top] += segmentCount * numProfTop * 4;

   mVertCount[Bottom] = sliceCount * 2;
   mVertCount[Bottom] += sliceCount * numProfBottom * 4;
   mTriangleCount[Bottom] = segmentCount * 2;
   mTriangleCount[Bottom] += segmentCount * numProfBottom * 4;

   mVertCount[Side] = sliceCount * numProfSide * 4;               // side verts
   mVertCount[Side] += mSideProfile.mNodes.size() * 4;            // end cap verts
   mTriangleCount[Side] = segmentCount * numProfSide * 4;         // side tris
   mTriangleCount[Side] += mSideProfile.mCap.getNumTris() * 2;    // end cap tris
   
   // Calculate TexCoords for Slices

   F32 texCoordV = 0.0f;
   mSlices[0].texCoordV = 0.0f;

   for ( U32 i = 1; i < sliceCount; i++ )
   {
      MeshRoadSlice &slice = mSlices[i]; 
      MeshRoadSlice &prevSlice = mSlices[i-1];
             
      // Increment the textCoordV for the next slice.
      F32 len = ( slice.p1 - prevSlice.p1 ).len();
      texCoordV += len / mTextureLength;         

      slice.texCoordV = texCoordV;
   }

   // Make Vertex Buffers
   GFXVertexPNTT *pVert = NULL;
   U32 vertCounter = 0;

   // Top Buffers...

   mVB[Top].set( GFX, mVertCount[Top], GFXBufferTypeStatic );   
   pVert = mVB[Top].lock(); 
   vertCounter = 0;
   
   for ( U32 i = 0; i < sliceCount; i++ )
   {
      MeshRoadSlice &slice = mSlices[i];      
      
      pVert->point = slice.p0;    
      pVert->normal = slice.uvec;
      pVert->tangent = slice.fvec;
      pVert->texCoord.set(1,slice.texCoordV);      
      pVert++;
      vertCounter++;

      for ( U32 j = 0; j < widthDivisions; j++ )
      {
         const F32 t = divisionStep * (F32)( j + 1 );

         pVert->point.interpolate( slice.p0, slice.p2, t );    
         pVert->normal = slice.uvec;
         pVert->tangent = slice.fvec;
         pVert->texCoord.set( 1.0f - t, slice.texCoordV );      
         pVert++;
         vertCounter++;
      }

      pVert->point = slice.p2;    
      pVert->normal = slice.uvec;
      pVert->tangent = slice.fvec;
      pVert->texCoord.set( 0, slice.texCoordV );      
      pVert++;
      vertCounter++;
   }

   if(numProfTop)
   {
      for ( U32 i = 0; i < sliceCount; i++ )
      {
         MeshRoadSlice &slice = mSlices[i];

         // Right Side
         for ( U32 j = 0; j < mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j] == Top)
            {
               // Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j+1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }

         // Left Side
         for( U32 j = mSideProfile.mNodes.size(); j < 2*mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j-mSideProfile.mNodes.size()] == Top)
            {
               // Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j-2];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j-1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }
      }
   }

   AssertFatal( vertCounter == mVertCount[Top], "MeshRoad, wrote incorrect number of verts in mVB[Top]!" );

   mVB[Top].unlock();

   // Bottom Buffer...

   mVB[Bottom].set( GFX, mVertCount[Bottom], GFXBufferTypeStatic );   
   pVert = mVB[Bottom].lock(); 
   vertCounter = 0;

   for ( U32 i = 0; i < sliceCount; i++ )
   {
      MeshRoadSlice &slice = mSlices[i];      

      pVert->point = slice.pb2;    
      pVert->normal = -slice.uvec;
      pVert->tangent = slice.fvec;
      pVert->texCoord.set(0,slice.texCoordV);      
      pVert++;
      vertCounter++;

      pVert->point = slice.pb0;    
      pVert->normal = -slice.uvec;
      pVert->tangent = slice.fvec;
      pVert->texCoord.set(1,slice.texCoordV);      
      pVert++;
      vertCounter++;
   }

   if(numProfBottom)
   {
      for ( U32 i = 0; i < sliceCount; i++ )
      {
         MeshRoadSlice &slice = mSlices[i];

         // Right Side
         for ( U32 j = 0; j < mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j] == Bottom)
            {
               // Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j+1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }

         // Left Side
         for( U32 j = mSideProfile.mNodes.size(); j < 2*mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j-mSideProfile.mNodes.size()] == Bottom)
            {
               // Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j-2];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j-1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }
      }
   }

   AssertFatal( vertCounter == mVertCount[Bottom], "MeshRoad, wrote incorrect number of verts in mVB[Bottom]!" );

   mVB[Bottom].unlock();

   // Side Buffers...

   mVB[Side].set( GFX, mVertCount[Side], GFXBufferTypeStatic );   
   pVert = mVB[Side].lock(); 
   vertCounter = 0;

   if(numProfSide)
   {
      for ( U32 i = 0; i < sliceCount; i++ )
      {
         MeshRoadSlice &slice = mSlices[i];

         // Right Side
         for( U32 j = 0; j < mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j] == Side)
            {
               // Segment Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Segment Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j+1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }

         // Left Side
         for( U32 j = mSideProfile.mNodes.size(); j < 2*mSideProfile.mNodes.size()-1; j++)
         {
            if(mSideProfile.mSegMtrls[j-mSideProfile.mNodes.size()] == Side)
            {
               // Segment Vertex 1
               pVert->point = slice.verts[j];
               pVert->normal = slice.norms[2*j-2];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;

               // Segment Vertex 2
               pVert->point = slice.verts[j+1];
               pVert->normal = slice.norms[2*j-1];
               pVert->tangent = slice.fvec;
               pVert->texCoord.set(mSideProfile.getNodePosPercent(j+1)*profLen/mTextureLength,slice.texCoordV);
               pVert++;
               vertCounter++;
            }
         }
      }
   }

   // Cap verts
   Point3F pos;
   VectorF norm;
   VectorF tang;

   for( U32 i = 0; i < mSlices.size(); i += mSlices.size()-1)
   {
      MeshRoadSlice &slice = mSlices[i];

      // Back cap
      if(i)
      {
         norm = slice.fvec;
         tang = -slice.rvec;
      }
      // Front cap
      else
      {
         norm = -slice.fvec;
         tang = slice.rvec;
      }

      // Right side
      for( U32 j = 0; j < mSideProfile.mNodes.size(); j++)
      {
         pVert->point = slice.verts[j];
         pVert->normal = norm;
         pVert->tangent = tang;
         pos = mSideProfile.mNodes[j].getPosition();
         pVert->texCoord.set(pos.x/mTextureLength, pos.y/mTextureLength);
         pVert++;
         vertCounter++;
      }

      // Left side
      for( U32 j = 2*mSideProfile.mNodes.size()-1; j >= mSideProfile.mNodes.size(); j--)
      {
         pVert->point = slice.verts[j];
         pVert->normal = norm;
         pVert->tangent = tang;
         pos = mSideProfile.mNodes[j-mSideProfile.mNodes.size()].getPosition();
         pos.x = -pos.x - slice.width;
         pVert->texCoord.set(pos.x/mTextureLength, pos.y/mTextureLength);
         pVert++;
         vertCounter++;
      }
   }

   AssertFatal( vertCounter == mVertCount[Side], "MeshRoad, wrote incorrect number of verts in mVB[Side]!" );

   mVB[Side].unlock();

   // Make Primitive Buffers   
   U32 p00, p01, p11, p10;
   U32 offset = 0;
   U16 *pIdx = NULL;   
   U32 curIdx = 0; 

   // Top Primitive Buffer

   mPB[Top].set( GFX, mTriangleCount[Top] * 3, mTriangleCount[Top], GFXBufferTypeStatic );

   mPB[Top].lock(&pIdx);     
   curIdx = 0; 
   offset = 0;

   const U32 rowStride = 2 + widthDivisions;
 
   for ( U32 i = 0; i < mSegments.size(); i++ )
   {		
      for ( U32 j = 0; j < widthDivisions + 1; j++ )
      {
         p00 = offset;
         p10 = offset + 1;
         p01 = offset + rowStride;
         p11 = offset + rowStride + 1;

         pIdx[curIdx] = p00;
         curIdx++;
         pIdx[curIdx] = p01;
         curIdx++;
         pIdx[curIdx] = p11;
         curIdx++;

         pIdx[curIdx] = p00;
         curIdx++;
         pIdx[curIdx] = p11;
         curIdx++;
         pIdx[curIdx] = p10;
         curIdx++;  

         offset += 1;
      }

      offset += 1;
   }

   offset += 2;

   if(numProfTop)
   {
      U32 nextSegOffset = 4 * numProfTop;

      for ( U32 i = 0; i < segmentCount; i++ )
      {
         // Loop through profile segments on right side
         for( U32 j = 0; j < numProfTop; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + 1 + offset;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
         }

         // Loop through profile segments on left side
         for( U32 j = numProfTop; j < 2*numProfTop; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + offset;
            curIdx++;
         }
      }
   }

   AssertFatal( curIdx == mTriangleCount[Top] * 3, "MeshRoad, wrote incorrect number of indices in mPB[Top]!" );

   mPB[Top].unlock();

   // Bottom Primitive Buffer

   mPB[Bottom].set( GFX, mTriangleCount[Bottom] * 3, mTriangleCount[Bottom], GFXBufferTypeStatic );

   mPB[Bottom].lock(&pIdx);     
   curIdx = 0; 
   offset = 0;

   for ( U32 i = 0; i < mSegments.size(); i++ )
   {		
      p00 = offset;
      p10 = offset + 1;
      p01 = offset + 2;
      p11 = offset + 3;

      pIdx[curIdx] = p00;
      curIdx++;
      pIdx[curIdx] = p01;
      curIdx++;
      pIdx[curIdx] = p11;
      curIdx++;

      pIdx[curIdx] = p00;
      curIdx++;
      pIdx[curIdx] = p11;
      curIdx++;
      pIdx[curIdx] = p10;
      curIdx++;      

      offset += 2;
   }

   offset += 2;

   if(numProfBottom)
   {
      U32 nextSegOffset = 4 * numProfBottom;

      for ( U32 i = 0; i < segmentCount; i++ )
      {
         // Loop through profile segments on right side
         for( U32 j = 0; j < numProfBottom; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + 1 + offset;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
         }

         // Loop through profile segments on left side
         for( U32 j = numProfBottom; j < 2*numProfBottom; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = nextSegOffset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + 1 + offset;
            curIdx++;
            pIdx[curIdx] = nextSegOffset*i + 2*j + nextSegOffset + offset;
            curIdx++;
         }
      }
   }

   AssertFatal( curIdx == mTriangleCount[Bottom] * 3, "MeshRoad, wrote incorrect number of indices in mPB[Bottom]!" );

   mPB[Bottom].unlock();

   // Side Primitive Buffer

   mPB[Side].set( GFX, mTriangleCount[Side] * 3, mTriangleCount[Side], GFXBufferTypeStatic );

   mPB[Side].lock(&pIdx);     
   curIdx = 0; 
   offset = 4 * numProfSide;

   if(numProfSide)
   {
      for ( U32 i = 0; i < mSegments.size(); i++ )
      {
         // Loop through profile segments on right side
         for( U32 j = 0; j < numProfSide; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = offset*i + 2*j;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset + 1;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + 1;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = offset*i + 2*j;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset + 1;
            curIdx++;
         }

         // Loop through profile segments on left side
         for( U32 j = numProfSide; j < 2*numProfSide; j++)
         {
            // Profile Segment Face 1
            pIdx[curIdx] = offset*i + 2*j;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + 1;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset + 1;
            curIdx++;

            // Profile Segment Face 2
            pIdx[curIdx] = offset*i + 2*j;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset + 1;
            curIdx++;
            pIdx[curIdx] = offset*i + 2*j + offset;
            curIdx++;
         }
      }
   }

   // Cap the front
   offset = sliceCount * numProfSide * 4;

   for ( U32 i = 0; i < mSideProfile.mCap.getNumTris(); i++ )
   {
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 0) + offset;
      curIdx++;
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 1) + offset;
      curIdx++;
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 2) + offset;
      curIdx++;
   }

   // Cap the back
   offset += mSideProfile.mNodes.size() * 2;

   for ( U32 i = 0; i < mSideProfile.mCap.getNumTris(); i++ )
   {
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 2) + offset;
      curIdx++;
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 1) + offset;
      curIdx++;
      pIdx[curIdx] = mSideProfile.mCap.getTriIdx(i, 0) + offset;
      curIdx++;
   }

   AssertFatal( curIdx == mTriangleCount[Side] * 3, "MeshRoad, wrote incorrect number of indices in mPB[Side]!" );

   mPB[Side].unlock();
}

const MeshRoadNode& MeshRoad::getNode( U32 idx )
{
   return mNodes[idx];
}

VectorF MeshRoad::getNodeNormal( U32 idx )
{
   if ( mNodes.size() - 1 < idx )
      return VectorF::Zero;

   return mNodes[idx].normal;
}

void MeshRoad::setNodeNormal( U32 idx, const VectorF &normal )
{
   if ( mNodes.size() - 1 < idx )
      return;

   mNodes[idx].normal = normal;

   regenerate();

   setMaskBits( NodeMask | RegenMask );
}

Point3F MeshRoad::getNodePosition( U32 idx )
{
   if ( mNodes.size() - 1 < idx )
      return Point3F::Zero;

   return mNodes[idx].point;
}

void MeshRoad::setNodePosition( U32 idx, const Point3F &pos )
{
   if ( mNodes.size() - 1 < idx )
      return;

   mNodes[idx].point = pos;

   regenerate();

   setMaskBits( NodeMask | RegenMask );
}

U32 MeshRoad::addNode( const Point3F &pos, const F32 &width, const F32 &depth, const VectorF &normal )
{
   U32 idx = _addNode( pos, width, depth, normal );   

   regenerate();

   setMaskBits( NodeMask | RegenMask );

   return idx;
}

void MeshRoad::buildNodesFromList( MeshRoadNodeList* list )
{
   mNodes.clear();

   for (U32 i=0; i<list->mPositions.size(); ++i)
   {
      _addNode( list->mPositions[i], list->mWidths[i], list->mDepths[i], list->mNormals[i] );
   }

   _regenerate();
}

U32 MeshRoad::insertNode( const Point3F &pos, const F32 &width, const F32 &depth, const VectorF &normal, const U32 &idx )
{
   U32 ret = _insertNode( pos, width, depth, normal, idx );

   regenerate();

   setMaskBits( NodeMask | RegenMask );

   return ret;
}

void MeshRoad::setNode( const Point3F &pos, const F32 &width, const F32 &depth, const VectorF &normal, const U32 &idx )
{
   if ( mNodes.size() - 1 < idx )
      return;

   MeshRoadNode &node = mNodes[idx];

   node.point = pos;
   node.width = width;
   node.depth = depth;
   node.normal = normal;

   regenerate();

   setMaskBits( NodeMask | RegenMask );
}

void MeshRoad::setNodeWidth( U32 idx, F32 meters )
{
   meters = mClampF( meters, MIN_NODE_WIDTH, MAX_NODE_WIDTH );

   if ( mNodes.size() - 1 < idx )
      return;

   mNodes[idx].width = meters;
   _regenerate();

   setMaskBits( RegenMask | NodeMask );
}

F32 MeshRoad::getNodeWidth( U32 idx )
{
   if ( mNodes.size() - 1 < idx )
      return -1.0f;

   return mNodes[idx].width;
}

void MeshRoad::setNodeDepth( U32 idx, F32 meters )
{
   meters = mClampF( meters, MIN_NODE_DEPTH, MAX_NODE_DEPTH );

   if ( mNodes.size() - 1 < idx )
      return;

   mNodes[idx].depth = meters;

   _regenerate();

   setMaskBits( MeshRoadMask | RegenMask | NodeMask );
}

F32 MeshRoad::getNodeDepth( U32 idx )
{
   if ( mNodes.size() - 1 < idx )
      return -1.0f;

   return mNodes[idx].depth;
}

MatrixF MeshRoad::getNodeTransform( U32 idx )
{   
   MatrixF mat(true);   

   if ( mNodes.size() - 1 < idx )
      return mat;

   bool hasNext = idx + 1 < mNodes.size();
   bool hasPrev = (S32)idx - 1 > 0;

   const MeshRoadNode &node = mNodes[idx];   

   VectorF fvec( 0, 1, 0 );

   if ( hasNext )
   {
      fvec = mNodes[idx+1].point - node.point;      
      fvec.normalizeSafe();
   }
   else if ( hasPrev )
   {
      fvec = node.point - mNodes[idx-1].point;
      fvec.normalizeSafe();
   }
   else
      fvec = mPerp( node.normal );

   if ( fvec.isZero() )
      fvec = mPerp( node.normal );

   F32 dot = mDot( fvec, node.normal );
   if ( dot < -0.9f || dot > 0.9f )
      fvec = mPerp( node.normal );

   VectorF rvec = mCross( fvec, node.normal );
   if ( rvec.isZero() )
      rvec = mPerp( fvec );
   rvec.normalize();

   fvec = mCross( node.normal, rvec );
   fvec.normalize();

   mat.setColumn( 0, rvec );
   mat.setColumn( 1, fvec );
   mat.setColumn( 2, node.normal );
   mat.setColumn( 3, node.point );

   AssertFatal( m_matF_determinant( mat ) != 0.0f, "no inverse!");

   return mat; 
}

void MeshRoad::calcSliceTransform( U32 idx, MatrixF &mat )
{   
   if ( mSlices.size() - 1 < idx )
      return;

   bool hasNext = idx + 1 < mSlices.size();
   bool hasPrev = (S32)idx - 1 >= 0;

   const MeshRoadSlice &slice = mSlices[idx];   

   VectorF fvec( 0, 1, 0 );

   if ( hasNext )
   {
      fvec = mSlices[idx+1].p1 - slice.p1;      
      fvec.normalizeSafe();
   }
   else if ( hasPrev )
   {
      fvec = slice.p1 - mSlices[idx-1].p1;
      fvec.normalizeSafe();
   }
   else
      fvec = mPerp( slice.normal );

   if ( fvec.isZero() )
      fvec = mPerp( slice.normal );

   F32 dot = mDot( fvec, slice.normal );
   if ( dot < -0.9f || dot > 0.9f )
      fvec = mPerp( slice.normal );

   VectorF rvec = mCross( fvec, slice.normal );
   if ( rvec.isZero() )
      rvec = mPerp( fvec );
   rvec.normalize();

   fvec = mCross( slice.normal, rvec );
   fvec.normalize();

   mat.setColumn( 0, rvec );
   mat.setColumn( 1, fvec );
   mat.setColumn( 2, slice.normal );
   mat.setColumn( 3, slice.p1 );

   AssertFatal( m_matF_determinant( mat ) != 0.0f, "no inverse!");
}

F32 MeshRoad::getRoadLength() const
{
   F32 length = 0.0f;

   for ( U32 i = 0; i < mSegments.size(); i++ )
   {
      length += mSegments[i].length();
   }

   return length;
}

void MeshRoad::deleteNode( U32 idx )
{
   if ( mNodes.size() - 1 < idx )
      return;

   mNodes.erase(idx);   
   _regenerate();

   setMaskBits( RegenMask | NodeMask );
}

U32 MeshRoad::_addNode( const Point3F &pos, const F32 &width, const F32 &depth, const VectorF &normal )
{
   mNodes.increment();
   MeshRoadNode &node = mNodes.last();

   node.point = pos;   
   node.width = width;
   node.depth = depth;
   node.normal = normal;

   setMaskBits( NodeMask | RegenMask );

   return mNodes.size() - 1;
}

U32 MeshRoad::_insertNode( const Point3F &pos, const F32 &width, const F32 &depth, const VectorF &normal, const U32 &idx )
{
   U32 ret;
   MeshRoadNode *node;

   if ( idx == U32_MAX )
   {
      mNodes.increment();
      node = &mNodes.last();
      ret = mNodes.size() - 1;
   }
   else
   {
      mNodes.insert( idx );
      node = &mNodes[idx];
      ret = idx;
   }

   node->point = pos;
   node->depth = depth;
   node->width = width;     
   node->normal = normal;

   return ret;
}

bool MeshRoad::collideRay( const Point3F &origin, const Point3F &direction, U32 *nodeIdx, Point3F *collisionPnt )
{
   Point3F p0 = origin;
   Point3F p1 = origin + direction * 2000.0f;

   // If the line segment does not collide with the MeshRoad's world box, 
   // it definitely does not collide with any part of the river.
   if ( !getWorldBox().collideLine( p0, p1 ) )
      return false;

   if ( mSlices.size() < 2 )
      return false;

   MathUtils::Quad quad;
   MathUtils::Ray ray;
   F32 t;

   // Check each road segment (formed by a pair of slices) for collision
   // with the line segment.
   for ( U32 i = 0; i < mSlices.size() - 1; i++ )
   {
      const MeshRoadSlice &slice0 = mSlices[i];
      const MeshRoadSlice &slice1 = mSlices[i+1];

      // For simplicities sake we will only test for collision between the
      // line segment and the Top face of the river segment.

      // Clockwise starting with the leftmost/closest point.
      quad.p00 = slice0.p0;
      quad.p01 = slice1.p0;
      quad.p11 = slice1.p2;
      quad.p10 = slice0.p2;

      ray.origin = origin;
      ray.direction = direction;

      if ( MathUtils::mRayQuadCollide( quad, ray, NULL, &t ) )
      {
         if ( nodeIdx )
            *nodeIdx = slice0.parentNodeIdx;         
         if ( collisionPnt )
            *collisionPnt = ray.origin + ray.direction * t;
         return true;
      }
   }

   return false;
}

void MeshRoad::regenerate()
{
   _regenerate();
   setMaskBits( RegenMask );
}

//-------------------------------------------------------------------------
// Console Methods
//-------------------------------------------------------------------------

DefineEngineMethod( MeshRoad, setNodeDepth, void, ( S32 idx, F32 meters ),,
                   "Intended as a helper to developers and editor scripts.\n"
                   "Sets the depth in meters of a particular node."
                   )
{
   object->setNodeDepth( idx, meters );
}

DefineEngineMethod( MeshRoad, regenerate, void, (),,
                   "Intended as a helper to developers and editor scripts.\n"
                   "Force MeshRoad to recreate its geometry."
                   )
{
   object->regenerate();
}

DefineEngineMethod( MeshRoad, postApply, void, (),,
                   "Intended as a helper to developers and editor scripts.\n"
                   "Force trigger an inspectPostApply. This will transmit "
                   "material and other fields ( not including nodes ) to client objects."
                   )
{
   object->inspectPostApply();
}
bool MeshRoad::buildPolyList_TopSurfaceOnly = false;

bool MeshRoad::buildTopPolyList(PolyListContext plc, AbstractPolyList* polyList)
{
   static Box3F box_prox; static SphereF ball_prox;

   buildPolyList_TopSurfaceOnly = true;
   bool result = buildPolyList(plc, polyList, box_prox, ball_prox);
   buildPolyList_TopSurfaceOnly = false;

   return result;
}

