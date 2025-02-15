/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "modularity/ioc.h"
#include "audioplugins/iknownaudiopluginsregister.h"
#include "effects/ieffectsconfiguration.h"
#include "global/iinteractive.h"

#include "effects/ieffectsprovider.h"

namespace au::effects {
class EffectsProvider : public IEffectsProvider
{
    muse::Inject<IEffectsConfiguration> configuration;
    muse::Inject<muse::audioplugins::IKnownAudioPluginsRegister> knownPlugins;
    muse::Inject<muse::IInteractive> interactive;

public:
    void reloadEffects() override;

    EffectMetaList effectMetaList() const override;
    muse::async::Notification effectMetaListChanged() const override;

    EffectCategoryList effectsCategoryList() const override;

    EffectMeta meta(const muse::String& effectId) const override;

private:
    mutable EffectMetaList m_effects;
    muse::async::Notification m_effectsChanged;

    mutable EffectCategoryList m_effectsCategories;
};
}
