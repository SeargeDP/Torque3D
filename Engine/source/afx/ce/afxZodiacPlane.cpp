
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// Arcane-FX for MIT Licensed Open Source version of Torque 3D from GarageGames
// Copyright (C) 2015 Faust Logic, Inc.
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
//
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//

#include "afx/arcaneFX.h"

#include "math/mathIO.h"
#include "renderInstance/renderPassManager.h"

#include "afx/afxChoreographer.h"
#include "afx/ce/afxZodiac.h"
#include "afx/ce/afxZodiacPlane.h"

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// afxZodiacPlaneData

IMPLEMENT_CO_DATABLOCK_V1(afxZodiacPlaneData);

ConsoleDocClass( afxZodiacPlaneData,
   "@brief A datablock that specifies a Zodiac Plane effect.\n\n"

   "afxZodiacData describes a zodiac-like effect called a zodiac plane. It reproduces most of the behavior of normal zodiacs "
   "but unlike zodiac decals, it is represented as a flat plane of geometry that can be more flexibly positioned and oriented."
   "\n\n"

   "@ingroup afxEffects\n"
   "@ingroup AFX\n"
   "@ingroup Datablocks\n"
);

afxZodiacPlaneData::afxZodiacPlaneData()
{
   INIT_ASSET(Texture);

  radius_xy = 1;
  start_ang = 0;
  ang_per_sec = 0;
  grow_in_time = 0.0f; 
  shrink_out_time = 0.0f;
  growth_rate = 0.0f;
  color.set(1,1,1,1);
  blend_flags = BLEND_NORMAL;
  respect_ori_cons = false;
  zflags = 0;
  double_sided = true;
  face_dir = FACES_UP;
  use_full_xfm = false;
}

afxZodiacPlaneData::afxZodiacPlaneData(const afxZodiacPlaneData& other, bool temp_clone)
  : GameBaseData(other, temp_clone)
{
   CLONE_ASSET(Texture);

  radius_xy = other.radius_xy;
  start_ang = other.start_ang;
  ang_per_sec = other.ang_per_sec;
  grow_in_time = other.grow_in_time;
  shrink_out_time = other.shrink_out_time;
  growth_rate = other.growth_rate;
  color = other.color;

  double_sided = other.double_sided;
  face_dir = other.face_dir;
  use_full_xfm = other.use_full_xfm;

  zflags = other.zflags;
  expand_zflags();
}

ImplementEnumType( afxZodiacPlane_BlendType, "Possible zodiac blend types.\n" "@ingroup afxZodiacPlane\n\n" )
   { afxZodiacData::BLEND_NORMAL,      "normal",         "..." },
   { afxZodiacData::BLEND_ADDITIVE,    "additive",       "..." },
   { afxZodiacData::BLEND_SUBTRACTIVE, "subtractive",    "..." },
EndImplementEnumType;

ImplementEnumType( afxZodiacPlane_FacingType, "Possible zodiac plane facing types.\n" "@ingroup afxZodiacPlane\n\n" )
  { afxZodiacPlaneData::FACES_UP,        "up",        "..." },
  { afxZodiacPlaneData::FACES_DOWN,      "down",      "..." },
  { afxZodiacPlaneData::FACES_FORWARD,   "forward",   "..." },
  { afxZodiacPlaneData::FACES_BACK,      "backward",  "..." },
  { afxZodiacPlaneData::FACES_RIGHT,     "right",     "..." },
  { afxZodiacPlaneData::FACES_LEFT,      "left",      "..." },

  { afxZodiacPlaneData::FACES_FORWARD,   "front",     "..." },
  { afxZodiacPlaneData::FACES_BACK,      "back",      "..." },
EndImplementEnumType;

#define myOffset(field) Offset(field, afxZodiacPlaneData)

void afxZodiacPlaneData::initPersistFields()
{
   docsURL;
   INITPERSISTFIELD_IMAGEASSET(Texture, afxZodiacPlaneData, "An image to use as the zodiac's texture.");

  addFieldV("radius", TypeRangedF32,        myOffset(radius_xy), &CommonValidators::PositiveFloat,
    "The zodiac's radius in scene units.");
  addFieldV("startAngle", TypeRangedF32,        myOffset(start_ang), &CommonValidators::DegreeRange,
    "The starting angle in degrees of the zodiac's rotation.");
  addFieldV("rotationRate", TypeRangedF32,        myOffset(ang_per_sec), &CommonValidators::DegreeRange,
    "The rate of rotation in degrees-per-second. Zodiacs with a positive rotationRate "
    "rotate clockwise, while those with negative values turn counter-clockwise.");
  addFieldV("growInTime", TypeRangedF32,        myOffset(grow_in_time), &CommonValidators::PositiveFloat,
    "A duration of time in seconds over which the zodiac grows from a zero size to its "
    "full size as specified by the radius.");
  addFieldV("shrinkOutTime", TypeRangedF32,        myOffset(shrink_out_time), &CommonValidators::PositiveFloat,
    "A duration of time in seconds over which the zodiac shrinks from full size to "
    "invisible.");
  addFieldV("growthRate", TypeRangedF32,        myOffset(growth_rate), &CommonValidators::F32Range,
    "A rate in meters-per-second at which the zodiac grows in size. A negative value will "
    "shrink the zodiac.");
  addField("color",           TypeColorF,     myOffset(color),
    "A color value for the zodiac.");

  addField("blend", TYPEID<BlendType>(), myOffset(blend_flags),
    "A blending style for the zodiac. Possible values: normal, additive, or subtractive.");

  addField("trackOrientConstraint", TypeBool,  myOffset(respect_ori_cons),
    "Specifies if the zodiac's rotation should be defined by its constrained "
    "transformation.");

  addField("doubleSided",     TypeBool,       myOffset(double_sided),
    "Controls whether the zodiac-plane's polygons are rendered when viewed from either "
    "side. If set to false, the zodiac-plane will only be seen when viewed from the "
    "direction it is facing (according to faceDir).");

  addField("faceDir", TYPEID<afxZodiacPlaneData::FacingType>(), myOffset(face_dir),
    "Specifies which direction the zodiac-plane's polygons face. Possible values: "
    "up, down, front, back, right, or left.");

  addField("useFullTransform",      TypeBool,  myOffset(use_full_xfm),
    "Normal zodiacs have only one degree of freedom, a rotation around the z-axis. "
    "Depending on the setting for trackOrientConstraint, this means that the effect's "
    "orientation is either ignored or is limited to influencing the zodiac's angle of "
    "rotation. By default, zodiac-plane reproduces this limited behavior in order to "
    "match normal zodiacs. When useFullTransform is set to true, the zodiac can be "
    "arbitrarily oriented.");

  Parent::initPersistFields();
}

void afxZodiacPlaneData::packData(BitStream* stream)
{
	Parent::packData(stream);

  merge_zflags();

  PACKDATA_ASSET(Texture);

  stream->write(radius_xy);
  stream->write(start_ang);
  stream->write(ang_per_sec);
  stream->write(grow_in_time);
  stream->write(shrink_out_time);
  stream->write(growth_rate);
  stream->write(color);
  stream->write(zflags);
  stream->write(double_sided);
  stream->writeFlag(use_full_xfm);
  stream->writeInt(face_dir, FACES_BITS);
}

void afxZodiacPlaneData::unpackData(BitStream* stream)
{
  Parent::unpackData(stream);

  UNPACKDATA_ASSET(Texture);

  stream->read(&radius_xy);
  stream->read(&start_ang);
  stream->read(&ang_per_sec);
  stream->read(&grow_in_time);
  stream->read(&shrink_out_time);
  stream->read(&growth_rate);
  stream->read(&color);
  stream->read(&zflags);
  stream->read(&double_sided);
  use_full_xfm = stream->readFlag();
  face_dir = stream->readInt(FACES_BITS);

  expand_zflags();
}

bool afxZodiacPlaneData::preload(bool server, String &errorStr)
{
  if (!Parent::preload(server, errorStr))
    return false;

  return true;
}

F32 afxZodiacPlaneData::calcRotationAngle(F32 elapsed, F32 rate_factor)
{
  F32 angle = start_ang + elapsed*ang_per_sec*rate_factor;
  angle = mFmod(angle, 360.0f);

  return angle;
}

void afxZodiacPlaneData::expand_zflags()
{
  blend_flags = (zflags & BLEND_MASK);
  respect_ori_cons = ((zflags & RESPECT_ORIENTATION) != 0);
}

void afxZodiacPlaneData::merge_zflags()
{
  zflags = (blend_flags & BLEND_MASK);
  if (respect_ori_cons)
    zflags |= RESPECT_ORIENTATION;
}

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
// afxZodiacPlane

IMPLEMENT_CO_NETOBJECT_V1(afxZodiacPlane);

ConsoleDocClass( afxZodiacPlane,
   "@brief A ZodiacPlane effect as defined by an afxZodiacPlaneData datablock.\n\n"

   "@ingroup afxEffects\n"
   "@ingroup AFX\n"
);

afxZodiacPlane::afxZodiacPlane()
{
  mNetFlags.clear();
  mNetFlags.set(IsGhost);

  mDataBlock = 0;
  color.set(1,1,1,1);
  radius = 1;
  is_visible = true;
}

afxZodiacPlane::~afxZodiacPlane()
{
}

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//

bool afxZodiacPlane::onNewDataBlock(GameBaseData* dptr, bool reload)
{
  mDataBlock = dynamic_cast<afxZodiacPlaneData*>(dptr);
  if (!mDataBlock || !Parent::onNewDataBlock(dptr, reload))
    return false;

  return true;
}

bool afxZodiacPlane::onAdd()
{
  if(!Parent::onAdd())
    return false;
  
  F32 len = mDataBlock->radius_xy;

  switch (mDataBlock->face_dir)
  {
  case afxZodiacPlaneData::FACES_UP:
  case afxZodiacPlaneData::FACES_DOWN:
    mObjBox = Box3F(Point3F(-len, -len, -0.01f), Point3F(len, len, 0.01f));
    break;  
  case afxZodiacPlaneData::FACES_FORWARD:
  case afxZodiacPlaneData::FACES_BACK:
    mObjBox = Box3F(Point3F(-len, -0.01f, -len), Point3F(len, 0.01f, len));
    break;  
  case afxZodiacPlaneData::FACES_RIGHT:
  case afxZodiacPlaneData::FACES_LEFT:
    mObjBox = Box3F(Point3F(-0.01f, -len, -len), Point3F(0.01f, len, len));
    break;  
  }

  addToScene();
  
  return true;
}

void afxZodiacPlane::onRemove()
{
  removeFromScene();
  
  Parent::onRemove();
}

//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~//~~~~~~~~~~~~~~~~~~~~~//
