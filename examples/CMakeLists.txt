# Örnek uygulamalar

add_executable(simple_example simple_example.cpp)
target_link_libraries(simple_example PRIVATE state_checkpoint)

add_executable(auto_save_example auto_save_example.cpp)
target_link_libraries(auto_save_example PRIVATE state_checkpoint)

add_executable(partial_rollback_example partial_rollback_example.cpp)
target_link_libraries(partial_rollback_example PRIVATE state_checkpoint)

# Real Process Checkpoint Demo (Linux ptrace)
add_executable(real_process_demo real_process_demo.cpp)
target_link_libraries(real_process_demo PRIVATE state_checkpoint)

# Real Process Restore Test
add_executable(real_restore_test real_restore_test.cpp)
target_link_libraries(real_restore_test PRIVATE state_checkpoint)

# Real Process Restore Test v2
add_executable(real_restore_test_v2 real_restore_test_v2.cpp)
target_link_libraries(real_restore_test_v2 PRIVATE state_checkpoint)

# Real Process Restore Test v3 (Debug)
add_executable(real_restore_test_v3 real_restore_test_v3.cpp)
target_link_libraries(real_restore_test_v3 PRIVATE state_checkpoint)

# Real Process Restore Test v4 (Memory verification)
add_executable(real_restore_test_v4 real_restore_test_v4.cpp)
target_link_libraries(real_restore_test_v4 PRIVATE state_checkpoint)

# Real Process Restore Test v5 (State check)
add_executable(real_restore_test_v5 real_restore_test_v5.cpp)
target_link_libraries(real_restore_test_v5 PRIVATE state_checkpoint)
