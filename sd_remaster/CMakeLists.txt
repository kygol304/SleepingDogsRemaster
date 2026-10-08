cmake_minimum_required(VERSION 3.15)
project(SleepingDogsRemaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(SleepingDogsRemaster SHARED src/dllmain.cpp src/newweapons.cpp)
# Les modèles des nouvelles armes utilisent le SDK SDmodding (external/SD-SDK), qui définit lui-même
# ces macros Windows.
set_source_files_properties(src/newweapons.cpp PROPERTIES COMPILE_OPTIONS "/W3;/wd4005")
target_compile_definitions(SleepingDogsRemaster PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(SleepingDogsRemaster PRIVATE /utf-8 /W4 /EHa)

# Chargé par le jeu à la place de dinput8.dll
set_target_properties(SleepingDogsRemaster PROPERTIES OUTPUT_NAME "dinput8")
