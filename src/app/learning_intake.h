#pragma once

// App-layer intake of one injection read-back (P2 learning loop).
//
// The platform layer only observes and posts kInjectionReflectionMessage; every
// policy decision lives here so the injector keeps its single responsibility and
// the sensitive/history rules stay in one auditable place.

#include "app_messages.h"
#include "config_store.h"

namespace learning_intake {

// Handles one reflection message. Safe to call with a failed read-back: it logs
// the reason and returns without touching the store.
void HandleReflection(const InjectionReflection& reflection, const Config& config);

} // namespace learning_intake
