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

#ifndef _SCENEOBJECT_H_
#define _SCENEOBJECT_H_

#ifndef _NETOBJECT_H_
#include "sim/netObject.h"
#endif

#ifndef _COLLISION_H_
#include "collision/collision.h"
#endif

#ifndef _OBJECTTYPES_H_
#include "T3D/objectTypes.h"
#endif

#ifndef _COLOR_H_
#include "core/color.h"
#endif

#ifndef _BITSET_H_
#include "core/bitSet.h"
#endif

#ifndef _PROCESSLIST_H_
#include "T3D/gameBase/processList.h"
#endif

#ifndef _GFXDEVICE_H_
#include "gfx/gfxDevice.h"
#endif
#ifndef _TSRENDERDATA_H_
#include "ts/tsRenderState.h"
#endif

#ifndef _COLLADA_UTILS_H_
#include "ts/collada/colladaUtils.h"
#endif

#ifndef _ASSET_PTR_H_
#include "assets/assetPtr.h"
#endif 
#ifndef GAME_OBJECT_ASSET_H
#include "T3D/assets/GameObjectAsset.h"
#endif

#ifndef _SCENEQUERY_UTIL_H_
#include "scene/sceneQueryUtil.h"
#endif


class SceneManager;
class SceneRenderState;
class SceneTraversalState;
class SceneCameraState;
class SceneObjectLink;
class SceneObjectLightingPlugin;

class Convex;
class LightInfo;
class SFXAmbience;
class SceneContainer;

struct ObjectRenderInst;
struct Move;
struct SceneRayHelper;

/// A 3D object.
///
/// @section SceneObject_intro Introduction
///
/// SceneObject exists as a foundation for 3D objects in Torque. It provides the
/// basic functionality for:
///      - A scene graph (in the Zones and Portals sections), allowing efficient
///        and robust rendering of the game scene.
///      - Various helper functions, including functions to get bounding information
///        and momentum/velocity.
///      - Collision detection, as well as ray casting.
///      - Lighting. SceneObjects can register lights both at lightmap generation time,
///        and dynamic lights at runtime (for special effects, such as from flame or
///        a projectile, or from an explosion).
///      - Manipulating scene objects, for instance varying scale.
///
/// @section SceneObject_example An Example
///
/// Melv May has written a most marvelous example object deriving from SceneObject.
/// Unfortunately this page is too small to contain it.
///
/// @see http://www.garagegames.com/index.php?sec=mg&mod=resource&page=view&qid=3217
///      for a copy of Melv's example.
class SceneObject : public NetObject, public ProcessObject
{
   public:

      typedef NetObject Parent;

      friend class SceneManager;
      friend class SceneContainer;
      friend class SceneZoneSpaceManager;
      friend class SceneCullingState; // _getZoneRefHead
      friend class SceneObjectLink; // mSceneObjectLinks
      friend struct SceneRayHelper;

      enum 
      {
         /// Maximum number of zones that an object can concurrently be assigned to.
         MaxObjectZones = 128,

         NumMountPoints = 32,
         NumMountPointBits = 5,
      };
      
      /// Networking dirty mask.
      enum SceneObjectMasks
      {
         InitialUpdateMask = BIT( 0 ),
         ScaleMask         = BIT( 1 ),
         FlagMask          = BIT( 2 ),
         MountedMask       = BIT( 3 ),
         NextFreeMask      = BIT( 4 )
      };
      
      /// Bit-flags stored in mObjectFlags.
      /// If a derived class adds more flags they must overload 
      /// getObjectFlagMax to ensure those flags will be transmitted over
      /// the network.
      /// @see getObjectFlagMax
      enum SceneObjectFlags
      {
         /// If set, the object can be rendered.
         /// @note The per-class render disable flag can override the per-object flag.
         RenderEnabledFlag = BIT( 0 ),

         /// If set, the object can be selected in the editor.
         /// @note The per-class selection disable flag can override the per-object flag.
         SelectionEnabledFlag  = BIT( 1 ),

         /// If set, object will not be subjected to culling when in the editor.
         /// This is useful to bypass zone culling and always render certain editor-only
         /// visual elements (like the zones themselves).
         DisableCullingInEditorFlag = BIT( 2 ),

         /// If set, object will be used as a visual occluder.  In this case,
         /// the object should implement buildSilhouette() and return a
         /// *convex* silhouette polygon.
         VisualOccluderFlag = BIT( 3 ),

         /// If set, object will be used as a sound occluder.
         SoundOccluderFlag = BIT( 4 ),

         NextFreeFlag = BIT( 5 )
      };

   protected:

      /// Combination of SceneObjectFlags.
      BitSet32 mObjectFlags;

      /// SceneManager to which this SceneObject belongs.
      SceneManager* mSceneManager;

      /// Links installed by SceneTrackers attached to this object.
      SceneObjectLink* mSceneObjectLinks;

      /// SceneObjectLightingPlugin attached to this object.
      SceneObjectLightingPlugin* mLightPlugin;

      /// Object type mask.
      /// @see SimObjectTypes
      U32 mTypeMask;

      /// @name Mounting
      /// @{

      /// Mounted object.
      struct MountInfo
      {
         SceneObject* list;              ///< Linked-list of objects mounted on this object
         SceneObject* object;            ///< Object this object is mounted on.
         SceneObject* link;              ///< Link to next object mounted to this object's mount
         S32 node;                       ///< Node point we are mounted to.
         MatrixF xfm;
      };

      ///
      MountInfo mMount;

      ///
      SimPersistID* mMountPID;

      StringTableEntry		      mGameObjectAssetId;
      AssetPtr<GameObjectAsset>  mGameObjectAsset;

      //Marked if this entity is a GameObject and deliniates from the parent GO asset
      bool mDirtyGameObject;

      /// @}

      /// @name Zoning
      /// @{

      /// If an object moves, its zoning state needs to be updated.  This is deferred
      /// to when the state is actually needed and this flag indicates a refresh
      /// is necessary.
      bool mZoneRefDirty;

      /// Number of zones this object is assigned to.
      /// @note If #mZoneRefDirty is set, this might be outdated.
      U32 mNumCurrZones;

      /// Handle for the zone list of this object
      U32 mZoneListHandle;

      /// Refresh the zoning state of this object, if it isn't up-to-date anymore.
      void _updateZoningState();
      
      /// @}

      /// @name Transform and Collision Members
      /// @{

      // PATHSHAPE
      MatrixF mLastXform;
      // PATHSHAPE END

      /// Transform from object space to world space.
      MatrixF mObjToWorld;

      /// Transform from world space to object space (inverse).
      MatrixF mWorldToObj;

      /// Object scale.
      Point3F mObjScale;

      /// Bounding box in object space.
      Box3F mObjBox;

      /// Bounding box (AABB) in world space.
      Box3F mWorldBox;

      /// Bounding sphere in world space.
      SphereF mWorldSphere;

      /// Render matrix to transform object space to world space.
      MatrixF mRenderObjToWorld;

      /// Render matrix to transform world space to object space.
      MatrixF mRenderWorldToObj;

      /// Render bounding box in world space.
      Box3F mRenderWorldBox;

      /// Render bounding sphere in world space.
      SphereF mRenderWorldSphere;

      /// Whether this object is considered to have an infinite bounding box.
      bool mGlobalBounds;

      ///
      S32 mCollisionCount;

      /// Regenerates the world-space bounding box and bounding sphere.
      void resetWorldBox();

      /// Regenerates the render-world-space bounding box and sphere.
      void resetRenderWorldBox();

      /// Regenerates the object-space bounding box from the world-space
      /// bounding box, the world space to object space transform, and
      /// the object scale.
      void resetObjectBox();

      /// Called when the size of the object changes.
      virtual void onScaleChanged() {}

      /// @}

      /// Object which must be ticked before this object.
      SimObjectPtr< SceneObject > mAfterObject;

      /// @name SceneContainer Interface
      ///
      /// When objects are searched, we go through all the zones and ask them for
      /// all of their objects. Because an object can exist in multiple zones, the
      /// container sequence key is set to the id of the current search. Then, while
      /// searching, we check to see if an object's sequence key is the same as the
      /// current search key. If it is, it will NOT be added to the list of returns
      /// since it has already been processed.
      ///
      /// @{

      /// Container database that the object is assigned to.
      SceneContainer* mContainer;

      /// Assigned container index
      U32 mContainerIndex;

      /// Lookup Info
      SceneBinListLookup mContainerLookup;

      /// SceneContainer sequence key.
      U32 mContainerSeqKey;

      /// Returns the container sequence key.
      U32 getContainerSeqKey() const { return mContainerSeqKey; }

      /// Sets the container sequence key.
      void setContainerSeqKey( const U32 key ) { mContainerSeqKey = key;  }

      /// @}

      /// Called when this is added to a SceneManager.
      virtual bool onSceneAdd() { return true; }

      /// Called when this is removed from its current SceneManager.
      virtual void onSceneRemove() {}

      /// Returns the greatest object flag bit defined.
      /// Only bits within this range will be transmitted over the network.
      virtual U32 getObjectFlagMax() const { return NextFreeFlag - 1; }

   public:

      SceneObject();
      virtual ~SceneObject();
      bool mPathfindingIgnore;

      /// Triggered when a SceneObject onAdd is called.
      static Signal< void( SceneObject* ) > smSceneObjectAdd;

      /// Triggered when a SceneObject onRemove is called.
      static Signal< void( SceneObject* ) > smSceneObjectRemove;

      /// Return the type mask that indicates to which broad object categories
      /// this object belongs.
      U32 getTypeMask() const { return mTypeMask; }

      /// @name SceneManager Functionality
      /// @{

      /// Return the SceneManager that this SceneObject belongs to.
      SceneManager* getSceneManager() const { return mSceneManager; }

      /// Adds object to the client or server container depending on the object
      void addToScene();

      /// Removes the object from the client/server container
      void removeFromScene();

      /// Returns a pointer to the container that contains this object
      SceneContainer* getContainer() { return mContainer; }

      /// @}

      /// @name Flags
      /// @{

      /// Return true if this object is rendered.
      bool isRenderEnabled() const;

      /// Set whether the object gets rendered.
      void setRenderEnabled( bool value );

      /// Return true if this object can be selected in the editor.
      bool isSelectionEnabled() const;

      /// Set whether the object can be selected in the editor.
      void setSelectionEnabled( bool value );

      /// Return true if the object doesn't want to be subjected to culling
      /// when in the editor.
      bool isCullingDisabledInEditor() const { return mObjectFlags.test( DisableCullingInEditorFlag ); }

      /// Return true if the object should be taken into account for visual occlusion.
      bool isVisualOccluder() const { return mObjectFlags.test( VisualOccluderFlag ); }

      /// @}

      /// @name Collision and transform related interface
      ///
      /// The Render Transform is the interpolated transform with respect to the
      /// frame rate. The Render Transform will differ from the object transform
      /// because the simulation is updated in fixed intervals, which controls the
      /// object transform. The framerate is, most likely, higher than this rate,
      /// so that is why the render transform is interpolated and will differ slightly
      /// from the object transform.
      ///
      /// @{

      /// Disables collisions for this object including raycasts
      virtual void disableCollision();

      /// Enables collisions for this object
      virtual void enableCollision();

      /// Returns true if collisions are enabled
      inline bool isCollisionEnabled() const { return mCollisionCount == 0; }

      /// This gets called when an object collides with this object.
      /// @param   object   Object colliding with this object
      /// @param   vec   Vector along which collision occurred
      virtual void onCollision( SceneObject *object, const VectorF &vec ) {}

      /// Returns true if this object allows itself to be displaced
      /// @see displaceObject
      virtual bool isDisplacable() const { return false; }

      /// Returns the momentum of this object
      virtual Point3F getMomentum() const { return Point3F( 0, 0, 0 ); }

      /// Sets the momentum of this object
      /// @param   momentum   Momentum
      virtual void setMomentum( const Point3F& momentum ) {}

      /// Returns the mass of this object
      virtual F32 getMass() const { return 1.f; }

      /// Displaces this object by a vector
      /// @param   displaceVector   Displacement vector
      virtual bool displaceObject( const Point3F& displaceVector ) { return false; }

      /// Returns the transform which can be used to convert object space
      /// to world space
      virtual const MatrixF& getTransform() const { return mObjToWorld; }

      /// Returns the transform which can be used to convert world space
      /// into object space
      const MatrixF& getWorldTransform() const { return mWorldToObj; }

      /// Returns the scale of the object
      virtual const VectorF& getScale() const { return mObjScale; }

      /// Returns the bounding box for this object in local coordinates.
      const Box3F& getObjBox() const { return mObjBox; }

      /// Returns the bounding box for this object in world coordinates.
      const Box3F& getWorldBox() const { return mWorldBox; }

      /// Returns the bounding sphere for this object in world coordinates.
      const SphereF& getWorldSphere() const { return mWorldSphere; }

      /// Returns the center of the bounding box in world coordinates
      Point3F getBoxCenter() const { return ( mWorldBox.minExtents + mWorldBox.maxExtents ) * 0.5f; }

      /// Sets the Object -> World transform
      ///
      /// @param   mat   New transform matrix
      virtual void setTransform( const MatrixF &mat );

      /// Sets the scale for the object
      /// @param   scale   Scaling values
      virtual void setScale( const VectorF &scale );

      /// Sets the forward vector of the object
      void setForwardVector(VectorF newForward, VectorF upVector = VectorF(0, 0, 1));

      /// This sets the render transform for this object
      /// @param   mat   New render transform
      virtual void setRenderTransform(const MatrixF &mat);

      /// Returns the render transform
      const MatrixF& getRenderTransform() const { return mRenderObjToWorld; }

      /// Returns the render transform to convert world to local coordinates
      const MatrixF& getRenderWorldTransform() const { return mRenderWorldToObj; }

      /// Returns the render world box
      const Box3F& getRenderWorldBox()  const { return mRenderWorldBox; }

      /// Sets the state of this object as hidden or not. If an object is hidden
      /// it is removed entirely from collisions, it is not ghosted and is
      /// essentially "non existant" as far as simulation is concerned.
      /// @param   hidden   True if object is to be hidden
      void setHidden( bool hidden ) override;

      /// Builds a convex hull for this object.
      ///
      /// Think of a convex hull as a low-res mesh which covers, as tightly as
      /// possible, the object mesh, and is used as a collision mesh.
      /// @param   box
      /// @param   convex   Convex mesh generated (out)
      virtual void buildConvex( const Box3F& box,Convex* convex ) {}

      /// Builds a list of polygons which intersect a bounding volume.
      ///
      /// This will use either the sphere or the box, not both, the
      /// SceneObject implementation ignores sphere.
      ///
      /// @see AbstractPolyList
      /// @param   context    A contentual hint as to the type of polylist to build.
      /// @param   polyList   Poly list build (out)
      /// @param   box        Box bounding volume
      /// @param   sphere     Sphere bounding volume
      ///
      virtual bool buildPolyList(   PolyListContext context, 
                                    AbstractPolyList* polyList, 
                                    const Box3F& box, 
                                    const SphereF& sphere ) { return false; }

      /// Builds a list of polygons which intersect a bounding volume for exporting
      ///
      /// This will use either the sphere or the box, not both, the
      /// SceneObject implementation ignores sphere.
      ///
      /// @see AbstractPolyList
      /// @param   context    A contentual hint as to the type of polylist to build.
      /// @param   polyList   Poly list build (out)
      /// @param   box        Box bounding volume
      /// @param   sphere     Sphere bounding volume
      ///
      virtual bool buildExportPolyList(ColladaUtils::ExportData *exportData,
         const Box3F& box,
         const SphereF& sphere) {
         return false;
      }

      /// Casts a ray and obtain collision information, returns true if RayInfo is modified.
      ///
      /// @param   start   Start point of ray
      /// @param   end   End point of ray
      /// @param   info   Collision information obtained (out)
      virtual bool castRay( const Point3F& start, const Point3F& end, RayInfo* info ) { return false; }

      /// Casts a ray against rendered geometry, returns true if RayInfo is modified.
      ///
      /// @param   start   Start point of ray
      /// @param   end     End point of ray
      /// @param   info    Collision information obtained (out)
      virtual bool castRayRendered( const Point3F& start, const Point3F& end, RayInfo* info );

      /// Build a world-space silhouette polygon for the object for the given camera settings.
      /// This is used for occlusion.
      ///
      /// @param cameraState Camera view parameters.
      /// @param outPoints Vector to store the resulting polygon points in.  Leave untouched
      ///   if method is not implemented.
      virtual void buildSilhouette( const SceneCameraState& cameraState, Vector< Point3F >& outPoints ) {}

      /// Return true if the given point is contained by the object's (collision) shape.
      ///
      /// The default implementation will return true if the point is within the object's
      /// bounding box.  Subclasses should implement more precise tests.
      virtual bool containsPoint( const Point3F &point );
      
      virtual bool collideBox( const Point3F& start, const Point3F& end, RayInfo* info );

      /// Returns the position of the object.
      virtual Point3F getPosition() const;

      /// Returns the render-position of the object.
      ///
      /// @see getRenderTransform
      Point3F getRenderPosition() const;

      /// Sets the position of the object
      void setPosition ( const Point3F& pos );

      /// Gets the velocity of the object.   
      virtual Point3F getVelocity() const { return Point3F::Zero; }

      /// Sets the velocity of the object
      /// @param  v  Velocity   
      virtual void setVelocity( const Point3F &v ) {}

      /// Applies an impulse force to this object
      /// @param   pos   Position where impulse came from in world space
      /// @param   vec   Velocity vector (Impulse force F = m * v)   
      virtual void applyImpulse( const Point3F &pos, const VectorF &vec ) {}

      /// Applies a radial impulse to the object
      /// using the impulse origin and force.
      /// @param origin Point of origin of the radial impulse.
      /// @param radius The radius of the impulse area.
      /// @param magnitude The strength of the impulse.   
      virtual void applyRadialImpulse( const Point3F &origin, F32 radius, F32 magnitude ) {}

      /// Returns the distance from this object to a point   
      /// @param pnt World space point to measure to
      virtual F32 distanceTo( const Point3F &pnt ) const;

      /// @}

      /// @name Mounting
      /// @{

      /// ex: Mount B to A at A's node N
      /// A.mountObject( B, N )
      /// 
      /// @param   obj   Object to mount
      /// @param   node  Mount node ID
      virtual void mountObject( SceneObject *obj, S32 node, const MatrixF &xfm = MatrixF::Identity );

      /// Remove an object mounting
      /// @param   obj   Object to unmount
      virtual void unmountObject( SceneObject *obj );

      /// Unmount this object from it's mount
      virtual void unmount();    

      /// Callback when this object is mounted.
      /// @param obj Object we are mounting to.
      /// @param node Node we are unmounting from.
      virtual void onMount( SceneObject *obj, S32 node );

      /// Callback when this object is unmounted. This should be overridden to
      /// set maskbits or do other object type specific work.
      /// @param obj Object we are unmounting from.
      /// @param node Node we are unmounting from.
      virtual void onUnmount( SceneObject *obj, S32 node );

      // Returns mount point to world space transform at tick time.
      virtual void getMountTransform( S32 index, const MatrixF &xfm, MatrixF *outMat );

      // Returns mount point to world space transform at render time.
      // Note this will only be correct if called after this object has interpolated.
      virtual void getRenderMountTransform( F32 delta, S32 index, const MatrixF &xfm, MatrixF *outMat );

      /// Return the object that this object is mounted to.
      virtual SceneObject* getObjectMount() { return mMount.object; }

      /// Return object link of next object mounted to this object's mount
      virtual SceneObject* getMountLink() { return mMount.link; }

      /// Returns object list of objects mounted to this object.
      virtual SceneObject* getMountList() { return mMount.list; }

      /// Returns the mount id that this is mounted to.
      virtual U32 getMountNode() { return mMount.node; }

      /// Returns true if this object is mounted to anything at all
      /// Also try to resolve the PID to objectId here if it is pending.
      virtual bool isMounted();

      /// Returns the number of object mounted along with this
      virtual S32 getMountedObjectCount();

      /// Returns the object mounted at a position in the mount list
      /// @param   idx   Position on the mount list
      virtual SceneObject* getMountedObject( S32 idx );

      /// Returns the node the object at idx is mounted to
      /// @param   idx   Index
      virtual S32 getMountedObjectNode( S32 idx );

      /// Returns the object a object on the mount list is mounted to
      /// @param   node
      virtual SceneObject* getMountNodeObject( S32 node );

      void resolveMountPID();

      /// @}
      
      /// @name Sound
      /// @{
      
      /// Return whether the object's collision shape is blocking sound.
      bool isOccludingSound() const { return mObjectFlags.test( SoundOccluderFlag ); }
      
      /// Return the ambient sound space active inside the volume of this object or NULL if the object does
      /// not have its own ambient space.
      virtual SFXAmbience* getSoundAmbience() const { return NULL; }
      
      /// @}

      /// @name Rendering
      /// @{

      /// Called when the SceneManager is ready for the registration of render instances.
      /// @param state Rendering state.
      virtual void prepRenderImage( SceneRenderState* state ) {}

      /// @}

      /// @name Lighting
      /// @{

      void setLightingPlugin( SceneObjectLightingPlugin* plugin ) { mLightPlugin = plugin; }
      SceneObjectLightingPlugin* getLightingPlugin() { return mLightPlugin; }

      /// Gets the number of zones containing this object.
      U32 getNumCurrZones() const { return mNumCurrZones; }

      /// Returns the nth zone containing this object.
      U32 getCurrZone(const U32 index);

      /// @}   

      /// @name Global Bounds
      /// @{

      const bool isGlobalBounds() const
      {
         return mGlobalBounds;
      }

      /// If global bounds are set to be true, then the object is assumed to
      /// have an infinitely large bounding box for collision and rendering
      /// purposes.
      ///
      /// They can't be toggled currently.
      void setGlobalBounds();

      /// @}

      /// Return the ProcessList for this object to use.
      ProcessList* getProcessList() const;

      // ProcessObject,
      void processAfter( ProcessObject *obj ) override;
      void clearProcessAfter() override;
      ProcessObject* getAfterObject() const override { return mAfterObject; }
      void setProcessTick( bool t ) override;

      // NetObject.
      U32 packUpdate( NetConnection* conn, U32 mask, BitStream* stream ) override;
      void unpackUpdate( NetConnection* conn, BitStream* stream ) override;
      void onCameraScopeQuery( NetConnection* connection, CameraScopeQuery* query ) override;

      // SimObject.
      bool onAdd() override;
      void onRemove() override;
      void onDeleteNotify( SimObject *object ) override;
      void inspectPostApply() override;
      bool writeField( StringTableEntry fieldName, const char* value ) override;

      static void initPersistFields();

      static bool _setGameObject(void* object, const char* index, const char* data);

      DECLARE_CONOBJECT( SceneObject );
      DECLARE_CATEGORY("MISC");
   private:

      SceneObject( const SceneObject& ); ///< @deprecated disallowed

      /// For ScopeAlways objects to be able to properly implement setHidden(), they
      /// need to temporarily give up ScopeAlways status while being hidden.  Otherwise
      /// the client-side ghost will not disappear as the server-side object will be
      /// forced to stay in scope.
      bool mIsScopeAlways;

      /// @name Protected field getters/setters
      /// @{

      static const char* _getRenderEnabled( void *object, const char *data );
      static bool _setRenderEnabled( void *object, const char *index, const char *data );
      static const char* _getSelectionEnabled( void *object, const char *data );
      static bool _setSelectionEnabled( void *object, const char *index, const char *data );
      static bool _setFieldPosition( void *object, const char *index, const char *data );
      static bool _setFieldRotation( void *object, const char *index, const char *data );
      static bool _setFieldScale( void *object, const char *index, const char *data );
      static bool _setMountPID( void* object, const char* index, const char* data );
      static bool _setAccuEnabled( void *object, const char *index, const char *data );

      /// @}
// PATHSHAPE
   /// @}
   //Anthony's Original Code, still used so i keep it here
   /// TGE uses the term "mount" in a quirky, staticky way relating to its limited use to have
   /// riders and guns mounted on a vehicle (and similar)
   /// I did not alter that code at all (yet) and did not want to keep its terminology for other reasons
   /// I decided to support a hierarchy of scene objects and dubbed the operations 
   /// attaching and removing child SceneObjects
  protected:

      // this member struct tracks the relationship to parent and children
      // sceneObjects in a hierarchical scene graph whose root is the entire Scene
   struct AttachInfo {
      SceneObject* firstChild;  ///< Objects mounted on this object
      SimObjectPtr<SceneObject>  parent; ///< Object this object is mounted on.
      SceneObject* nextSibling;        ///< Link to next child object of this object's parent 
      MatrixF      objToParent;   ///< this obects transformation in the parent object's space
      MatrixF      RenderobjToParent;   ///< this obects Render Offset transformation to the parent object
      AttachInfo() {
         firstChild = NULL;
         parent = NULL;
         nextSibling = NULL;
         objToParent.identity();
         RenderobjToParent.identity();
      };
   } mGraph;
// PATHSHAPE END


   // Accumulation Texture
   // Note: This was placed in SceneObject to both ShapeBase and TSStatic could support it.
   public:
      GFXTextureObject* mAccuTex;
      //   mSelectionFlags field keeps track of flags related to object selection.
      //     PRE_SELECTED marks an object as pre-selected (object under cursor)
      //     SELECTED marks an object as selected (a target)
   protected:
      U8 mSelectionFlags;
   public:
      enum { 
         SELECTED      = BIT(0), 
         PRE_SELECTED  = BIT(1), 
      };
      virtual void setSelectionFlags(U8 flags) { mSelectionFlags = flags; }
      U8 getSelectionFlags() const { return mSelectionFlags; }
      bool needsSelectionHighlighting() const { return (mSelectionFlags != 0); }
      //   This should only return true if the object represents an independent camera
      //   as opposed to something like a Player that has a built-in camera that requires
      //   special calculations to determine the view transform.
      virtual bool isCamera() const { return false; }
      // AFX CODE BLOCK (is-camera) >>
// PATHSHAPE
// Added for dynamic attaching
	void UpdateXformChange(const MatrixF &mat);
      /// this is useful for setting NULL parent (making SceneObject a root object)
   virtual bool attachToParent(SceneObject *parent, MatrixF *atThisOffset = NULL, S32 node=0);
   SceneObject *getParent() { return mGraph.parent; };

   
   /// attach a subobject, but do not alter the subObject's present absolute position or orientation
   bool attachChild(SceneObject* subObject);   
   /// attach a subobject, at the specified offset expressed in our local coordinate space
   bool attachChildAt(SceneObject* subObject, MatrixF atThisTransform, S32 node);   

   /// attach a subobject, at the specified position expressed in our local coordinate space
   bool attachChildAt(SceneObject* subObject, Point3F atThisPosition);   
   
   /// how many child SceneObjects are (directly) attached to this one?
   U32 getNumChildren() const;

   /// how many child objects does this SceneObject have when we count them recursively?
   U32 getNumProgeny() const;

   /// returns the (direct) child SceneObject at the given index (0 <= index <= getNumChildren() - 1)
   SceneObject *getChild(U32 index) const; 
   
   /// is this SceneObject a child (directly or indirectly) of the given object?
   bool isChildOf(SceneObject *);

   /// set position in parent SceneObject's coordinate space (or in world space if no parent)
   //void setLocalPosition(const Point3F &pos);

   /// move the object in parent SceneObject's coordinate space (or in world space if no parent)
   //void localMove(const Point3F &delta);
   /// as localMove(const Point3F &delta), with different signature
   //void localMove(F32 x, F32 y, F32 z);
   
   /// move the object in world space, without altering place in scene hierarchy
   void move(const Point3F &delta);
   
   // Does checks for children objects and updates their positions
   void PerformUpdatesForChildren(MatrixF mat);
   
   // Move the RenderTransform
   void moveRender(const Point3F &delta);
   //Calculate how much to adjust the render transform - Called by the child objects
   void updateRenderChangesByParent();
   //Calculate how much to adjust the transform - Called by the parent object
   void updateChildTransform(); 
   /// as move(const Point3F &delta), with different signature   
   void move(F32 x, F32 y, F32 z);

   /// returns the transform relative to parent SceneObject transform (or world transform if no parent)
   //const MatrixF& getLocalTransform() const;
   /// returns the position within parent SceneObject space (or world space if no parent)
   //Point3F getLocalPosition() const;

   inline U32 getRootContainerIndex() { return mContainerIndex;  }
   inline const SceneBinListLookup getContainerLookupInfo() { return mContainerLookup; }
   
//   virtual void onParentScaleChanged();   
//   virtual void onParentTransformChanged();
      
   /// Sets the Object -> Parent transform.  If no parent SceneObject, this is equivalent to 
   ///  setTransform()
   ///
   /// @param   mat   New transform matrix
   //virtual void setLocalTransform(const MatrixF & mat);


   /// Called to let instance specific code happen 
   virtual void onLostParent(SceneObject *oldParent);
   DECLARE_CALLBACK(void, onLostParent, (SceneObject *oldParent));
   /// Called to let instance specific code happen 
   virtual void onNewParent(SceneObject *newParent);
   DECLARE_CALLBACK(void, onNewParent, (SceneObject *oldParent));
   /// notification that a direct child object has been attached
   virtual void onNewChild(SceneObject *subObject);
   DECLARE_CALLBACK(void, onNewChild, (SceneObject *subObject));
   /// notification that a direct child object has been detached
   virtual void onLostChild(SceneObject *subObject);
   DECLARE_CALLBACK(void, onLostChild, (SceneObject *subObject));
// PATHSHAPE END

   DECLARE_CALLBACK(void, onSaving, (const char* fileName));

   virtual void getUtilizedAssets(Vector<StringTableEntry>* usedAssetsList) {}
};

#endif  // _SCENEOBJECT_H_

