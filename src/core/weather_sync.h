#pragma once

namespace app::espnow {
class MasterNode;
}

namespace core::weather_sync {

void tick(app::espnow::MasterNode& master);

}  // namespace core::weather_sync
