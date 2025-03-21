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
#include "materials/materialDefinition.h"

#include "console/consoleTypes.h"
#include "console/engineAPI.h"
#include "math/mathTypes.h"
#include "materials/materialManager.h"
#include "sceneData.h"
#include "gfx/sim/cubemapData.h"
#include "gfx/gfxCubemap.h"
#include "math/mathIO.h"
#include "materials/matInstance.h"
#include "sfx/sfxTrack.h"
#include "sfx/sfxTypes.h"
#include "core/util/safeDelete.h"
#include "T3D/accumulationVolume.h"
#include "gui/controls/guiTreeViewCtrl.h"
#include <console/persistenceManager.h>
#include "console/typeValidators.h"

IMPLEMENT_CONOBJECT(Material);

ConsoleDocClass(Material,
   "@brief A material in Torque 3D is a data structure that describes a surface.\n\n"

   "It contains many different types of information for rendering properties. "
   "Torque 3D generates shaders from Material definitions. The shaders are compiled "
   "at runtime and output into the example/shaders directory. Any errors or warnings "
   "generated from compiling the procedurally generated shaders are output to the console "
   "as well as the output window in the Visual C IDE.\n\n"

   "@tsexample\n"
   "singleton Material(DECAL_scorch)\n"
   "{\n"
   "	baseTex[0] = \"./scorch_decal.png\";\n"
   "	vertColor[ 0 ] = true;\n\n"
   "	translucent = true;\n"
   "	translucentBlendOp = None;\n"
   "	translucentZWrite = true;\n"
   "	alphaTest = true;\n"
   "	alphaRef = 84;\n"
   "};\n"
   "@endtsexample\n\n"

   "@see Rendering\n"
   "@see ShaderData\n"

   "@ingroup GFX\n");

ImplementBitfieldType(MaterialAnimType,
   "The type of animation effect to apply to this material.\n"
   "@ingroup GFX\n\n")
{ Material::Scroll, "$Scroll", "Scroll the material along the X/Y axis.\n"},
{ Material::Rotate, "$Rotate" , "Rotate the material around a point.\n" },
{ Material::Wave, "$Wave" , "Warps the material with an animation using Sin, Triangle or Square mathematics.\n" },
{ Material::Scale, "$Scale", "Scales the material larger and smaller with a pulsing effect.\n" },
{ Material::Sequence, "$Sequence", "Enables the material to have multiple frames of animation in its imagemap.\n" }
EndImplementBitfieldType;

ImplementEnumType(MaterialBlendOp,
   "The type of graphical blending operation to apply to this material\n"
   "@ingroup GFX\n\n")
{ Material::None, "None", "Disable blending for this material."},
{ Material::Mul,          "Mul", "Multiplicative blending." },
{ Material::PreMul,       "PreMul", "Premultiplied alpha." },
{ Material::Add,          "Add", "Adds the color of the material to the frame buffer with full alpha for each pixel." },
{ Material::AddAlpha,     "AddAlpha", "The color is modulated by the alpha channel before being added to the frame buffer." },
{ Material::Sub,          "Sub", "Subtractive Blending. Reverses the color model, causing dark colors to have a stronger visual effect." },
{ Material::LerpAlpha,    "LerpAlpha", "Linearly interpolates between Material color and frame buffer color based on alpha." }
EndImplementEnumType;

ImplementEnumType(MaterialWaveType,
   "When using the Wave material animation, one of these Wave Types will be used to determine the type of wave to display.\n"
   "@ingroup GFX\n")
{
   Material::Sin, "Sin", "Warps the material along a curved Sin Wave."
},
{ Material::Triangle,     "Triangle", "Warps the material along a sharp Triangle Wave." },
{ Material::Square,       "Square", "Warps the material along a wave which transitions between two oppposite states. As a Square Wave, the transition is quick and sudden." },
   EndImplementEnumType;

bool Material::sAllowTextureTargetAssignment = false;

GFXCubemap* Material::GetNormalizeCube()
{
   if (smNormalizeCube)
      return smNormalizeCube;
   smNormalizeCube = GFX->createCubemap();
   smNormalizeCube->initNormalize(64);
   return smNormalizeCube;
}

GFXCubemapHandle Material::smNormalizeCube;


Material::Material()
{
   for (U32 i = 0; i < MAX_STAGES; i++)
   {
      mDiffuse[i].set(1.0f, 1.0f, 1.0f, 1.0f);
      mDiffuseMapSRGB[i] = true;

      mRoughness[i] = 1.0f;
      mMetalness[i] = 0.0f;

      mIsSRGb[i] = false;

      mAOChan[i] = 0;
      mInvertRoughness[i] = false;
      mRoughnessChan[i] = 1;
      mMetalChan[i] = 2;

      mAccuEnabled[i] = false;
      mAccuScale[i] = 1.0f;
      mAccuDirection[i] = 1.0f;
      mAccuStrength[i] = 0.6f;
      mAccuCoverage[i] = 0.9f;
      mAccuSpecular[i] = 16.0f;

      INIT_IMAGEASSET_ARRAY(DiffuseMap, GFXStaticTextureSRGBProfile, i);
      INIT_IMAGEASSET_ARRAY(OverlayMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(LightMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(ToneMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(DetailMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(NormalMap, GFXNormalMapProfile, i);
      INIT_IMAGEASSET_ARRAY(ORMConfigMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(RoughMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(AOMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(MetalMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(GlowMap, GFXStaticTextureProfile, i);
      INIT_IMAGEASSET_ARRAY(DetailNormalMap, GFXNormalMapProfile, i);

      mParallaxScale[i] = 0.0f;

      mVertLit[i] = false;
      mVertColor[i] = false;

      mGlow[i] = false;
      mReceiveShadows[i] = true;
      mIgnoreLighting[i] = false;

      mDetailScale[i].set(2.0f, 2.0f);

      mDetailNormalMapStrength[i] = 1.0f;

      mMinnaertConstant[i] = -1.0f;
      mSubSurface[i] = false;
      mSubSurfaceColor[i].set(1.0f, 0.2f, 0.2f, 1.0f);
      mSubSurfaceRolloff[i] = 0.2f;

      mAnimFlags[i] = 0;

      mScrollDir[i].set(0.0f, 0.0f);
      mScrollSpeed[i] = 0.0f;
      mScrollOffset[i].set(0.0f, 0.0f);

      mRotSpeed[i] = 0.0f;
      mRotPivotOffset[i].set(0.0f, 0.0f);
      mRotPos[i] = 0.0f;

      mWavePos[i] = 0.0f;
      mWaveFreq[i] = 0.0f;
      mWaveAmp[i] = 0.0f;
      mWaveType[i] = 0;

      mSeqFramePerSec[i] = 0.0f;
      mSeqSegSize[i] = 0.0f;

      // Deferred Shading
      mMatInfoFlags[i] = 0.0f;

      mGlowMul[i] = 0.0f;
   }

   dMemset(mCellIndex, 0, sizeof(mCellIndex));
   dMemset(mCellLayout, 0, sizeof(mCellLayout));
   dMemset(mCellSize, 0, sizeof(mCellSize));
   dMemset(mNormalMapAtlas, 0, sizeof(mNormalMapAtlas));
   dMemset(mUseAnisotropic, 1, sizeof(mUseAnisotropic));

   mImposterLimits = Point4F::Zero;

   mDoubleSided = false;

   mTranslucent = false;
   mTranslucentBlendOp = PreMul;
   mTranslucentZWrite = false;

   mAlphaTest = false;
   mAlphaRef = 1;

   mCastShadows = true;

   mPlanarReflection = false;

   mCubemapData = NULL;
   mDynamicCubemap = false;

   mLastUpdateTime = 0;

   mAutoGenerated = false;

   mShowDust = false;
   mShowFootprints = true;

   dMemset(mEffectColor, 0, sizeof(mEffectColor));

   mEffectColor[0] = LinearColorF::WHITE;
   mEffectColor[1] = LinearColorF::WHITE;

   mFootstepSoundId = -1;     mImpactSoundId = -1;
   mImpactFXIndex = -1;
   INIT_ASSET(CustomFootstepSound);
   INIT_ASSET(CustomImpactSound);
   mFriction = 0.0;

   mDirectSoundOcclusion = 1.f;
   mReverbSoundOcclusion = 1.0;
}

IRangeValidator bmpChanRange(0, 3);
FRangeValidator glowMulRange(0.0f, 20.0f);
FRangeValidator parallaxScaleRange(0.0f, 4.0f);
FRangeValidator scrollSpeedRange(0.0f, 10.0f);
FRangeValidator waveFreqRange(0.0f, 10.0f);
void Material::onImageAssetChanged()
{
   flush();
   reload();
}

void Material::initPersistFields()
{
   docsURL;
   addField("mapTo", TypeRealString, Offset(mMapTo, Material),
      "Used to map this material to the material name used by TSShape.");

   addArray("Stages", MAX_STAGES);

   addGroup("Basic Texture Maps");
      INITPERSISTFIELD_IMAGEASSET_ARRAY(DiffuseMap, MAX_STAGES, Material, "Albedo");
      addField("diffuseColor", TypeColorF, Offset(mDiffuse, Material), MAX_STAGES,
         "This color is multiplied against the diffuse texture color.  If no diffuse texture "
         "is present this is the material color.");
      addField("diffuseMapSRGB", TypeBool, Offset(mDiffuseMapSRGB, Material), MAX_STAGES,
         "Enable sRGB for the diffuse color texture map.");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(NormalMap, MAX_STAGES, Material, "NormalMap");
   endGroup("Basic Texture Maps");

   addGroup("Light Influence Maps");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(ORMConfigMap, MAX_STAGES, Material, "AO|Roughness|metalness map");
      addField("isSRGb", TypeBool, Offset(mIsSRGb, Material), MAX_STAGES,
         "Substance Designer Workaround.");
      addField("invertRoughness", TypeBool, Offset(mInvertRoughness, Material), MAX_STAGES,
         "Treat Roughness as Roughness");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(AOMap, MAX_STAGES, Material, "AOMap");
      addFieldV("AOChan", TypeRangedS32, Offset(mAOChan, Material), &bmpChanRange, MAX_STAGES,
         "The input channel AO maps use.");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(RoughMap, MAX_STAGES, Material, "RoughMap (also needs MetalMap)");
      addFieldV("roughness", TypeRangedF32, Offset(mRoughness, Material),  &CommonValidators::F32_8BitPercent,MAX_STAGES,
         "The degree of roughness when not using a ORMConfigMap.");
      addFieldV("roughnessChan", TypeRangedS32, Offset(mRoughnessChan, Material), &bmpChanRange, MAX_STAGES,
         "The input channel roughness maps use.");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(MetalMap, MAX_STAGES, Material, "MetalMap (also needs RoughMap)");
      addFieldV("metalness", TypeRangedF32, Offset(mMetalness, Material), &CommonValidators::F32_8BitPercent, MAX_STAGES,
         "The degree of Metalness when not using a ORMConfigMap.");
      addFieldV("metalChan", TypeRangedS32, Offset(mMetalChan, Material), &bmpChanRange, MAX_STAGES,
         "The input channel metalness maps use.");
      INITPERSISTFIELD_IMAGEASSET_ARRAY(GlowMap, MAX_STAGES, Material, "GlowMap (needs Albedo)");

      addFieldV("glowMul", TypeRangedF32, Offset(mGlowMul, Material),&glowMulRange, MAX_STAGES,
         "glow mask multiplier");
   endGroup("Light Influence Maps");

   addGroup("Advanced Texture Maps");
      INITPERSISTFIELD_IMAGEASSET_ARRAY(DetailMap, MAX_STAGES, Material, "DetailMap");
      addField("detailScale", TypePoint2F, Offset(mDetailScale, Material), MAX_STAGES,
         "The scale factor for the detail map.");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(DetailNormalMap, MAX_STAGES, Material, "DetailNormalMap");
      addFieldV("detailNormalMapStrength", TypeRangedF32, Offset(mDetailNormalMapStrength, Material), &CommonValidators::PositiveFloat, MAX_STAGES,
         "Used to scale the strength of the detail normal map when blended with the base normal map.");

      INITPERSISTFIELD_IMAGEASSET_ARRAY(OverlayMap, MAX_STAGES, Material, "Overlay");
      INITPERSISTFIELD_IMAGEASSET_ARRAY(LightMap, MAX_STAGES, Material, "LightMap");
      INITPERSISTFIELD_IMAGEASSET_ARRAY(ToneMap, MAX_STAGES, Material, "ToneMap");
   endGroup("Advanced Texture Maps");
   
   addGroup("Accumulation Properties");
      addProtectedField("accuEnabled", TYPEID< bool >(), Offset(mAccuEnabled, Material),
         &_setAccuEnabled, &defaultProtectedGetFn, MAX_STAGES, "Accumulation texture.");

      addFieldV("accuScale", TypeRangedF32, Offset(mAccuScale, Material), &CommonValidators::PositiveFloat, MAX_STAGES,
         "The scale that is applied to the accu map texture. You can use this to fit the texture to smaller or larger objects.");

      addFieldV("accuDirection", TypeRangedF32, Offset(mAccuDirection, Material), &CommonValidators::DirFloat, MAX_STAGES,
         "The direction of the accumulation. Chose whether you want the accu map to go from top to bottom (ie. snow) or upwards (ie. mold).");

      addFieldV("accuStrength", TypeRangedF32, Offset(mAccuStrength, Material), &CommonValidators::NormalizedFloat, MAX_STAGES,
         "The strength of the accu map. This changes the transparency of the accu map texture. Make it subtle or add more contrast.");

      addFieldV("accuCoverage", TypeRangedF32, Offset(mAccuCoverage, Material), &CommonValidators::NormalizedFloat, MAX_STAGES,
         "The coverage ratio of the accu map texture. Use this to make the entire shape pick up some of the accu map texture or none at all.");

      addFieldV("accuSpecular", TypeRangedF32, Offset(mAccuSpecular, Material), &CommonValidators::NormalizedFloat, MAX_STAGES,
         "Changes specularity to this value where the accumulated material is present.");
   endGroup("Accumulation Properties");
   
   addGroup("Lighting Properties");
      addField("receiveShadows", TypeBool, Offset(mReceiveShadows, Material), MAX_STAGES,
         "Shadows being cast onto the material.");
      addField("ignoreLighting", TypeBool, Offset(mIgnoreLighting, Material), MAX_STAGES,
         "Enables emissive lighting for the material.");
      addField("glow", TypeBool, Offset(mGlow, Material), MAX_STAGES,
         "Enables rendering as glowing.");
      addFieldV("parallaxScale", TypeRangedF32, Offset(mParallaxScale, Material),&parallaxScaleRange, MAX_STAGES,
         "Enables parallax mapping and defines the scale factor for the parallax effect.  Typically "
         "this value is less than 0.4 else the effect breaks down.");

      addField("useAnisotropic", TypeBool, Offset(mUseAnisotropic, Material), MAX_STAGES,
         "Use anisotropic filtering for the textures of this stage.");

      addField("vertLit", TypeBool, Offset(mVertLit, Material), MAX_STAGES,
         "If true the vertex color is used for lighting.");
      addField("vertColor", TypeBool, Offset(mVertColor, Material), MAX_STAGES,
         "If enabled, vertex colors are premultiplied with diffuse colors.");
   /* presently unsupported directly. advice would be to use a glowmap+glowmul to fine tune backscatter effects
      addField("subSurface", TypeBool, Offset(mSubSurface, Material), MAX_STAGES,
         "Enables the subsurface scattering approximation.");
      addField("minnaertConstant", TypeF32, Offset(mMinnaertConstant, Material), MAX_STAGES,
         "The Minnaert shading constant value.  Must be greater than 0 to enable the effect.");
      addField("subSurfaceColor", TypeColorF, Offset(mSubSurfaceColor, Material), MAX_STAGES,
         "The color used for the subsurface scattering approximation.");
      addField("subSurfaceRolloff", TypeF32, Offset(mSubSurfaceRolloff, Material), MAX_STAGES,
         "The 0 to 1 rolloff factor used in the subsurface scattering approximation.");
   */
   endGroup("Lighting Properties");

   addGroup("Animation Properties");
      addField("animFlags", TypeMaterialAnimType, Offset(mAnimFlags, Material), MAX_STAGES,
         "The types of animation to play on this material.");

      addField("scrollDir", TypePoint2F, Offset(mScrollDir, Material), MAX_STAGES,
         "The scroll direction in UV space when scroll animation is enabled.");

      addFieldV("scrollSpeed", TypeRangedF32, Offset(mScrollSpeed, Material), &scrollSpeedRange, MAX_STAGES,
         "The speed to scroll the texture in UVs per second when scroll animation is enabled.");

      addFieldV("rotSpeed", TypeRangedF32, Offset(mRotSpeed, Material), &CommonValidators::DegreeRange, MAX_STAGES,
         "The speed to rotate the texture in degrees per second when rotation animation is enabled.");

      addField("rotPivotOffset", TypePoint2F, Offset(mRotPivotOffset, Material), MAX_STAGES,
         "The piviot position in UV coordinates to center the rotation animation.");

      addField("waveType", TYPEID< WaveType >(), Offset(mWaveType, Material), MAX_STAGES,
         "The type of wave animation to perform when wave animation is enabled.");

      addFieldV("waveFreq", TypeRangedF32, Offset(mWaveFreq, Material),&waveFreqRange, MAX_STAGES,
         "The wave frequency when wave animation is enabled.");

      addFieldV("waveAmp", TypeRangedF32, Offset(mWaveAmp, Material), &CommonValidators::NormalizedFloat, MAX_STAGES,
         "The wave amplitude when wave animation is enabled.");

      addField("sequenceFramePerSec", TypeF32, Offset(mSeqFramePerSec, Material), MAX_STAGES,
         "The number of frames per second for frame based sequence animations if greater than zero.");

      addField("sequenceSegmentSize", TypeF32, Offset(mSeqSegSize, Material), MAX_STAGES,
         "The size of each frame in UV units for sequence animations.");

      // Texture atlasing
      addField("cellIndex", TypePoint2I, Offset(mCellIndex, Material), MAX_STAGES,
         "@internal");
      addField("cellLayout", TypePoint2I, Offset(mCellLayout, Material), MAX_STAGES,
         "@internal");
      addFieldV("cellSize", TypeRangedS32, Offset(mCellSize, Material), &CommonValidators::PositiveInt, MAX_STAGES,
         "@internal");
      addField("bumpAtlas", TypeBool, Offset(mNormalMapAtlas, Material), MAX_STAGES,
         "@internal");
   endGroup("Animation Properties");

   endArray("Stages");

   addGroup("Advanced Properties (All Layers)");
      addField("doubleSided", TypeBool, Offset(mDoubleSided, Material),
         "Disables backface culling casing surfaces to be double sided. "
         "Note that the lighting on the backside will be a mirror of the front "
         "side of the surface.");
      addField("castShadows", TypeBool, Offset(mCastShadows, Material),
         "If set to false the lighting system will not cast shadows from this material.");

      addField("planarReflection", TypeBool, Offset(mPlanarReflection, Material), "@internal");

      addField("translucent", TypeBool, Offset(mTranslucent, Material),
         "If true this material is translucent blended.");

      addField("translucentBlendOp", TYPEID< BlendOp >(), Offset(mTranslucentBlendOp, Material),
         "The type of blend operation to use when the material is translucent.");

      addField("translucentZWrite", TypeBool, Offset(mTranslucentZWrite, Material),
         "If enabled and the material is translucent it will write into the depth buffer.");

      addField("alphaTest", TypeBool, Offset(mAlphaTest, Material),
         "Enables alpha test when rendering the material.\n@see alphaRef\n");

      addFieldV("alphaRef", TypeRangedS32, Offset(mAlphaRef, Material), &CommonValidators::S32_8BitCap,
         "The alpha reference value for alpha testing.  Must be between 0 to 255.\n@see alphaTest\n");

      addField("cubemap", TypeRealString, Offset(mCubemapName, Material),
         "The name of a CubemapData for environment mapping.");

      addField("dynamicCubemap", TypeBool, Offset(mDynamicCubemap, Material),
         "Enables the material to use the dynamic cubemap from the ShapeBase object its applied to.");
   endGroup("Advanced Properties (All Layers)");

   addGroup("Behavioral (All Layers)");
      addField("showFootprints", TypeBool, Offset(mShowFootprints, Material),
         "Whether to show player footprint decals on this material.\n\n"
         "@see PlayerData::decalData");

      addField("showDust", TypeBool, Offset(mShowDust, Material),
         "Whether to emit dust particles from a shape moving over the material.  This is, for example, used by "
         "vehicles or players to decide whether to show dust trails.");

      addField("effectColor", TypeColorF, Offset(mEffectColor, Material), NUM_EFFECT_COLOR_STAGES,
         "If #showDust is true, this is the set of colors to use for the ParticleData of the dust "
         "emitter.\n\n"
         "@see ParticleData::colors");

      addField("footstepSoundId", TypeS32, Offset(mFootstepSoundId, Material),
         "What sound to play from the PlayerData sound list when the player walks over the material.  -1 (default) to not play any sound.\n"
         "\n"
         "The IDs are:\n\n"
         "- 0: PlayerData::FootSoftSound\n"
         "- 1: PlayerData::FootHardSound\n"
         "- 2: PlayerData::FootMetalSound\n"
         "- 3: PlayerData::FootSnowSound\n"
         "- 4: PlayerData::FootShallowSound\n"
         "- 5: PlayerData::FootWadingSound\n"
         "- 6: PlayerData::FootUnderwaterSound\n"
         "- 7: PlayerData::FootBubblesSound\n"
         "- 8: PlayerData::movingBubblesSound\n"
         "- 9: PlayerData::waterBreathSound\n"
         "- 10: PlayerData::impactSoftSound\n"
         "- 11: PlayerData::impactHardSound\n"
         "- 12: PlayerData::impactMetalSound\n"
         "- 13: PlayerData::impactSnowSound\n"
         "- 14: PlayerData::impactWaterEasy\n"
         "- 15: PlayerData::impactWaterMedium\n"
         "- 16: PlayerData::impactWaterHard\n"
         "- 17: PlayerData::exitingWater\n");

      INITPERSISTFIELD_SOUNDASSET(CustomFootstepSound, Material,
         "The sound to play when the player walks over the material.  If this is set, it overrides #footstepSoundId.  This field is "
         "useful for directly assigning custom footstep sounds to materials without having to rely on the PlayerData sound assignment.\n\n"
         "@warn Be aware that materials are client-side objects.  This means that the SFXTracks assigned to materials must be client-side, too.");
      addField("impactSoundId", TypeS32, Offset(mImpactSoundId, Material),
         "What sound to play from the PlayerData sound list when the player impacts on the surface with a velocity equal or greater "
         "than PlayerData::groundImpactMinSpeed.\n\n"
         "For a list of IDs, see #footstepSoundId");
      addField("ImpactFXIndex", TypeS32, Offset(mImpactFXIndex, Material),
         "What FX to play from the PlayerData sound list when the player impacts on the surface with a velocity equal or greater "
         "than PlayerData::groundImpactMinSpeed.\n\n"
         "For a list of IDs, see #impactFXId");
      INITPERSISTFIELD_SOUNDASSET(CustomImpactSound, Material,
         "The sound to play when the player impacts on the surface with a velocity equal or greater than PlayerData::groundImpactMinSpeed.  "
         "If this is set, it overrides #impactSoundId.  This field is useful for directly assigning custom impact sounds to materials "
         "without having to rely on the PlayerData sound assignment.\n\n"
         "@warn Be aware that materials are client-side objects.  This means that the SFXTracks assigned to materials must be client-side, too.");

      //Deactivate these for the moment as they are not used.

   #if 0
      addField("friction", TypeF32, Offset(mFriction, Material));
      addField("directSoundOcclusion", TypeF32, Offset(mDirectSoundOcclusion, Material));
      addField("reverbSoundOcclusion", TypeF32, Offset(mReverbSoundOcclusion, Material));
   #endif
   endGroup("Behavioral (All Layers)");

   // For backwards compatibility.  
  //
  // They point at the new 'map' fields, but reads always return
  // an empty string and writes only apply if the value is not empty.
  //
   addProtectedField("baseTex", TypeImageFilename, Offset(mDiffuseMapName, Material),
      defaultProtectedSetNotEmptyFn, emptyStringProtectedGetFn, MAX_STAGES,
      "For backwards compatibility.\n@see diffuseMap\n", AbstractClassRep::FIELD_HideInInspectors);
   addProtectedField("detailTex", TypeImageFilename, Offset(mDetailMapName, Material),
      defaultProtectedSetNotEmptyFn, emptyStringProtectedGetFn, MAX_STAGES,
      "For backwards compatibility.\n@see detailMap\n", AbstractClassRep::FIELD_HideInInspectors);
   addProtectedField("overlayTex", TypeImageFilename, Offset(mOverlayMapName, Material),
      defaultProtectedSetNotEmptyFn, emptyStringProtectedGetFn, MAX_STAGES,
      "For backwards compatibility.\n@see overlayMap\n", AbstractClassRep::FIELD_HideInInspectors);
   addProtectedField("bumpTex", TypeImageFilename, Offset(mNormalMapName, Material),
      defaultProtectedSetNotEmptyFn, emptyStringProtectedGetFn, MAX_STAGES,
      "For backwards compatibility.\n@see normalMap\n", AbstractClassRep::FIELD_HideInInspectors);
   addProtectedField("colorMultiply", TypeColorF, Offset(mDiffuse, Material),
      defaultProtectedSetNotEmptyFn, emptyStringProtectedGetFn, MAX_STAGES,
      "For backwards compatibility.\n@see diffuseColor\n", AbstractClassRep::FIELD_HideInInspectors);

   Parent::initPersistFields();
}

bool Material::writeField(StringTableEntry fieldname, const char* value)
{
   // Never allow the old field names to be written.
   if (fieldname == StringTable->insert("baseTex") ||
      fieldname == StringTable->insert("detailTex") ||
      fieldname == StringTable->insert("overlayTex") ||
      fieldname == StringTable->insert("bumpTex") ||
      fieldname == StringTable->insert("envTex") ||
      fieldname == StringTable->insert("colorMultiply") ||
      fieldname == StringTable->insert("internalName"))
      return false;

   return Parent::writeField(fieldname, value);
}

bool Material::onAdd()
{
   if (Parent::onAdd() == false)
      return false;

   mCubemapData = dynamic_cast<CubemapData*>(Sim::findObject(mCubemapName));

   if (mTranslucentBlendOp >= NumBlendTypes || mTranslucentBlendOp < 0)
   {
      Con::errorf("Invalid blend op in material: %s", getName());
      mTranslucentBlendOp = PreMul;
   }

   SimSet* matSet = MATMGR->getMaterialSet();
   if (matSet)
      matSet->addObject((SimObject*)this);

   // save the current script path for texture lookup later
   const String  scriptFile = Con::getVariable("$Con::File");  // current script file - local materials.tscript

   String::SizeType  slash = scriptFile.find('/', scriptFile.length(), String::Right);
   if (slash != String::NPos)
      mPath = scriptFile.substr(0, slash + 1);

   //convert any non-assets we have
   /*for (U32 i = 0; i < MAX_STAGES; i++)
   {
      AUTOCONVERT_IMAGEASSET_ARRAY(DiffuseMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(OverlayMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(LightMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(ToneMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(DetailMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(ORMConfigMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(AOMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(RoughMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(MetalMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(GlowMap, i);
      AUTOCONVERT_IMAGEASSET_ARRAY(DetailNormalMap, i);
   }

   //bind any assets we have
   for (U32 i = 0; i < MAX_STAGES; i++)
   {
      LOAD_IMAGEASSET_ARRAY(DiffuseMap, i);
      LOAD_IMAGEASSET_ARRAY(OverlayMap, i);
      LOAD_IMAGEASSET_ARRAY(LightMap, i);
      LOAD_IMAGEASSET_ARRAY(ToneMap, i);
      LOAD_IMAGEASSET_ARRAY(DetailMap, i);
      LOAD_IMAGEASSET_ARRAY(ORMConfigMap, i);
      LOAD_IMAGEASSET_ARRAY(AOMap, i);
      LOAD_IMAGEASSET_ARRAY(RoughMap, i);
      LOAD_IMAGEASSET_ARRAY(MetalMap, i);
      LOAD_IMAGEASSET_ARRAY(GlowMap, i);
      LOAD_IMAGEASSET_ARRAY(DetailNormalMap, i);
   }*/
   inspectPostApply();

   _mapMaterial();

   return true;
}

void Material::onRemove()
{
   smNormalizeCube = NULL;
   Parent::onRemove();
}

void Material::inspectPostApply()
{
   Parent::inspectPostApply();

   // Reload the material instances which 
   // use this material.
   if (isProperlyAdded())
      reload();
}


bool Material::isLightmapped() const
{
   bool ret = false;
   for (U32 i = 0; i < MAX_STAGES; i++)
      ret |= mLightMapName[i] != StringTable->EmptyString() || mToneMapName[i] != StringTable->EmptyString() || mVertLit[i];
   return ret;
}

void Material::updateTimeBasedParams()
{
   U32 lastTime = MATMGR->getLastUpdateTime();
   F32 dt = MATMGR->getDeltaTime();
   if (mLastUpdateTime != lastTime)
   {
      for (U32 i = 0; i < MAX_STAGES; i++)
      {
         mScrollOffset[i] += mScrollDir[i] * mScrollSpeed[i] * dt;
         mScrollOffset[i].x = mWrapF(mScrollOffset[i].x, 0.0, 1.0);
         mScrollOffset[i].y = mWrapF(mScrollOffset[i].y, 0.0, 1.0);
         mRotPos[i] = mWrapF((mRotPos[i] + (mRotSpeed[i] * dt)), 0.0, 360.0);
         mWavePos[i] = mWrapF((mWavePos[i] + (mWaveFreq[i] * dt)), 0.0, 1.0);
      }
      mLastUpdateTime = lastTime;
   }
}

void Material::_mapMaterial()
{
   if (String(getName()).isEmpty())
   {
      Con::warnf("[Material::mapMaterial] - Cannot map unnamed Material");
      return;
   }

   // If mapTo not defined in script, try to use the base texture name instead
   if (mMapTo.isEmpty())
   {
      if (mDiffuseMapName[0] == StringTable->EmptyString() && mDiffuseMapAsset->isNull())
         return;

      else
      {
         // extract filename from base texture
         if (mDiffuseMapName[0] != StringTable->EmptyString())
         {
            U32 slashPos = String(mDiffuseMapName[0]).find('/', 0, String::Right);
            if (slashPos == String::NPos)
               // no '/' character, must be no path, just the filename
               mMapTo = mDiffuseMapName[0];
            else
               // use everything after the last slash
               mMapTo = String(mDiffuseMapName[0]).substr(slashPos + 1, (U32)strlen(mDiffuseMapName[0]) - slashPos - 1);
         }
         else if (!mDiffuseMapAsset->isNull())
         {
            mMapTo = mDiffuseMapAsset[0]->getImageFileName();
         }
      }
   }

   // add mapping
   MATMGR->mapMaterial(mMapTo, getName());
}

BaseMatInstance* Material::createMatInstance()
{
   return new MatInstance(*this);
}

void Material::flush()
{
   MATMGR->flushInstance(this);
}

void Material::reload()
{
   MATMGR->reInitInstance(this);
}

void Material::StageData::getFeatureSet(FeatureSet* outFeatures) const
{
   TextureTable::ConstIterator iter = mTextures.begin();
   for (; iter != mTextures.end(); iter++)
   {
      if (iter->value.isValid())
         outFeatures->addFeature(*iter->key);
   }
}

DefineEngineMethod(Material, flush, void, (), ,
   "Flushes all material instances that use this material.")
{
   object->flush();
}

DefineEngineMethod(Material, reload, void, (), ,
   "Reloads all material instances that use this material.")
{
   object->reload();
}

DefineEngineMethod(Material, dumpInstances, void, (), ,
   "Dumps a formatted list of the currently allocated material instances for this material to the console.")
{
   MATMGR->dumpMaterialInstances(object);
}

DefineEngineMethod(Material, getMaterialInstances, void, (GuiTreeViewCtrl* matTree), (nullAsType< GuiTreeViewCtrl*>()),
   "Dumps a formatted list of the currently allocated material instances for this material to the console.")
{
   MATMGR->getMaterialInstances(object, matTree);
}

DefineEngineMethod(Material, getAnimFlags, const char*, (U32 id), , "")
{
   char* animFlags = Con::getReturnBuffer(512);

   if (object->mAnimFlags[id] & Material::Scroll)
   {
      if (String::compare(animFlags, "") == 0)
         dStrcpy(animFlags, "$Scroll", 512);
   }
   if (object->mAnimFlags[id] & Material::Rotate)
   {
      if (String::compare(animFlags, "") == 0)
         dStrcpy(animFlags, "$Rotate", 512);
      else
         dStrcat(animFlags, " | $Rotate", 512);
   }
   if (object->mAnimFlags[id] & Material::Wave)
   {
      if (String::compare(animFlags, "") == 0)
         dStrcpy(animFlags, "$Wave", 512);
      else
         dStrcat(animFlags, " | $Wave", 512);
   }
   if (object->mAnimFlags[id] & Material::Scale)
   {
      if (String::compare(animFlags, "") == 0)
         dStrcpy(animFlags, "$Scale", 512);
      else
         dStrcat(animFlags, " | $Scale", 512);
   }
   if (object->mAnimFlags[id] & Material::Sequence)
   {
      if (String::compare(animFlags, "") == 0)
         dStrcpy(animFlags, "$Sequence", 512);
      else
         dStrcat(animFlags, " | $Sequence", 512);
   }

   return animFlags;
}

DefineEngineMethod(Material, setAnimFlags, void, (S32 id, const char *flags), (0, ""), "setAnimFlags")
{
   object->mAnimFlags[id] = 0;

   if (String(flags).find("$Scroll") != String::NPos)
      object->mAnimFlags[id] |= Material::Scroll;

   if (String(flags).find("$Rotate") != String::NPos)
      object->mAnimFlags[id] |= Material::Rotate;

   if (String(flags).find("$Wave") != String::NPos)
      object->mAnimFlags[id] |= Material::Wave;

   if (String(flags).find("$Scale") != String::NPos)
      object->mAnimFlags[id] |= Material::Scale;

   if (String(flags).find("$Sequence") != String::NPos)
      object->mAnimFlags[id] |= Material::Sequence;

   //if we're still unset, see if they tried assigning a number
   if (object->mAnimFlags[id] == 0)
      object->mAnimFlags[id] = dAtoi(flags);

   //if we're *still* unset, make sure we've cleared all cases
   if (object->mAnimFlags[id] == 0)
   {
      object->mScrollOffset[id].set(0.0f, 0.0f);
      object->mRotPos[id] = 0.0f;
      object->mWavePos[id] = 0.0f;
   }

}

DefineEngineMethod(Material, getFilename, const char*, (), , "Get filename of material")
{
   SimObject* material = static_cast<SimObject*>(object);
   return material->getFilename();
}

DefineEngineMethod(Material, isAutoGenerated, bool, (), ,
   "Returns true if this Material was automatically generated by MaterialList::mapMaterials()")
{
   return object->isAutoGenerated();
}

DefineEngineMethod(Material, setAutoGenerated, void, (bool isAutoGenerated), ,
   "setAutoGenerated(bool isAutoGenerated): Set whether or not the Material is autogenerated.")
{
   object->setAutoGenerated(isAutoGenerated);
}

DefineEngineMethod(Material, getAutogeneratedFile, const char*, (), , "Get filename of autogenerated shader file")
{
   SimObject* material = static_cast<SimObject*>(object);
   return material->getFilename();
}


// Accumulation
bool Material::_setAccuEnabled(void* object, const char* index, const char* data)
{
   Material* mat = reinterpret_cast<Material*>(object);

   if (index)
   {
      U32 i = dAtoui(index);
      mat->mAccuEnabled[i] = dAtob(data);
      AccumulationVolume::refreshVolumes();
   }
   return true;
}
//declare general get<entry>, get<entry>Asset and set<entry> methods
//signatures are:
//using DiffuseMap as an example
//material.getDiffuseMap(%layer); //returns the raw file referenced
//material.getDiffuseMapAsset(%layer); //returns the asset id
//material.setDiffuseMap(%texture, %layer); //tries to set the asset and failing that attempts a flat file reference
DEF_IMAGEASSET_ARRAY_BINDS(Material, DiffuseMap)
DEF_IMAGEASSET_ARRAY_BINDS(Material, OverlayMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, LightMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, ToneMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, DetailMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, NormalMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, ORMConfigMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, RoughMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, AOMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, MetalMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, GlowMap);
DEF_IMAGEASSET_ARRAY_BINDS(Material, DetailNormalMap);

