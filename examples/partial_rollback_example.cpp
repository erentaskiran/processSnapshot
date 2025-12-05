#include <iostream>
#include "state/state_manager.hpp"
#include "logger/operation_logger.hpp"
#include "rollback/rollback_engine.hpp"

using namespace checkpoint;

int main() {
    std::cout << "=== Kısmi Geri Alma Örneği ===\n\n";
    
    auto stateManager = std::make_shared<StateManager>("partial_rollback_checkpoints/");
    auto logger = std::make_shared<OperationLogger>();
    auto rollbackEngine = std::make_shared<RollbackEngine>(stateManager, logger);
    
    // Base checkpoint
    std::string baseData = "Base state";
    auto baseCp = stateManager->createCheckpoint("Base", 
        StateData(baseData.begin(), baseData.end()));
    
    std::cout << "Base checkpoint oluşturuldu\n\n";
    
    // Bir dizi işlem yap
    std::cout << "İşlemler gerçekleştiriliyor:\n";
    
    auto op1 = logger->logOperation(OperationType::Create, "Dosya A oluşturuldu");
    std::cout << "  1. Dosya A oluşturuldu (ID: " << op1 << ")\n";
    
    auto op2 = logger->logOperation(OperationType::Update, "Dosya B güncellendi");
    std::cout << "  2. Dosya B güncellendi (ID: " << op2 << ")\n";
    
    auto op3 = logger->logOperation(OperationType::Delete, "Dosya C silindi");
    std::cout << "  3. Dosya C silindi (ID: " << op3 << ")\n";
    
    auto op4 = logger->logOperation(OperationType::Update, "Dosya A güncellendi");
    std::cout << "  4. Dosya A güncellendi (ID: " << op4 << ")\n";
    
    // Kısmi geri alma - sadece Update işlemlerini geri al
    std::cout << "\nSadece UPDATE işlemleri geri alınıyor...\n";
    
    PartialRollbackOptions options;
    options.filter = [](const OperationRecord& op) {
        return op.type == OperationType::Update;
    };
    options.preserveNewerChanges = true;
    
    auto result = rollbackEngine->partialRollback(options);
    
    if (result.isSuccess() && result.value->success) {
        std::cout << "\n✓ Kısmi geri alma başarılı!\n";
        std::cout << "  Geri alınan işlem sayısı: " << result.value->operationsUndone << "\n";
        
        if (!result.value->warnings.empty()) {
            std::cout << "  Uyarılar:\n";
            for (const auto& warning : result.value->warnings) {
                std::cout << "    - " << warning << "\n";
            }
        }
    }
    
    std::cout << "\n=== Örnek tamamlandı ===\n";
    return 0;
}
