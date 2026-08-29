# HKClock.exe
add_executable(HKClock WIN32 HKClock.cpp HKClock_res.rc)
target_link_libraries(HKClock comctl32 gdiplus user32 gdi32 winmm ws2_32)
if(MSVC)
    target_link_options(HKClock PRIVATE "LINKER:/MANIFEST:NO")
endif()
