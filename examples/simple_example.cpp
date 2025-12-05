#include <iostream>
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"
#include "rollback/rollback_engine.hpp"

using namespace checkpoint;

int main() {
    std::cout << "=== Basit Checkpoint Örneği ===\n\n";
    
    // State Manager oluştur
    StateManager manager("simple_checkpoints/");
    
    // İlk durumu kaydet
    std::string data1 = "Durum 1: Başlangıç verisi";
    StateData state1(data1.begin(), data1.end());
    
    auto cp1 = manager.createCheckpoint("Başlangıç", state1);
    if (cp1.isSuccess()) {
        std::cout << "✓ Checkpoint 1 oluşturuldu: ID = " << *cp1.value << "\n";
    }
    
    // Durumu değiştir
    std::string data2 = "Durum 2: Değiştirilmiş veri";
    StateData state2(data2.begin(), data2.end());
    
    auto cp2 = manager.createCheckpoint("Değişiklik", state2);
    if (cp2.isSuccess()) {
        std::cout << "✓ Checkpoint 2 oluşturuldu: ID = " << *cp2.value << "\n";
    }
    
    // Checkpoint'leri listele
    std::cout << "\nMevcut checkpoint'ler:\n";
    for (const auto& meta : manager.listCheckpoints()) {
        std::cout << "  - " << meta.name << " (ID: " << meta.id << ")\n";
    }
    
    // İlk checkpoint'e dön
    auto result = manager.getCheckpoint(*cp1.value);
    if (result.isSuccess()) {
        auto& data = result.value->getData();
        std::string content(data.begin(), data.end());
        std::cout << "\nCheckpoint 1 içeriği: " << content << "\n";
    }
    
    std::cout << "\n=== Örnek tamamlandı ===\n";
    return 0;
}
