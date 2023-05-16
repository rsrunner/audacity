/**********************************************************************

  Audacity: A Digital Audio Editor

  BeatsFormat.cpp

  Michael Papadopoulos

**********************************************************************/

#include "BeatsFormat.h"

void BeatsFormat::SetTickSizes(
   double units, double& major, double& minor, double &minorMinor,
   int& mDigits
) const
{
   // Check that all data is positive
   if (!(mBpm > 0 && mTimeSigUpper > 0 && mTimeSigLower > 0)) return;
   // Also check that the lower time signature is valid (power of 2)
   if(mTimeSigLower & (mTimeSigLower - 1)) return;

   const auto lower = static_cast<double>(mTimeSigLower);
   if (units < .025 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // thirtysecondth notes (label every quarter note)
      minor = 60 / (mBpm * (lower * 2));
      // hundredtwentyeighth notes
      minorMinor = 60 / (mBpm * (lower * 8));
   }
   else if (units < .05 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // sixteenth notes (label every quarter note)
      minor = 60 / (mBpm * (lower));
      // sixtyfourth notes
      minorMinor = 60 / (mBpm * (lower * 4));
   }
   else if (units < .1 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // eigth notes (label every quarter note)
      minor = 60 / (mBpm * (lower / 2));
      // thirtysecondth notes
      minorMinor = 60 / (mBpm * (lower * 2));
   }
   else if (units < .4 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // eigth notes (label every quarter note)
      minor = 60 / (mBpm * (lower / 2));
      // sixteenth notes
      minorMinor = 60 / (mBpm * (lower));
   }
   else if (units < .8 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // quarter notes
      minor = 60 / (mBpm * (lower / 4));
      // sixteenth notes
      minorMinor = 60 / (mBpm * (lower));
   }
   else if (units < 4 * (60 / mBpm) * (4 / lower))
   {
      // measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 4));
      // quarter notes
      minorMinor = 60 / (mBpm * (lower / 4));
   }
   else if (units < 8 * (60 / mBpm) * (4 / lower))
   {
      // four-measures
      major = (60 * mTimeSigUpper) / (mBpm * (lower / 16));
      // measures
      minor = 60 / (mBpm * (lower / 16));
      // half measures
      minorMinor = 60 / (mBpm * (lower / 8));
   }
   else {
      int factor = pow(2, std::floor(log2(std::ceil(units) * (mBpm / 60) * (mTimeSigLower / 4))) - 2);
      major = (60 * mTimeSigUpper) / (mBpm * (lower / (16 * factor)));
      minorMinor = 60 / (mBpm * (lower / (8 * factor)));
   }

   mDigits = 0;
}

void BeatsFormat::SetLabelString(
   wxString& s, double d, double units, double minor, int mDigits, TickType tickType
) const
{
   if (d < 0) {
      return;
   }

   const auto lower = static_cast<double>(mTimeSigLower);
   double val = (mBpm * (lower / 4) * d) / (60 * mTimeSigUpper);
   double beatApprox = (val - floor(val)) * mTimeSigUpper + 1;
   int beat = round(beatApprox);

   // Don't add decimal if it's a major tick or is on the beat
   // Segment by distance with units
   if (units < .4 * (60 / mBpm)* (4 / lower))
   {
      if (tickType == RulerFormat::t_major) {
         s.Printf(wxT("%d"), (int)round(val + 1));
      }
      else if (tickType == RulerFormat::t_minor && abs(beat - beatApprox) < 1.0e-5f) {
         s.Printf(wxT("%d.%d"), (int)floor(val + 1), (int)beat);
      }
   }
   else if (units < .8 * (60 / mBpm) * (4 / lower))
   {
      if (tickType == RulerFormat::t_major) {
         s.Printf(wxT("%d"), (int)round(val + 1));
      }
      else if (tickType == RulerFormat::t_minor && beat != 1) {
         s.Printf(wxT("%d.%d"), (int)floor(val + 1), (int)beat);
      }
   }
   else {
      if (tickType == RulerFormat::t_major) {
         s.Printf(wxT("%d"), (int)round(val + 1));
      }
   }
}

BeatsFormat::~BeatsFormat() = default;
