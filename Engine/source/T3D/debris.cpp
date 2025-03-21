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
#include "T3D/debris.h"

#include "core/stream/bitStream.h"
#include "math/mathUtils.h"
#include "console/consoleTypes.h"
#include "console/consoleObject.h"
#include "sim/netConnection.h"
#include "scene/sceneRenderState.h"
#include "scene/sceneManager.h"
#include "ts/tsShapeInstance.h"
#include "ts/tsPartInstance.h"
#include "T3D/fx/particleEmitter.h"
#include "T3D/fx/explosion.h"
#include "T3D/gameBase/gameProcess.h"
#include "core/resourceManager.h"
#include "gfx/gfxTransformSaver.h"
#include "console/engineAPI.h"
#include "lighting/lightQuery.h"


const U32 csmStaticCollisionMask = TerrainObjectType | StaticShapeObjectType | StaticObjectType;

IMPLEMENT_CO_DATABLOCK_V1(DebrisData);

ConsoleDocClass( DebrisData,
   "@brief Stores properties for an individual debris type.\n\n"

   "DebrisData defines the base properties for a Debris object.  Typically you'll want a Debris object to consist of "
   "a shape and possibly up to two particle emitters.  The DebrisData datablock provides the definition for these items, "
   "along with physical properties and how a Debris object will react to other game objects, such as water and terrain.\n"

   "@tsexample\n"
   "datablock DebrisData(GrenadeDebris)\n"
   "{\n"
   "   shapeFile = \"art/shapes/weapons/ramrifle/debris.dts\";\n"
   "   emitters[0] = GrenadeDebrisFireEmitter;\n"
   "   elasticity = 0.4;\n"
   "   friction = 0.25;\n"
   "   numBounces = 3;\n"
   "   bounceVariance = 1;\n"
   "   explodeOnMaxBounce = false;\n"
   "   staticOnMaxBounce = false;\n"
   "   snapOnMaxBounce = false;\n"
   "   minSpinSpeed = 200;\n"
   "   maxSpinSpeed = 600;\n"
   "   lifetime = 4;\n"
   "   lifetimeVariance = 1.5;\n"
   "   velocity = 15;\n"
   "   velocityVariance = 5;\n"
   "   fade = true;\n"
   "   useRadiusMass = true;\n"
   "   baseRadius = 0.3;\n"
   "   gravModifier = 1.0;\n"
   "   terminalVelocity = 20;\n"
   "   ignoreWater = false;\n"
   "};\n"
   "@endtsexample\n\n"
   "@see Debris\n\n"
   "@ingroup FX\n"
);

DebrisData::DebrisData()
{
   dMemset( emitterList, 0, sizeof( emitterList ) );
   dMemset( emitterIDList, 0, sizeof( emitterIDList ) );

   explosion = NULL;
   explosionId = 0;

   velocity = 0.0f;
   velocityVariance = 0.0;
   elasticity = 0.3f;
   friction   = 0.2f;
   numBounces = 0;
   bounceVariance = 0;
   staticOnMaxBounce = false;
   explodeOnMaxBounce = false;
   snapOnMaxBounce = false;
   lifetime = 3.0f;
   lifetimeVariance = 0.0f;
   minSpinSpeed = 0.0f;
   maxSpinSpeed = 0.0f;
   textureName = NULL;
   fade = true;
   useRadiusMass = false;
   baseRadius = 1.0f;
   gravModifier = 1.0f;
   terminalVelocity = 0.0f;
   ignoreWater = true;

   INIT_ASSET(Shape);
}

//#define TRACK_DEBRIS_DATA_CLONES

#ifdef TRACK_DEBRIS_DATA_CLONES
static int debris_data_clones = 0;
#endif

DebrisData::DebrisData(const DebrisData& other, bool temp_clone) : GameBaseData(other, temp_clone)
{
#ifdef TRACK_DEBRIS_DATA_CLONES
   debris_data_clones++;
   if (debris_data_clones == 1)
      Con::errorf("DebrisData -- Clones are on the loose!");
#endif
   velocity = other.velocity;
   velocityVariance = other.velocityVariance;
   friction = other.friction;
   elasticity = other.elasticity;
   lifetime = other.lifetime;
   lifetimeVariance = other.lifetimeVariance;
   numBounces = other.numBounces;
   bounceVariance = other.bounceVariance;
   minSpinSpeed = other.minSpinSpeed;
   maxSpinSpeed = other.maxSpinSpeed;
   explodeOnMaxBounce = other.explodeOnMaxBounce;
   staticOnMaxBounce = other.staticOnMaxBounce;
   snapOnMaxBounce = other.snapOnMaxBounce;
   fade = other.fade;
   useRadiusMass = other.useRadiusMass;
   baseRadius = other.baseRadius;
   gravModifier = other.gravModifier;
   terminalVelocity = other.terminalVelocity;
   ignoreWater = other.ignoreWater;

   CLONE_ASSET(Shape);

   textureName = other.textureName;
   explosionId = other.explosionId; // -- for pack/unpack of explosion ptr
   explosion = other.explosion;
   dMemcpy( emitterList, other.emitterList, sizeof( emitterList ) );
   dMemcpy( emitterIDList, other.emitterIDList, sizeof( emitterIDList ) ); // -- for pack/unpack of emitterList ptrs
}

DebrisData::~DebrisData()
{
   if (!isTempClone())
      return;

#ifdef TRACK_DEBRIS_DATA_CLONES
   if (debris_data_clones > 0)
   {
      debris_data_clones--;
      if (debris_data_clones == 0)
         Con::errorf("DebrisData -- Clones eliminated!");
   }
   else
      Con::errorf("DebrisData -- Too many clones deleted!");
#endif
}

DebrisData* DebrisData::cloneAndPerformSubstitutions(const SimObject* owner, S32 index)
{
   if (!owner || getSubstitutionCount() == 0)
      return this;

   DebrisData* sub_debris_db = new DebrisData(*this, true);
   performSubstitutions(sub_debris_db, owner, index);

   return sub_debris_db;
}

void DebrisData::onPerformSubstitutions() 
{ 
   _setShape(getShape());
}

bool DebrisData::onAdd()
{
   if(!Parent::onAdd())
      return false;

   for( S32 i=0; i<DDC_NUM_EMITTERS; i++ )
   {
      if( !emitterList[i] && emitterIDList[i] != 0 )
      {
         if( Sim::findObject( emitterIDList[i], emitterList[i] ) == false)
         {
            Con::errorf( ConsoleLogEntry::General, "DebrisData::onAdd: Invalid packet, bad datablockId(emitter): 0x%x", emitterIDList[i]);
         }
      }
   }

   if (!explosion && explosionId != 0)
   {
      if (!Sim::findObject( SimObjectId( explosionId ), explosion ))
            Con::errorf( ConsoleLogEntry::General, "DebrisData::onAdd: Invalid packet, bad datablockId(particle emitter): 0x%x", explosionId);
   }

   // validate data

   if( velocityVariance > velocity )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: velocityVariance invalid", getName());
      velocityVariance = velocity;
   }
   if( friction < -10.0f || friction > 10.0f )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: friction invalid", getName());
      friction = 0.2f;
   }
   if( elasticity < -10.0f || elasticity > 10.0f )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: elasticity invalid", getName());
      elasticity = 0.2f;
   }
   if( lifetime < 0.0f || lifetime > 1000.0f )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: lifetime invalid", getName());
      lifetime = 3.0f;
   }
   if( lifetimeVariance < 0.0f || lifetimeVariance > lifetime )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: lifetimeVariance invalid", getName());
      lifetimeVariance = 0.0f;
   }
   if( numBounces < 0 || numBounces > 10000 )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: numBounces invalid", getName());
      numBounces = 3;
   }
   if( bounceVariance < 0 || bounceVariance > numBounces )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: bounceVariance invalid", getName());
      bounceVariance = 0;
   }
   if( minSpinSpeed < -10000.0f || minSpinSpeed > 10000.0f || minSpinSpeed > maxSpinSpeed )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: minSpinSpeed invalid", getName());
      minSpinSpeed = maxSpinSpeed - 1.0f;
   }
   if( maxSpinSpeed < -10000.0f || maxSpinSpeed > 10000.0f )
   {
      Con::warnf(ConsoleLogEntry::General, "DebrisData(%s)::onAdd: maxSpinSpeed invalid", getName());
      maxSpinSpeed = 0.0f;
   }

   return true;
}

bool DebrisData::preload(bool server, String &errorStr)
{
   if (Parent::preload(server, errorStr) == false)
      return false;

   if( server ) return true;

   if (mShapeAsset.notNull())
   {
      if (!mShape)
      {
         errorStr = String::ToString("DebrisData::load: Couldn't load shape \"%s\"", mShapeAssetId);
         return false;
      }
      else
      {
         TSShapeInstance* pDummy = new TSShapeInstance(mShape, !server);
         delete pDummy;
         if (!server && !mShape->preloadMaterialList(mShape.getPath()) && NetConnection::filesWereDownloaded())
            return false;
      }
   }

   return true;
}

FRangeValidator debElasticityRange(-10.0f, 10.0f);
FRangeValidator debFrictionRange(-10.0f, 10.0f);
IRangeValidator debBounceRange(0, 10000);
FRangeValidator debSpinSpeedRange(-10000.0f, 10000.0f);
FRangeValidator debLifetimeRange(0.0f, 1000.0f);
void DebrisData::initPersistFields()
{
   docsURL;
   addGroup("Shapes");
      addField("texture",              TypeString,                  Offset(textureName,         DebrisData), 
         "@brief Texture imagemap to use for this debris object.\n\nNot used any more.\n", AbstractClassRep::FIELD_HideInInspectors);
      INITPERSISTFIELD_SHAPEASSET(Shape, DebrisData, "Shape to use for this debris object.");
   endGroup("Shapes");

   addGroup("Particle Effects");
      addField("emitters",             TYPEID< ParticleEmitterData >(),  Offset(emitterList,    DebrisData), DDC_NUM_EMITTERS, 
         "@brief List of particle emitters to spawn along with this debris object.\n\nThese are optional.  You could have Debris made up of only a shape.\n");
   endGroup("Particle Effects");
   addGroup("Datablocks");
   addField("explosion",            TYPEID< ExplosionData >(),   Offset(explosion,           DebrisData), 
      "@brief ExplosionData to spawn along with this debris object.\n\nThis is optional as not all Debris explode.\n");
   endGroup("Datablocks");

   addGroup("Physics");
   addFieldV("elasticity",           TypeRangedF32,                     Offset(elasticity,          DebrisData), &debElasticityRange,
      "@brief A floating-point value specifying how 'bouncy' this object is.\n\nMust be in the range of -10 to 10.\n");
   addFieldV("friction", TypeRangedF32,                     Offset(friction,            DebrisData), &debFrictionRange,
      "@brief A floating-point value specifying how much velocity is lost to impact and sliding friction.\n\nMust be in the range of -10 to 10.\n");
   addFieldV("numBounces",           TypeRangedS32,                     Offset(numBounces,          DebrisData), &debBounceRange,
      "@brief How many times to allow this debris object to bounce until it either explodes, becomes static or snaps (defined in explodeOnMaxBounce, staticOnMaxBounce, snapOnMaxBounce).\n\n"
      "Must be within the range of 0 to 10000.\n"
      "@see bounceVariance\n");
   addFieldV("bounceVariance", TypeRangedS32,                     Offset(bounceVariance,      DebrisData), &debBounceRange,
      "@brief Allowed variance in the value of numBounces.\n\nMust be less than numBounces.\n@see numBounces\n");
   addFieldV("minSpinSpeed", TypeRangedF32,                     Offset(minSpinSpeed,        DebrisData),&debSpinSpeedRange,
      "@brief Minimum speed that this debris object will rotate.\n\nMust be in the range of -10000 to 1000, and must be less than maxSpinSpeed.\n@see maxSpinSpeed\n");
   addFieldV("maxSpinSpeed", TypeRangedF32,                     Offset(maxSpinSpeed,        DebrisData), &debSpinSpeedRange,
      "@brief Maximum speed that this debris object will rotate.\n\nMust be in the range of -10000 to 10000.\n@see minSpinSpeed\n");
   addFieldV("gravModifier", TypeRangedF32,                     Offset(gravModifier,        DebrisData), &CommonValidators::F32Range, "How much gravity affects debris.");
   addFieldV("terminalVelocity", TypeRangedF32,                     Offset(terminalVelocity,    DebrisData), &CommonValidators::PositiveFloat, "Max velocity magnitude.");
   addFieldV("velocity", TypeRangedF32,                     Offset(velocity,            DebrisData), &CommonValidators::PositiveFloat,
      "@brief Speed at which this debris object will move.\n\n@see velocityVariance\n");
   addFieldV("velocityVariance", TypeRangedF32,                     Offset(velocityVariance,    DebrisData), &CommonValidators::PositiveFloat,
      "@brief Allowed variance in the value of velocity\n\nMust be less than velocity.\n@see velocity\n");
   addFieldV("lifetime",             TypeRangedF32,                     Offset(lifetime,            DebrisData), &debLifetimeRange,
      "@brief Amount of time until this debris object is destroyed.\n\nMust be in the range of 0 to 1000.\n@see lifetimeVariance");
   addFieldV("lifetimeVariance", TypeRangedF32,                     Offset(lifetimeVariance,    DebrisData), &debLifetimeRange,
      "@brief Allowed variance in the value of lifetime.\n\nMust be less than lifetime.\n@see lifetime\n");
   addField("useRadiusMass",        TypeBool,                    Offset(useRadiusMass,       DebrisData), 
      "@brief Use mass calculations based on radius.\n\nAllows for the adjustment of elasticity and friction based on the Debris size.\n@see baseRadius\n");
   addFieldV("baseRadius", TypeRangedF32,                     Offset(baseRadius,          DebrisData), &CommonValidators::PositiveFloat,
      "@brief Radius at which the standard elasticity and friction apply.\n\nOnly used when useRaduisMass is true.\n@see useRadiusMass.\n");
   endGroup("Physics");

   addGroup("Behavior");
   addField("explodeOnMaxBounce",   TypeBool,                    Offset(explodeOnMaxBounce,  DebrisData), 
      "@brief If true, this debris object will explode after it has bounced max times.\n\nBe sure to provide an ExplosionData datablock for this to take effect.\n@see explosion\n");
   addField("staticOnMaxBounce",    TypeBool,                    Offset(staticOnMaxBounce,   DebrisData), "If true, this debris object becomes static after it has bounced max times.");
   addField("snapOnMaxBounce",      TypeBool,                    Offset(snapOnMaxBounce,     DebrisData), "If true, this debris object will snap into a resting position on the last bounce.");
   addField("fade",                 TypeBool,                    Offset(fade,                DebrisData), 
      "@brief If true, this debris object will fade out when destroyed.\n\nThis fade occurs over the last second of the Debris' lifetime.\n");
   addField("ignoreWater",          TypeBool,                    Offset(ignoreWater,         DebrisData), "If true, this debris object will not collide with water, acting as if the water is not there.");
   endGroup("Behavior");

   // disallow some field substitutions
   onlyKeepClearSubstitutions("emitters"); // subs resolving to "~~", or "~0" are OK
   onlyKeepClearSubstitutions("explosion");
   Parent::initPersistFields();
}

void DebrisData::packData(BitStream* stream)
{
   Parent::packData(stream);

   stream->write(elasticity);
   stream->write(friction);
   stream->write(numBounces);
   stream->write(bounceVariance);
   stream->write(minSpinSpeed);
   stream->write(maxSpinSpeed);
   stream->write(explodeOnMaxBounce);
   stream->write(staticOnMaxBounce);
   stream->write(snapOnMaxBounce);
   stream->write(lifetime);
   stream->write(lifetimeVariance);
   stream->write(velocity);
   stream->write(velocityVariance);
   stream->write(fade);
   stream->write(useRadiusMass);
   stream->write(baseRadius);
   stream->write(gravModifier);
   stream->write(terminalVelocity);
   stream->write(ignoreWater);

   stream->writeString( textureName );

   PACKDATA_ASSET(Shape);

   for( S32 i=0; i<DDC_NUM_EMITTERS; i++ )
   {
      if( stream->writeFlag( emitterList[i] != NULL ) )
      {
         stream->writeRangedU32( emitterList[i]->getId(), DataBlockObjectIdFirst,  DataBlockObjectIdLast );
      }
   }

   if( stream->writeFlag( explosion ) )
   {
      stream->writeRangedU32(mPacked ? SimObjectId((uintptr_t)explosion):
         explosion->getId(),DataBlockObjectIdFirst,DataBlockObjectIdLast);
   }

}

void DebrisData::unpackData(BitStream* stream)
{
   Parent::unpackData(stream);

   stream->read(&elasticity);
   stream->read(&friction);
   stream->read(&numBounces);
   stream->read(&bounceVariance);
   stream->read(&minSpinSpeed);
   stream->read(&maxSpinSpeed);
   stream->read(&explodeOnMaxBounce);
   stream->read(&staticOnMaxBounce);
   stream->read(&snapOnMaxBounce);
   stream->read(&lifetime);
   stream->read(&lifetimeVariance);
   stream->read(&velocity);
   stream->read(&velocityVariance);
   stream->read(&fade);
   stream->read(&useRadiusMass);
   stream->read(&baseRadius);
   stream->read(&gravModifier);
   stream->read(&terminalVelocity);
   stream->read(&ignoreWater);

   textureName = stream->readSTString();

   UNPACKDATA_ASSET(Shape);

   for( S32 i=0; i<DDC_NUM_EMITTERS; i++ )
   {
      if( stream->readFlag() )
      {
         emitterIDList[i] = stream->readRangedU32( DataBlockObjectIdFirst, DataBlockObjectIdLast );
      }
   }

   if(stream->readFlag())
   {
      explosionId = (S32)stream->readRangedU32(DataBlockObjectIdFirst, DataBlockObjectIdLast);
   }
   else
   {
      explosionId = 0;
   }

}


IMPLEMENT_CO_NETOBJECT_V1(Debris);

ConsoleDocClass( Debris,
   "@brief Base debris class. Uses the DebrisData datablock for properties of individual debris objects.\n\n"

   "Debris is typically made up of a shape and up to two particle emitters.  In most cases Debris objects are "
   "not created directly.  They are usually produced automatically by other means, such as through the Explosion "
   "class.  When an explosion goes off, its ExplosionData datablock determines what Debris to emit.\n"
   
   "@tsexample\n"
   "datablock ExplosionData(GrenadeLauncherExplosion)\n"
   "{\n"
   "   // Assiging debris data\n"
   "   debris = GrenadeDebris;\n\n"
   "   // Adjust how debris is ejected\n"
   "   debrisThetaMin = 10;\n"
   "   debrisThetaMax = 60;\n"
   "   debrisNum = 4;\n"
   "   debrisNumVariance = 2;\n"
   "   debrisVelocity = 25;\n"
   "   debrisVelocityVariance = 5;\n\n"
   "   // Note: other ExplosionData properties are not listed for this example\n"
   "};\n"
   "@endtsexample\n\n"

   "@note Debris are client side only objects.\n"

   "@see DebrisData\n"
   "@see ExplosionData\n"
   "@see Explosion\n"

   "@ingroup FX\n"
);

DefineEngineMethod(Debris, init, bool, (const char* inputPosition, const char* inputVelocity),
   ("1.0 1.0 1.0", "1.0 0.0 0.0"), 
   "@brief Manually set this piece of debris at the given position with the given velocity.\n\n"

   "Usually you do not manually create Debris objects as they are generated through other means, "
   "such as an Explosion.  This method exists when you do manually create a Debris object and "
   "want to have it start moving.\n"

   "@param inputPosition Position to place the debris.\n"
   "@param inputVelocity Velocity to move the debris after it has been placed.\n"
   "@return Always returns true.\n"

   "@tsexample\n"
      "// Define the position\n"
      "%position = \"1.0 1.0 1.0\";\n\n"
      "// Define the velocity\n"
      "%velocity = \"1.0 0.0 0.0\";\n\n"
      "// Inform the debris object of its new position and velocity\n"
      "%debris.init(%position,%velocity);\n"
   "@endtsexample\n")
{
   Point3F pos;
   dSscanf( inputPosition, "%f %f %f", &pos.x, &pos.y, &pos.z );

   Point3F vel;
   dSscanf( inputVelocity, "%f %f %f", &vel.x, &vel.y, &vel.z );

   object->init( pos, vel );

   return true;
}

Debris::Debris()
{
   mTypeMask |= DebrisObjectType | DynamicShapeObjectType;

   mVelocity = Point3F( 0.0f, 0.0f, 4.0f );
   mLifetime = gRandGen.randF( 1.0f, 10.0f );
   mLastPos =  getPosition();
   mNumBounces = gRandGen.randI( 0, 1 );
   mSize = 2.0f;
   mElapsedTime = 0.0f;
   mShape = NULL;
   mPart = NULL;
   mDataBlock = NULL;
   mXRotSpeed = 0.0f;
   mZRotSpeed = 0.0f;
   mInitialTrans.identity();
   mRadius = 0.2f;
   mStatic = false;
   mElasticity = 0.5f;
   mFriction = 0.5f;

   dMemset( mEmitterList, 0, sizeof( mEmitterList ) );

   // Only allocated client side.
   mNetFlags.set( IsGhost );
   ss_object = 0;
   ss_index = 0;
}

Debris::~Debris()
{
   if( mShape )
   {
      delete mShape;
      mShape = NULL;
   }

   if( mPart )
   {
      delete mPart;
      mPart = NULL;
   }
   
   if (mDataBlock && mDataBlock->isTempClone())
   { 
      delete mDataBlock;
      mDataBlock = 0;
   }
}

void Debris::initPersistFields()
{
   docsURL;
   addGroup( "Debris" );	
   
      addFieldV( "lifetime", TypeRangedF32, Offset(mLifetime, Debris), &CommonValidators::PositiveFloat,
         "@brief Length of time for this debris object to exist. When expired, the object will be deleted.\n\n"
         "The initial lifetime value comes from the DebrisData datablock.\n"
         "@see DebrisData::lifetime\n"
         "@see DebrisData::lifetimeVariance\n");
      
   endGroup( "Debris" );
   
   Parent::initPersistFields();
}

void Debris::init( const Point3F &position, const Point3F &velocity )
{
   setPosition( position );
   setVelocity( velocity );
}

bool Debris::onNewDataBlock( GameBaseData *dptr, bool reload )
{
   mDataBlock = dynamic_cast< DebrisData* >( dptr );
   if( !mDataBlock || !Parent::onNewDataBlock( dptr, reload ) )
      return false;

   if (mDataBlock->isTempClone())
      return true;
   scriptOnNewDataBlock();
   return true;

}

bool Debris::onAdd()
{
   if( !Parent::onAdd() )
   {
      return false;
   }

   if( !mDataBlock )
   {
      Con::errorf("Debris::onAdd - Fail - No datablock");
      return false;
   }

   // create emitters
   for( S32 i=0; i<DebrisData::DDC_NUM_EMITTERS; i++ )
   {
      if( mDataBlock->emitterList[i] != NULL )
      {
         ParticleEmitter * pEmitter = new ParticleEmitter;
         pEmitter->onNewDataBlock(mDataBlock->emitterList[i]->cloneAndPerformSubstitutions(ss_object, ss_index), false);
         if( !pEmitter->registerObject() )
         {
            Con::warnf( ConsoleLogEntry::General, "Could not register emitter for particle of class: %s", mDataBlock->getName() );
            delete pEmitter;
            pEmitter = NULL;
         }
         mEmitterList[i] = pEmitter;
      }
   }

   // set particle sizes based on debris size
   F32 sizeList[ParticleData::PDC_NUM_KEYS];

   if( mEmitterList[0] )
   {
      sizeList[0] = mSize * 0.5;
      sizeList[1] = mSize;
      for (U32 i = 2; i < ParticleData::PDC_NUM_KEYS; i++)
         sizeList[i] = mSize * 1.5;

      mEmitterList[0]->setSizes( sizeList );
   }

   if( mEmitterList[1] )
   {
      sizeList[0] = 0.0;
      sizeList[1] = mSize * 0.5;
      for (U32 i = 2; i < ParticleData::PDC_NUM_KEYS; i++)
         sizeList[i] = mSize;

      mEmitterList[1]->setSizes( sizeList );
   }

   S32 bounceVar = (S32)mDataBlock->bounceVariance;
   bounceVar = gRandGen.randI( -bounceVar, bounceVar );
   mNumBounces = mDataBlock->numBounces + bounceVar;

   F32 lifeVar = (mDataBlock->lifetimeVariance * 2.0f * gRandGen.randF(-1.0,1.0)) - mDataBlock->lifetimeVariance;
   mLifetime = mDataBlock->lifetime + lifeVar;

   F32 xRotSpeed = gRandGen.randF( mDataBlock->minSpinSpeed, mDataBlock->maxSpinSpeed );
   F32 zRotSpeed = gRandGen.randF( mDataBlock->minSpinSpeed, mDataBlock->maxSpinSpeed );
   zRotSpeed *= gRandGen.randF( 0.1f, 0.5f );

   mRotAngles.set( xRotSpeed, 0.0f, zRotSpeed );

   mElasticity = mDataBlock->elasticity;
   mFriction = mDataBlock->friction;

   // Setup our bounding box
   if( mDataBlock->mShape )
   {
      mObjBox = mDataBlock->mShape->mBounds;
   }
   else
   {
      mObjBox = Box3F(Point3F(-1, -1, -1), Point3F(1, 1, 1));
   }

   if( mDataBlock->mShape)
   {
      mShape = new TSShapeInstance( mDataBlock->mShape, true);
   }

   if( mPart )
   {
      // use half radius becuase we want debris to stick in ground
      mRadius = mPart->getRadius() * 0.5;
      mObjBox = mPart->getBounds();
   }

   resetWorldBox();

   mInitialTrans = getTransform();

   if( mDataBlock->velocity != 0.0 )
   {
      F32 velocity = mDataBlock->velocity + gRandGen.randF( -mDataBlock->velocityVariance, mDataBlock->velocityVariance );

      mVelocity.normalizeSafe();
      mVelocity *= velocity;
   }

   // mass calculations
   if( mDataBlock->useRadiusMass )
   {
      if( mRadius < mDataBlock->baseRadius )
      {
         mRadius = mDataBlock->baseRadius;
      }

      // linear falloff
      F32 multFactor = mDataBlock->baseRadius / mRadius;

      mElasticity *= multFactor;
      mFriction *= multFactor;
      mRotAngles *= multFactor;
   }


   // tell engine the debris exists
   gClientSceneGraph->addObjectToScene(this);

   removeFromProcessList();
   ClientProcessList::get()->addObject(this);

   NetConnection* pNC = NetConnection::getConnectionToServer();
   AssertFatal(pNC != NULL, "Error, must have a connection to the server!");
   pNC->addObject(this);

   return true;
}

void Debris::onRemove()
{
   for( S32 i=0; i<DebrisData::DDC_NUM_EMITTERS; i++ )
   {
      if( mEmitterList[i] )
      {
         mEmitterList[i]->deleteWhenEmpty();
         mEmitterList[i] = NULL;
      }
   }

   if( mPart )
   {
      TSShapeInstance *ss = mPart->getSourceShapeInstance();
      if( ss )
      {
         ss->decDebrisRefCount();
         if( ss->getDebrisRefCount() == 0 )
         {
            delete ss;
         }
      }
   }

   removeFromScene();

   Parent::onRemove();
}

void Debris::processTick(const Move*)
{
   if (mLifetime <= 0.0)
      deleteObject();
}

void Debris::advanceTime( F32 dt )
{
   mElapsedTime += dt;

   mLifetime -= dt;
   if( mLifetime <= 0.0 )
   {
      mLifetime = 0.0;
      return;
   }

   mLastPos = getPosition();

   if( !mStatic )
   {
      rotate( dt );

      Point3F nextPos = getPosition();
      computeNewState( nextPos, mVelocity, dt );

      if( bounce( nextPos, dt ) )
      {
         --mNumBounces;
         if( mNumBounces <= 0 )
         {
            if( mDataBlock->explodeOnMaxBounce )
            {
               explode();
               mLifetime = 0.0;
            }
            if( mDataBlock->snapOnMaxBounce )
            {
               // orient debris so it's flat
               MatrixF stat = getTransform();

               Point3F dir;
               stat.getColumn( 1, &dir );
               dir.z = 0.0;

               MatrixF newTrans = MathUtils::createOrientFromDir( dir );

               // hack for shell casings to get them above ground.  Need something better - bramage
               newTrans.setPosition( getPosition() + Point3F( 0.0f, 0.0f, 0.10f ) );

               setTransform( newTrans );
            }
            if( mDataBlock->staticOnMaxBounce )
            {
               mStatic = true;
            }
         }
      }
      else
      {
         setPosition( nextPos );
      }
   }

   Point3F pos( getPosition( ) );
   updateEmitters( pos, mVelocity, (U32)(dt * 1000.0));

}

void Debris::rotate( F32 dt )
{
   MatrixF curTrans = getTransform();
   curTrans.setPosition( Point3F(0.0f, 0.0f, 0.0f) );

   Point3F curAngles = mRotAngles * dt * M_PI_F/180.0f;
   MatrixF rotMatrix( EulerF( curAngles.x, curAngles.y, curAngles.z ) );

   curTrans.mul( rotMatrix );
   curTrans.setPosition( getPosition() );
   setTransform( curTrans );
}

bool Debris::bounce( const Point3F &nextPos, F32 dt )
{
   Point3F curPos = getPosition();

   Point3F dir = nextPos - curPos;
   if( dir.magnitudeSafe() == 0.0f ) return false;
   dir.normalizeSafe();
   Point3F extent = nextPos + dir * mRadius;
   F32 totalDist = Point3F( extent - curPos ).magnitudeSafe();
   F32 moveDist = Point3F( nextPos - curPos ).magnitudeSafe();
   F32 movePercent = (moveDist / totalDist);

   RayInfo rayInfo;
   U32 collisionMask = csmStaticCollisionMask;
   if( !mDataBlock->ignoreWater )
   {
      collisionMask |= WaterObjectType;
   }

   if( getContainer()->castRay( curPos, extent, collisionMask, &rayInfo ) )
   {

      Point3F reflection = mVelocity - rayInfo.normal * (mDot( mVelocity, rayInfo.normal ) * 2.0f);
      mVelocity = reflection;

      Point3F tangent = reflection - rayInfo.normal * mDot( reflection, rayInfo.normal );
      mVelocity -= tangent * mFriction;

      Point3F velDir = mVelocity;
      velDir.normalizeSafe();

      mVelocity *= mElasticity;

      Point3F bouncePos = curPos + dir * rayInfo.t * movePercent;
      bouncePos += mVelocity * dt;

      setPosition( bouncePos );

      mRotAngles *= mElasticity;

      return true;

   }

   return false;

}

void Debris::explode()
{

   if( !mDataBlock->explosion ) return;

   Point3F explosionPos = getPosition();

   Explosion* pExplosion = new Explosion;
   pExplosion->setSubstitutionData(ss_object, ss_index);
   pExplosion->onNewDataBlock(mDataBlock->explosion->cloneAndPerformSubstitutions(ss_object, ss_index), false);

   MatrixF trans( true );
   trans.setPosition( getPosition() );

   pExplosion->setTransform( trans );
   pExplosion->setInitialState( explosionPos, VectorF(0,0,1), 1);
   if (!pExplosion->registerObject())
      delete pExplosion;
}

void Debris::computeNewState( Point3F &newPos, Point3F &newVel, F32 dt )
{
   // apply gravity
   Point3F force = Point3F(0, 0, -9.81 * mDataBlock->gravModifier );

   if( mDataBlock->terminalVelocity > 0.0001 )
   {
      if( newVel.magnitudeSafe() > mDataBlock->terminalVelocity )
      {
         newVel.normalizeSafe();
         newVel *= mDataBlock->terminalVelocity;
      }
      else
      {
         newVel += force * dt;
      }
   }
   else
   {
      newVel += force * dt;
   }

   newPos += newVel * dt;

}

void Debris::updateEmitters( Point3F &pos, Point3F &vel, U32 ms )
{

   Point3F axis = -vel;

   if( axis.magnitudeSafe() == 0.0 )
   {
      axis = Point3F( 0.0, 0.0, 1.0 );
   }
   axis.normalizeSafe();


   Point3F lastPos = mLastPos;

   for( S32 i=0; i<DebrisData::DDC_NUM_EMITTERS; i++ )
   {
      if( mEmitterList[i] )
      {
         mEmitterList[i]->emitParticles( lastPos, pos, axis, vel, ms );
      }
   }

}

void Debris::prepRenderImage( SceneRenderState *state )
{
   if( !mPart && !mShape )
      return;

   Point3F cameraOffset;
   mObjToWorld.getColumn(3,&cameraOffset);
   cameraOffset -= state->getDiffuseCameraPosition();
   F32 dist = cameraOffset.len();
   F32 invScale = (1.0f/getMax(getMax(mObjScale.x,mObjScale.y),mObjScale.z));

   if( mShape )
   {
      mShape->setDetailFromDistance( state, dist * invScale );
      if( mShape->getCurrentDetail() < 0 )
         return;
   }

   if( mPart )
   {
      // get the shapeInstance that we are using for the debris parts
      TSShapeInstance *si = mPart->getSourceShapeInstance();
      if ( si )
         si->setDetailFromDistance( state, dist * invScale );
   }

   prepBatchRender( state );
}

void Debris::prepBatchRender( SceneRenderState *state )
{
   if ( !mShape && !mPart )
      return;

   GFXTransformSaver saver;

   F32 alpha = 1.0;
   if( mDataBlock->fade )
   {
      if( mLifetime < 1.0 ) alpha = mLifetime;
   }

   Point3F cameraOffset;
   mObjToWorld.getColumn(3,&cameraOffset);
   cameraOffset -= state->getCameraPosition();
      
   // Set up our TS render state.
   TSRenderState rdata;
   rdata.setSceneState( state );

   // We might have some forward lit materials
   // so pass down a query to gather lights.
   LightQuery query;
   query.init( getWorldSphere() );
   rdata.setLightQuery( &query );

   if( mShape )
   {
      MatrixF mat = getRenderTransform();
      GFX->setWorldMatrix( mat );

      rdata.setFadeOverride( alpha );
      mShape->render( rdata );
   }
   else
   {
      if (mPart->getCurrentObjectDetail() != -1)
      {
         MatrixF mat = getRenderTransform();
         GFX->setWorldMatrix( mat );
           
         rdata.setFadeOverride( alpha );
         mPart->render( rdata );
      }
   }
}

void Debris::setSize( F32 size )
{
   mSize = size;
}
