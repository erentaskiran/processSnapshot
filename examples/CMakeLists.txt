# Örnek uygulamalar

add_executable(simple_example simple_example.cpp)
target_link_libraries(simple_example PRIVATE state_checkpoint)

add_executable(auto_save_example auto_save_example.cpp)
target_link_libraries(auto_save_example PRIVATE state_checkpoint)

add_executable(partial_rollback_example partial_rollback_example.cpp)
target_link_libraries(partial_rollback_example PRIVATE state_checkpoint)

# Process Checkpoint & Rollback Demo (Simulated)
add_executable(process_demo process_demo.cpp)
target_link_libraries(process_demo PRIVATE state_checkpoint)

# Real Process Checkpoint Demo (Linux ptrace)
add_executable(real_process_demo real_process_demo.cpp)
target_link_libraries(real_process_demo PRIVATE state_checkpoint)
