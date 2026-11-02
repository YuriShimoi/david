#include "core/Effect.h"

namespace david {

void registerOutline();
void registerAdjustments();
void registerFilters();
void registerDither();

void registerBuiltinEffects() {
  static bool done = false;
  if(done) return;
  done = true;
  registerAdjustments();
  registerFilters();
  registerOutline();
  registerDither();
}

}
