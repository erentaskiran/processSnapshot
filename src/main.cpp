#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>

#include "core/types.hpp"
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"
#include "rollback/rollback_engine.hpp"
#include "utils/helpers.hpp"

using namespace checkpoint;

// Demo uygulama durumu
struct ApplicationState {
    int counter;
    std::string message;
    std::vector<int> data;
    
    StateData serialize() const {
        StateData result;
        
        // Counter
        result.insert(result.end(), 
                     reinterpret_cast<const uint8_t*>(&counter),
                     reinterpret_cast<const uint8_t*>(&counter) + sizeof(counter));
        
        // Message length + message
        uint32_t msgLen = static_cast<uint32_t>(message.size());
        result.insert(result.end(),
                     reinterpret_cast<const uint8_t*>(&msgLen),
                     reinterpret_cast<const uint8_t*>(&msgLen) + sizeof(msgLen));
        result.insert(result.end(), message.begin(), message.end());
        
        // Data size + data
        uint32_t dataSize = static_cast<uint32_t>(data.size());
        result.insert(result.end(),
                     reinterpret_cast<const uint8_t*>(&dataSize),
                     reinterpret_cast<const uint8_t*>(&dataSize) + sizeof(dataSize));
        for (int val : data) {
            result.insert(result.end(),
                         reinterpret_cast<const uint8_t*>(&val),
                         reinterpret_cast<const uint8_t*>(&val) + sizeof(val));
        }
        
        return result;
    }
    
    static ApplicationState deserialize(const StateData& data) {
        ApplicationState state;
        size_t offset = 0;
        
        // Counter
        std::memcpy(&state.counter, data.data() + offset, sizeof(int));
        offset += sizeof(int);
        
        // Message
        uint32_t msgLen;
        std::memcpy(&msgLen, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        state.message = std::string(data.begin() + offset, data.begin() + offset + msgLen);
        offset += msgLen;
        
        // Data
        uint32_t dataSize;
        std::memcpy(&dataSize, data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        for (uint32_t i = 0; i < dataSize; ++i) {
            int val;
            std::memcpy(&val, data.data() + offset, sizeof(int));
            offset += sizeof(int);
            state.data.push_back(val);
        }
        
        return state;
    }
    
    void print() const {
        std::cout << "  Counter: " << counter << "\n";
        std::cout << "  Message: " << message << "\n";
        std::cout << "  Data: [";
        for (size_t i = 0; i < data.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << data[i];
        }
        std::cout << "]\n";
    }
};

void printSeparator() {
    std::cout << "\n" << std::string(60, '=') << "\n\n";
}

int main() {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════════╗
    ║     Durum Kaydı & Geri Alma Sistemi - Demo Uygulaması     ║
    ╚═══════════════════════════════════════════════════════════╝
    )" << std::endl;
    
    std::cout << "Logger yapılandırılıyor..." << std::endl;
    std::cout.flush();
    
    // Logger'ı yapılandır (sadece console, dosya yok)
    auto& logger = OperationLogger::getInstance();
    logger.setMinLevel(LogLevel::Info);
    // Dosya output'u devre dışı - sadece console
    
    std::cout << "State Manager oluşturuluyor..." << std::endl;
    std::cout.flush();
    
    // State Manager oluştur (memory storage kullan, dosya değil)
    auto stateManager = std::make_shared<StateManager>();  // Memory storage
    
    std::cout << "Rollback Engine oluşturuluyor..." << std::endl;
    std::cout.flush();
    
    // Rollback Engine oluştur
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, 
                          std::make_shared<OperationLogger>());
    
    std::cout << "Başlatma tamamlandı!" << std::endl;
    std::cout.flush();
    
    // Uygulama durumu
    ApplicationState appState{0, "Başlangıç", {}};
    
    printSeparator();
    std::cout << "Başlangıç durumu:\n";
    appState.print();
    
    // İlk checkpoint oluştur
    auto cp1Result = stateManager->createCheckpoint("Başlangıç", appState.serialize());
    if (cp1Result.isSuccess()) {
        std::cout << "\n✓ Checkpoint 1 oluşturuldu (ID: " << *cp1Result.value << ")\n";
        logger.logOperation(OperationType::Checkpoint, "Başlangıç checkpoint'i", *cp1Result.value);
    }
    
    printSeparator();
    
    // Durumu değiştir
    std::cout << "İşlem 1: Counter artırılıyor...\n";
    appState.counter = 10;
    appState.message = "Counter güncellendi";
    appState.data = {1, 2, 3, 4, 5};
    logger.logOperation(OperationType::Update, "Counter 10 yapıldı");
    
    std::cout << "Güncel durum:\n";
    appState.print();
    
    // İkinci checkpoint
    auto cp2Result = stateManager->createCheckpoint("İşlem 1 sonrası", appState.serialize());
    if (cp2Result.isSuccess()) {
        std::cout << "\n✓ Checkpoint 2 oluşturuldu (ID: " << *cp2Result.value << ")\n";
    }
    
    printSeparator();
    
    // Daha fazla değişiklik
    std::cout << "İşlem 2: Daha fazla değişiklik...\n";
    appState.counter = 100;
    appState.message = "Büyük değişiklik";
    appState.data = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    logger.logOperation(OperationType::Update, "Büyük güncelleme yapıldı");
    
    std::cout << "Güncel durum:\n";
    appState.print();
    
    // Üçüncü checkpoint
    auto cp3Result = stateManager->createCheckpoint("Büyük değişiklik", appState.serialize());
    if (cp3Result.isSuccess()) {
        std::cout << "\n✓ Checkpoint 3 oluşturuldu (ID: " << *cp3Result.value << ")\n";
    }
    
    printSeparator();
    
    // Tüm checkpoint'leri listele
    std::cout << "Mevcut Checkpoint'ler:\n";
    std::cout << std::string(40, '-') << "\n";
    
    auto checkpoints = stateManager->listCheckpoints();
    for (const auto& meta : checkpoints) {
        std::cout << "  ID: " << meta.id << "\n";
        std::cout << "  Ad: " << meta.name << "\n";
        std::cout << "  Oluşturulma: " << utils::TimeUtils::formatTimestamp(meta.createdAt) << "\n";
        std::cout << "  Boyut: " << meta.dataSize << " bytes\n";
        std::cout << std::string(40, '-') << "\n";
    }
    
    printSeparator();
    
    // Geri alma önizlemesi
    if (cp1Result.isSuccess()) {
        std::cout << "Checkpoint 1'e geri alma önizlemesi:\n";
        auto preview = rollbackEngine->previewRollback(*cp1Result.value);
        std::cout << "  Geri alınacak işlem sayısı: " << preview.size() << "\n";
    }
    
    printSeparator();
    
    // Geri alma işlemi
    if (cp2Result.isSuccess()) {
        std::cout << "Checkpoint 2'ye geri alınıyor...\n\n";
        
        auto rollbackResult = rollbackEngine->rollbackToCheckpoint(*cp2Result.value);
        if (rollbackResult.isSuccess() && rollbackResult.value->success) {
            std::cout << "✓ Geri alma başarılı!\n";
            std::cout << "  Geri alınan işlem sayısı: " << rollbackResult.value->operationsUndone << "\n";
            std::cout << "  Süre: " << utils::TimeUtils::formatDuration(rollbackResult.value->timeTaken) << "\n";
            
            // Durumu güncelle
            auto cpResult = stateManager->getCheckpoint(*cp2Result.value);
            if (cpResult.isSuccess()) {
                appState = ApplicationState::deserialize(cpResult.value->getData());
                std::cout << "\nGeri yüklenmiş durum:\n";
                appState.print();
            }
        }
    }
    
    printSeparator();
    
    // İstatistikler
    std::cout << "Sistem İstatistikleri:\n";
    std::cout << "  Toplam checkpoint sayısı: " << stateManager->getCheckpointCount() << "\n";
    std::cout << "  Toplam depolama boyutu: " << stateManager->getTotalStorageSize() << " bytes\n";
    std::cout << "  Geri alma sayısı: " << rollbackEngine->getRollbackCount() << "\n";
    std::cout << "  Toplam geri alma süresi: " 
              << utils::TimeUtils::formatDuration(rollbackEngine->getTotalRollbackTime()) << "\n";
    
    printSeparator();
    
    // Son log kayıtları
    std::cout << "Son Log Kayıtları:\n";
    std::cout << std::string(60, '-') << "\n";
    
    auto entries = logger.getEntries(LogLevel::Info, 10);
    for (const auto& entry : entries) {
        std::cout << entry.toString() << "\n";
    }
    
    printSeparator();
    
    std::cout << "Demo tamamlandı!\n\n";
    
    LOG_INFO("Demo", "Demo uygulaması sonlandırıldı");
    logger.flush();
    
    return 0;
}
