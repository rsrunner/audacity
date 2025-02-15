/*
* Audacity: A Digital Audio Editor
*/
#include "effectsviewregister.h"

using namespace au::effects;

void EffectsViewRegister::regUrl(const muse::String& effectName, const muse::String& viewUrl)
{
    m_data.insert({ effectName, viewUrl });
}

const muse::String& EffectsViewRegister::viewUrl(const muse::String& effectName) const
{
    auto it = m_data.find(effectName);
    if (it != m_data.end()) {
        return it->second;
    }

    static muse::String nofound;
    return nofound;
}
