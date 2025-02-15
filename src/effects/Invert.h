/**********************************************************************

  Audacity: A Digital Audio Editor

  Invert.h

  Mark Phillips

  This class inverts the selected audio.

**********************************************************************/

#ifndef __AUDACITY_EFFECT_INVERT__
#define __AUDACITY_EFFECT_INVERT__

#include "StatefulEffectUIServices.h"
#include "StatefulPerTrackEffect.h"

class EffectInvert final :
    public StatefulPerTrackEffect,
    public StatefulEffectUIServices
{
public:
   static const ComponentInterfaceSymbol Symbol;

   EffectInvert();
   virtual ~EffectInvert();

   // ComponentInterface implementation

   ComponentInterfaceSymbol GetSymbol() const override;
   TranslatableString GetDescription() const override;

   // EffectDefinitionInterface implementation

   EffectType GetType() const override;
   bool IsInteractive() const override;

   unsigned GetAudioInCount() const override;
   unsigned GetAudioOutCount() const override;
   size_t ProcessBlock(EffectSettings &settings,
      const float *const *inBlock, float *const *outBlock, size_t blockLen)
      override;

   bool NeedsDither() const override;
};

#endif
