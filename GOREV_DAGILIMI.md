# Görev Dağılımı (Real Process Odaklı)

Simülasyon ve özel instruction-set yolu kaldırıldı. Aşağıdaki alanlar gerçek Linux süreçleri için checkpoint/rollback desteğini kapsar.

## Modüller ve Sorumluluklar
- Core: `types.hpp`, `serializer.hpp` (veri tipleri ve serileştirme)
- State: `state_manager.hpp`, `storage.hpp` (checkpoint yönetimi ve kalıcılık)
- Logger: `operation_logger.hpp` (işlem kayıtları)
- Rollback: `rollback_engine.hpp` (geri yükleme orkestrasyonu)
- Real Process: `ptrace_controller.hpp`, `real_process_types.hpp`, `memory_manager.hpp`, `fd_restorer.hpp` (ptrace tabanlı gerçek süreç yakalama/geri yükleme)

## Önerilen Çalışma Sırası
1. Core + State API'lerini gözden geçir.
2. Real process modülünde ptrace akışını ve bellek yakalama ayrıntılarını incele.
3. Rollback motorunu gerçek süreç verisiyle test et (`real_process_demo`, `real_restore_test_v5`).
4. Eksik test ve dokümantasyonları güncelle.

## Notlar
- ProcessSimulator ve ilgili test/örnekler kaldırıldı; yeni geliştirme gerçek süreç modülüne yapılmalıdır.
