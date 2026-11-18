#include "amp/model.hpp"
#include "models_internal.hpp"

namespace amp {
std::unique_ptr<PropagationModel> create_model(ModelId id) {
    switch (id) {
        case ModelId::Enhanced: return std::make_unique<EnhancedClassicModel>();
        case ModelId::ItuP1147: return std::make_unique<ITUP1147Model>();
        case ModelId::Classic:
        default:                return std::make_unique<ClassicModel>();
    }
}
const char *model_name(ModelId id) {
    switch (id) {
        case ModelId::Enhanced: return "Klassisch+ (Norton, Hops)";
        case ModelId::ItuP1147: return "ITU-R P.1147-2 (mit Jahreszeiten)";
        default:                return "Klassisch (heuristisch)";
    }
}
}  // namespace amp
