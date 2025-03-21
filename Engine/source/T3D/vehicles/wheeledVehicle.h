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

#ifndef _WHEELEDVEHICLE_H_
#define _WHEELEDVEHICLE_H_

#ifndef _VEHICLE_H_
#include "T3D/vehicles/vehicle.h"
#endif

#ifndef _CLIPPEDPOLYLIST_H_
#include "collision/clippedPolyList.h"
#endif

#include "T3D/assets/ShapeAsset.h"

class ParticleEmitter;
class ParticleEmitterData;


//----------------------------------------------------------------------------

struct WheeledVehicleTire: public SimDataBlock 
{
   typedef SimDataBlock Parent;

   DECLARE_SHAPEASSET(WheeledVehicleTire, Shape, onShapeChanged);
   DECLARE_ASSET_SETGET(WheeledVehicleTire, Shape);

   // Physical properties
   F32 mass;                  // Mass of the whole wheel
   F32 kineticFriction;       // Tire friction coefficient
   F32 staticFriction;        // Tire friction coefficient
   F32 restitution;           // Currently not used

   // Tires act as springs and generate lateral and longitudinal
   // forces to move the vehicle. These distortion/spring forces
   // are what convert wheel angular velocity into forces that
   // act on the rigid body.
   F32 lateralForce;          // Spring force
   F32 lateralDamping;        // Damping force
   F32 lateralRelaxation;     // The tire will relax if left alone
   F32 longitudinalForce;
   F32 longitudinalDamping;
   F32 longitudinalRelaxation;

   // Shape information initialized in the preload
   F32 radius;                // Tire radius

   //
   WheeledVehicleTire();
   DECLARE_CONOBJECT(WheeledVehicleTire);
   static void initPersistFields();
   bool preload(bool, String &errorStr) override;
   void packData(BitStream* stream) override;
   void unpackData(BitStream* stream) override;

   void onShapeChanged() {}
};


//----------------------------------------------------------------------------

struct WheeledVehicleSpring: public SimDataBlock 
{
   typedef SimDataBlock Parent;

   F32 length;                // Travel distance from root hub position
   F32 force;                 // Spring force
   F32 damping;               // Damping force
   F32 antiSway;              // Opposite wheel anti-sway

   //
   WheeledVehicleSpring();
   DECLARE_CONOBJECT(WheeledVehicleSpring);
   static void initPersistFields();
   void packData(BitStream* stream) override;
   void unpackData(BitStream* stream) override;
};


//----------------------------------------------------------------------------

struct WheeledVehicleData: public VehicleData 
{
   typedef VehicleData Parent;

   enum Constants 
   {
      MaxWheels = 8,
      MaxWheelBits = 3
   };

   enum Sounds 
   {
      JetSound,
      EngineSound,
      SquealSound,
      WheelImpactSound,
      MaxSounds,
   };

   DECLARE_SOUNDASSET_ARRAY(WheeledVehicleData, WheeledVehicleSounds, Sounds::MaxSounds);

   ParticleEmitterData* tireEmitter;

   F32 maxWheelSpeed;            // Engine torque is scale based on wheel speed
   F32 engineTorque;             // Engine force controlled through throttle
   F32 engineBrake;              // Break force applied when throttle is 0
   F32 brakeTorque;              // Force used when brakeing

   // Initialized onAdd
   struct Wheel 
   {
      S32 opposite;              // Opposite wheel on Y axis (or -1 for none)
      Point3F pos;               // Root pos of spring
      S32 springNode;            // Wheel spring/hub node
      S32 springSequence;        // Suspension animation
      F32 springLength;          // Suspension animation length
   } wheel[MaxWheels];
   U32 wheelCount;
   ClippedPolyList rigidBody;    // Extracted from shape
   S32 brakeLightSequence;       // Brakes
   S32 steeringSequence;         // Steering animation
   F32 mDownForce;

   //
   WheeledVehicleData();
   DECLARE_CONOBJECT(WheeledVehicleData);
   static void initPersistFields();
   bool preload(bool, String &errorStr) override;
   bool mirrorWheel(Wheel* we);
   void packData(BitStream* stream) override;
   void unpackData(BitStream* stream) override;
};


//----------------------------------------------------------------------------

class WheeledVehicle: public Vehicle
{
   typedef Vehicle Parent;

   enum MaskBits 
   {
      WheelMask    = Parent::NextFreeMask << 0,
      NextFreeMask = Parent::NextFreeMask << 1
   };

   WheeledVehicleData* mDataBlock;

   bool mBraking;
   TSThread* mTailLightThread;
   SFXSource* mJetSound;
   SFXSource* mEngineSound;
   SFXSource* mSquealSound;

   struct Wheel 
   {
      WheeledVehicleTire *tire;
      WheeledVehicleSpring *spring;
      WheeledVehicleData::Wheel* data;

      F32 extension;          // Spring extension (0-1)
      F32 avel;               // Angular velocity
      F32 apos;               // Anuglar position (client side only)
      F32 Dy,Dx;              // Current tire deformation

      struct Surface 
      {
         bool contact;        // Wheel is touching a surface
         Point3F normal;      // Surface normal
         BaseMatInstance* material; // Surface material
         Point3F pos;         // Point of contact
         SceneObject* object; // Object in contact with
      } surface;

      TSShapeInstance* shapeInstance;
      TSThread* springThread;

      F32 steering;           // Wheel steering scale
      bool powered;           // Powered by engine
      bool slipping;          // Traction on last tick
      F32 torqueScale;        // Max torque % applied to wheel (0-1)
      F32 slip;               // Amount of wheel slip (0-1)
      SimObjectPtr<ParticleEmitter> emitter;
   };
   Wheel mWheel[WheeledVehicleData::MaxWheels];
   TSThread* mSteeringThread;

   //
   bool onNewDataBlock( GameBaseData *dptr, bool reload ) override;
   void processTick(const Move *move) override;
   void updateMove(const Move *move) override;
   void updateForces(F32 dt) override;
   void extendWheels(bool clientHack = false);
   void prepBatchRender( SceneRenderState *state, S32 mountedImageIndex ) override;

   // Client sounds & particles
   void updateWheelThreads();
   void updateWheelParticles(F32 dt);
   void updateEngineSound(F32 level);
   void updateSquealSound(F32 level);
   void updateJetSound();

   U32 getCollisionMask() override;

public:
   DECLARE_CONOBJECT(WheeledVehicle);
   DECLARE_CATEGORY("Actor \t Controllable");
   static void initPersistFields();

   WheeledVehicle();
   ~WheeledVehicle();

   bool onAdd() override;
   void onRemove() override;
   void advanceTime(F32 dt) override;
   bool buildPolyList(PolyListContext context, AbstractPolyList* polyList, const Box3F &box, const SphereF &sphere) override;

   S32 getWheelCount();
   Wheel *getWheel(U32 index) {return &mWheel[index];}
   void setWheelSteering(S32 wheel,F32 steering);
   void setWheelPowered(S32 wheel,bool powered);
   void setWheelTire(S32 wheel,WheeledVehicleTire*);
   void setWheelSpring(S32 wheel,WheeledVehicleSpring*);

   void getWheelInstAndTransform( U32 wheel, TSShapeInstance** inst, MatrixF* xfrm ) const;

   void writePacketData(GameConnection * conn, BitStream *stream) override;
   void readPacketData(GameConnection * conn, BitStream *stream) override;
   U32  packUpdate(NetConnection * conn, U32 mask, BitStream *stream) override;
   void unpackUpdate(NetConnection * conn, BitStream *stream) override;
};


#endif
