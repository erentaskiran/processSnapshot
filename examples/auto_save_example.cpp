#include <iostream>
#include <thread>
#include <chrono>
#include "state/state_manager.hpp"

using namespace checkpoint;

int main() {
    std::cout << "=== Otomatik Kaydetme Örneği ===\n\n";
    
    StateManager manager("auto_save_checkpoints/");
    
    // Otomatik kaydetmeyi etkinleştir (her 5 saniyede bir)
    manager.setAutoSaveInterval(Duration(5000));
    manager.enableAutoSave(true);
    
    std::cout << "Otomatik kaydetme etkin (5 saniye aralıklarla)\n";
    std::cout << "15 saniye boyunca çalışacak...\n\n";
    
    for (int i = 0; i < 15; ++i) {
        std::string data = "Durum güncellemesi #" + std::to_string(i);
        StateData state(data.begin(), data.end());
        
        // Mevcut durumu güncelle (auto-save bu durumu kaydedecek)
        if (i % 3 == 0) {
            manager.createCheckpoint("Manuel_" + std::to_string(i), state);
            std::cout << "[" << i << "s] Manuel checkpoint oluşturuldu\n";
        } else {
            std::cout << "[" << i << "s] Çalışıyor...\n";
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    manager.enableAutoSave(false);
    
    std::cout << "\nToplam checkpoint sayısı: " << manager.getCheckpointCount() << "\n";
    std::cout << "\n=== Örnek tamamlandı ===\n";
    
    return 0;
}
