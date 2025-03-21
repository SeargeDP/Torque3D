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

#include "sfx/sfxEnvironment.h"
#include "sim/netConnection.h"
#include "core/stream/bitStream.h"
#include "core/module.h"
#include "console/typeValidators.h"


IMPLEMENT_CO_DATABLOCK_V1( SFXEnvironment );


// Reverb flags.
static const U32 sReverbFlagDecayTimeScale         = 0x001;
static const U32 sReverbFlagReflectionsScale       = 0x002;
//static const U32 sReverbFlagReflectionsDelayScale  = 0x004; unused, but kept for doc purposes -BJR
static const U32 sReverbFlagReverbScale            = 0x008;
static const U32 sReverbFlagReverbDelayScale       = 0x010;
static const U32 sReverbFlagDecayHFLimit           = 0x020;
static const U32 sReverbFlagEchoTimeScale          = 0x040;
static const U32 sReverbFlagModulationTimeScale    = 0x080;
static const U32 sReverbFlagCore0                  = 0x100;
static const U32 sReverbFlagCore1                  = 0x200;
static const U32 sReverbFlagHighQualityReverb      = 0x400;
static const U32 sReverbFlagHighQualityDPL2Reverb  = 0x800;

AFTER_MODULE_INIT( SFX )
{
   Con::addConstant( "SFXEnvironment::REVERB_DECAYTIMESCALE", TypeS32, &sReverbFlagDecayTimeScale,
      "SFXEnvironment::envSize affects reverberation decay time.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_REFLECTIONSSCALE", TypeS32, &sReverbFlagReflectionsScale,
      "SFXEnvironment::envSize affects reflection level.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_REFLECTIONSDELAYSCALE", TypeS32, &sReverbFlagReflectionsScale,
      "SFXEnvironment::envSize affects initial reflection delay time.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_REVERBSCALE", TypeS32, &sReverbFlagReverbScale,
      "SFXEnvironment::envSize affects reflections level.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_REVERBDELAYSCALE", TypeS32, &sReverbFlagReverbDelayScale,
      "SFXEnvironment::envSize affects late reverberation delay time.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_DECAYHFLIMIT", TypeS32, &sReverbFlagDecayHFLimit,
      "SFXEnvironment::airAbsorptionHF affects SFXEnvironment::decayHFRatio.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_ECHOTIMESCALE", TypeS32, &sReverbFlagEchoTimeScale,
      "SFXEnvironment::envSize affects echo time.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_MODULATIONTIMESCALE", TypeS32, &sReverbFlagModulationTimeScale,
      "SFXEnvironment::envSize affects modulation time.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_CORE0", TypeS32, &sReverbFlagCore0,
      "PS2 Only - Reverb is applied to CORE0 (hw voices 0-23).\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_CORE1", TypeS32, &sReverbFlagCore1,
      "PS2 Only - Reverb is applied to CORE1 (hw voices 24-47).\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_HIGHQUALITYREVERB", TypeS32, &sReverbFlagHighQualityReverb,
      "GameCube/Wii Only - Use high-quality reverb.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
   Con::addConstant( "SFXEnvironment::REVERB_HIGHQUALITYDPL2REVERB", TypeS32, &sReverbFlagHighQualityDPL2Reverb,
      "GameCube/Wii Only - Use high-quality DPL2 reverb.\n"
      "@see SFXEnvironment::flags\n\n"
      "@ingroup SFXEnvironment" );
}


ConsoleDocClass( SFXEnvironment,
   "@brief Description of a reverb environment.\n\n"
   
   "A reverb environment specifies how the audio mixer should render advanced environmental audio "
   "effects.  \n\n"
   
   "To use reverb environments in your level, set up one or more ambient audio spaces, assign "
   "reverb environments appropriately, and then attach the SFXAmbiences to your LevelInfo (taking effect "
   "globally) or Zone objects (taking effect locally).\n\n"
   
   "To define your own custom reverb environments, it is usually easiest to adapt one of the pre-existing "
   "reverb definitions:\n"
   
   "@tsexample_nopar\n"
   "singleton SFXEnvironment( AudioEnvCustomUnderwater : AudioEnvUnderwater )\n"
   "{\n"
   "   // Override select properties from AudioEnvUnderwater here.\n"
   "};\n"
   "@endtsexample\n\n"
   
   "In the Datablock Editor, this can be done by selecting an existing environment to copy from when creating "
   "the SFXEnvironment datablock.\n\n"
   
   "For a precise description of reverb audio and the properties of this class, please consult the EAX "
   "documentation.\n\n"
   
   "All SFXEnvironment instances are automatically added to the global @c SFXEnvironmentSet.\n\n"
   
   "@see http://www.atc.creative.com/algorithms/eax20.pdf\n"
   "@see http://connect.creativelabs.com/developer/Gaming/Forms/AllItems.aspx\n"
   "@see SFXAmbience::environment\n\n"
   "@ref SFX_reverb\n"
   "@ingroup SFX\n"
);


//-----------------------------------------------------------------------------

SFXEnvironment::SFXEnvironment()
{
}

//-----------------------------------------------------------------------------

void SFXEnvironment::initPersistFields()
{
   docsURL;
   addGroup( "Reverb" );
   
   addFieldV("reverbDensity", TypeRangedF32, Offset(mReverb.flDensity, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Density of reverb environment.");
   addFieldV("reverbDiffusion", TypeRangedF32, Offset(mReverb.flDiffusion, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Environment diffusion.");
   addFieldV("reverbGain", TypeRangedF32, Offset(mReverb.flGain, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Gain Level.");
   addFieldV("reverbGainHF", TypeRangedF32, Offset(mReverb.flGainHF, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Gain to high frequencies");
   addFieldV("reverbGainLF", TypeRangedF32, Offset(mReverb.flGainLF, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Gain to high frequencies");
   addFieldV("reverbDecayTime", TypeRangedF32, Offset(mReverb.flDecayTime, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Decay time for the reverb.");
   addFieldV("reverbDecayHFRatio", TypeRangedF32, Offset(mReverb.flDecayHFRatio, SFXEnvironment), &CommonValidators::PositiveFloat,
      "High frequency decay time ratio.");
   addFieldV("reverbDecayLFRatio", TypeRangedF32, Offset(mReverb.flDecayLFRatio, SFXEnvironment), &CommonValidators::PositiveFloat,
      "High frequency decay time ratio.");
   addFieldV("reflectionsGain", TypeRangedF32, Offset(mReverb.flReflectionsGain, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reflection Gain.");
   addFieldV("reflectionDelay", TypeRangedF32, Offset(mReverb.flReflectionsDelay, SFXEnvironment), &CommonValidators::PositiveFloat,
      "How long to delay reflections.");
   addFieldV("reflectionsPan", TypeRangedF32, Offset(mReverb.flReflectionsPan, SFXEnvironment), &CommonValidators::F32Range, 3,
      "Reflection reverberation panning vector.");
   addFieldV("lateReverbGain", TypeRangedF32, Offset(mReverb.flLateReverbGain, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Late reverb gain amount.");
   addFieldV("lateReverbDelay", TypeRangedF32, Offset(mReverb.flLateReverbDelay, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Late reverb delay time.");
   addFieldV("lateReverbPan", TypeRangedF32, Offset(mReverb.flLateReverbPan, SFXEnvironment), &CommonValidators::F32Range, 3,
      "Late reverberation panning vector.");
   addFieldV("reverbEchoTime", TypeRangedF32, Offset(mReverb.flEchoTime, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb echo time.");
   addFieldV("reverbEchoDepth", TypeRangedF32, Offset(mReverb.flEchoDepth, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb echo depth.");
   addFieldV("reverbModTime", TypeRangedF32, Offset(mReverb.flModulationTime, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Modulation time.");
   addFieldV("reverbModDepth", TypeRangedF32, Offset(mReverb.flModulationDepth, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Modulation time.");
   addFieldV("airAbsorbtionGainHF", TypeRangedF32, Offset(mReverb.flAirAbsorptionGainHF, SFXEnvironment), &CommonValidators::PositiveFloat,
      "High Frequency air absorbtion");
   addFieldV("reverbHFRef", TypeRangedF32, Offset(mReverb.flHFReference, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb High Frequency Reference.");
   addFieldV("reverbLFRef", TypeRangedF32, Offset(mReverb.flLFReference, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Reverb Low Frequency Reference.");
   addFieldV("roomRolloffFactor", TypeRangedF32, Offset(mReverb.flRoomRolloffFactor, SFXEnvironment), &CommonValidators::PositiveFloat,
      "Rolloff factor for reverb.");
   addFieldV("decayHFLimit", TypeRangedS32, Offset(mReverb.iDecayHFLimit, SFXEnvironment), &CommonValidators::PositiveInt,
      "High Frequency decay limit.");
   endGroup("Reverb");

   Parent::initPersistFields();
}

//-----------------------------------------------------------------------------

bool SFXEnvironment::onAdd()
{
   if( !Parent::onAdd() )
      return false;
      
   Sim::getSFXEnvironmentSet()->addObject( this );
      
   return true;
}

//-----------------------------------------------------------------------------

bool SFXEnvironment::preload( bool server, String& errorStr )
{
   if( !Parent::preload( server, errorStr ) )
      return false;
      
   validate();
   
   return true;
}

//-----------------------------------------------------------------------------

void SFXEnvironment::inspectPostApply()
{
   Parent::inspectPostApply();
   validate();
}

//-----------------------------------------------------------------------------

void SFXEnvironment::validate()
{
   mReverb.validate();
}

//-----------------------------------------------------------------------------

void SFXEnvironment::packData( BitStream* stream )
{
   Parent::packData( stream );

   stream->write(mReverb.flDensity);
   stream->write(mReverb.flDiffusion);
   stream->write(mReverb.flGain);
   stream->write(mReverb.flGainHF);
   stream->write(mReverb.flGainLF);
   stream->write(mReverb.flDecayTime);
   stream->write(mReverb.flDecayHFRatio);
   stream->write(mReverb.flDecayLFRatio);
   stream->write(mReverb.flReflectionsGain);
   stream->write(mReverb.flReflectionsDelay);
   stream->write(mReverb.flLateReverbGain);
   stream->write(mReverb.flLateReverbDelay);
   stream->write(mReverb.flEchoTime);
   stream->write(mReverb.flEchoDepth);
   stream->write(mReverb.flModulationTime);
   stream->write(mReverb.flModulationDepth);
   stream->write(mReverb.flAirAbsorptionGainHF);
   stream->write(mReverb.flHFReference);
   stream->write(mReverb.flLFReference);
   stream->write(mReverb.flRoomRolloffFactor);
   stream->write(mReverb.iDecayHFLimit);
}

//-----------------------------------------------------------------------------

void SFXEnvironment::unpackData( BitStream* stream )
{
   Parent::unpackData( stream );
   
   stream->read(&mReverb.flDensity);
   stream->read(&mReverb.flDiffusion);
   stream->read(&mReverb.flGain);
   stream->read(&mReverb.flGainHF);
   stream->read(&mReverb.flGainLF);
   stream->read(&mReverb.flDecayTime);
   stream->read(&mReverb.flDecayHFRatio);
   stream->read(&mReverb.flDecayLFRatio);
   stream->read(&mReverb.flReflectionsGain);
   stream->read(&mReverb.flReflectionsDelay);
   stream->read(&mReverb.flLateReverbGain);
   stream->read(&mReverb.flLateReverbDelay);
   stream->read(&mReverb.flEchoTime);
   stream->read(&mReverb.flEchoDepth);
   stream->read(&mReverb.flModulationTime);
   stream->read(&mReverb.flModulationDepth);
   stream->read(&mReverb.flAirAbsorptionGainHF);
   stream->read(&mReverb.flHFReference);
   stream->read(&mReverb.flLFReference);
   stream->read(&mReverb.flRoomRolloffFactor);
   stream->read(&mReverb.iDecayHFLimit);
}
